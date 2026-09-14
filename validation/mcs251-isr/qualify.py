#!/usr/bin/env python3
"""MCS251 ISR model qualification harness (T10).

Modes (interface frozen by the T10 card):

    qualify.py --self-test
        Runs the full self-test against fake RSP/qtest transports and exits.
        Requires no QEMU. Prints per-check lines and the final verdict line
        "ISR qualification self-test PASS" (or ... FAIL) on stdout.

    qualify.py --manifest ABS_JSON --case {hardware-frame,single,nested-windows,long-run,default}
        [--iterations N]
        Validates the manifest, verifies the frozen tool/image identity
        (sha256), launches the model per the frozen command line and executes
        the requested case. Without a usable QEMU environment this mode fails
        loudly; it never degrades into --self-test and a timeout is never a
        PASS.

Model interface facts are verified against the model source (read-only) and
frozen by the T10 card:

  * QOM CPU path              /machine/soc/cpu
  * qtest IRQ injection       set_irq_in QOM-PATH NAME NUM LEVEL
  * GDB core registers        0-31 R0-R31, 32-39 R56-R63,
                              40 PSW, 41 PSW1, 42 PC (32-bit, big-endian)
  * SFR physical window       0x01000000 + (SFR address - 0x80)
  * Wired device IRQ inputs   slots 0-4 only (INT0, T0, INT1, T1, UART1);
                              higher slots are static layout only and a
                              set_irq_in attempt may legitimately answer
                              FAIL, which is recorded as a model capability
                              limit, never worked around.
  * Single-step IRQ mask      the gdbstub defaults sstep_flags to
                              ENABLE|NOIRQ|NOTIMER and masks CPU_INTERRUPT_
                              HARD out of every step; the harness negotiates
                              "Qqemu.sstep=1" (SSTEP_ENABLE only) after
                              connect and reads the mask back with
                              "qqemu.sstep" as proof, otherwise an injected
                              interrupt is never accepted while
                              single-stepping.

Run methodology (per the T10 rework):

  * one absolute monotonic deadline per case; every transport read derives
    its budget from it (DeadlineTransport), and run_case catches
    CaseTimeout, rsp.RspTimeout and qtest.QTestTimeout uniformly — TIMEOUT
    has the highest classification priority and is never a PASS;
  * the acceptance baseline is never "leave reset and inject": the harness
    single-steps through CRT init (HOME/BOOT from the map) to a stable
    application main-loop instruction boundary; that boundary's SPX is S
    and its PC is the real interrupted return PC;
  * acceptance must occur on the first single-step after injection:
    SPX == S+4, then the 4B frame at [S+1..S+4] must match the independent
    pre-step (PSW1, PC) snapshot; deferral is NOT RUN, frame mismatch FAIL;
  * the save window is verified granularly (PSW +1, DR0..DPX +4 each) to
    exactly S+41; the body supports any local frame F (max SPX is evidence,
    never the save anchor); the restore window anchors at the first SPX
    below S+41 and must follow the exact inverse chain to RETI at S;
  * nested-windows snapshots every prescribed window of every layer
    (main -> low -> high -> low -> main) with full register sets, requires
    the high layer to actually clobber sentinel registers, and proves a
    shared bit-byte modification survives each RETI;
  * frozen baseline bytes (vector EJMP form, default entry C2AF80FE) are
    classified BASELINE_MISMATCH, never mixed into ordinary assertion FAIL.

Run outcome classification is exactly one of
PASS / FAIL / TIMEOUT / MODEL_UNSUPPORTED / BASELINE_MISMATCH.
TIMEOUT is never PASS. The per-board ledger (board-results.json) uses the
frozen six states PASS / FAIL / NOT_RUN / MODEL_UNSUPPORTED /
BASELINE_MISMATCH / BLOCKED_NO_BOARD.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import qtest  # noqa: E402  (local module, same directory)
import rsp  # noqa: E402  (local module, same directory)

# ---------------------------------------------------------------------------
# Frozen constants
# ---------------------------------------------------------------------------

RESULT_PASS = "PASS"
RESULT_FAIL = "FAIL"
RESULT_TIMEOUT = "TIMEOUT"
RESULT_MODEL_UNSUPPORTED = "MODEL_UNSUPPORTED"
RESULT_BASELINE_MISMATCH = "BASELINE_MISMATCH"
RUN_RESULTS = (
    RESULT_PASS,
    RESULT_FAIL,
    RESULT_TIMEOUT,
    RESULT_MODEL_UNSUPPORTED,
    RESULT_BASELINE_MISMATCH,
)

BOARD_STATE_PASS = "PASS"
BOARD_STATE_FAIL = "FAIL"
BOARD_STATE_NOT_RUN = "NOT_RUN"
BOARD_STATE_MODEL_UNSUPPORTED = "MODEL_UNSUPPORTED"
BOARD_STATE_BASELINE_MISMATCH = "BASELINE_MISMATCH"
BOARD_STATE_BLOCKED_NO_BOARD = "BLOCKED_NO_BOARD"
BOARD_STATES = (
    BOARD_STATE_PASS,
    BOARD_STATE_FAIL,
    BOARD_STATE_NOT_RUN,
    BOARD_STATE_MODEL_UNSUPPORTED,
    BOARD_STATE_BASELINE_MISMATCH,
    BOARD_STATE_BLOCKED_NO_BOARD,
)

CASES = ("hardware-frame", "single", "nested-windows", "long-run", "default")
LONG_RUN_DEFAULT_ITERATIONS = 10000

MACHINE = "stc32g144k246"
QOM_CPU_PATH = qtest.QOM_CPU_PATH
IRQ_GPIO_NAME = qtest.IRQ_GPIO_NAME_UNNAMED

# A6 fixed frame accounting (bytes per accepted interrupt).
FRAME_HARDWARE_BYTES = 4
FRAME_SOFTWARE_SAVE_BYTES = 37  # 1 (PSW) + 9 * 4 (DR0..DR28, DPX)
FRAME_LAYER_BYTES = FRAME_HARDWARE_BYTES + FRAME_SOFTWARE_SAVE_BYTES  # 41

# A6 fixed software save order (top of the 37B software save sequence).
SAVE_SEQUENCE = ("PSW", "DR0", "DR4", "DR8", "DR12", "DR16", "DR20", "DR24",
                 "DR28", "DPX")

# A5 frozen default-entry machine bytes: clr EA; sjmp halt.
DEFAULT_ENTRY_BYTES = bytes.fromhex("C2AF80FE")

# SFR bit for EA inside IE (0xA8), used by the default-entry case.
IE_EA_BIT = 0x80

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_USAGE = 2
EXIT_PREREQ_MISSING = 3

SELF_TEST_PASS_LINE = "ISR qualification self-test PASS"
SELF_TEST_FAIL_LINE = "ISR qualification self-test FAIL"


# ---------------------------------------------------------------------------
# Result classification
# ---------------------------------------------------------------------------


def classify_result(timeout=False, model_unsupported=False, baseline_error=None,
                    assertion_failures=()):
    """Classify one run outcome into exactly one frozen result.

    Priority (first hit wins):
      1. timeout                -> TIMEOUT       (a timeout is never a PASS)
      2. model capability limit -> MODEL_UNSUPPORTED
      3. tool/image identity    -> BASELINE_MISMATCH
      4. assertion failures     -> FAIL
      5. otherwise              -> PASS
    """
    if timeout:
        return RESULT_TIMEOUT
    if model_unsupported:
        return RESULT_MODEL_UNSUPPORTED
    if baseline_error:
        return RESULT_BASELINE_MISMATCH
    if assertion_failures:
        return RESULT_FAIL
    return RESULT_PASS


def board_state_for(result, board_provided=True, attempted=True):
    """Map a run result onto the frozen six-state board ledger."""
    if result not in RUN_RESULTS:
        raise ValueError("unknown run result %r" % (result,))
    if not board_provided:
        return BOARD_STATE_BLOCKED_NO_BOARD
    if not attempted:
        return BOARD_STATE_NOT_RUN
    if result == RESULT_TIMEOUT:
        # The board ledger has no TIMEOUT state; a timeout books as FAIL and
        # keeps the TIMEOUT detail in the run record.
        return BOARD_STATE_FAIL
    return result


# ---------------------------------------------------------------------------
# A6 fixed frame ledger (SPX accounting)
# ---------------------------------------------------------------------------

# Canonical A6 ledger phases, in frozen order. The observed SPX at each phase
# is anchored at S = SPX before interrupt acceptance.
LEDGER_PHASES = (
    "accept_pending",     # before acceptance            S
    "hardware_frame",     # after hardware 4B frame      S+4
    "software_saved",     # after 37B software save      S+41
    "local_frame",        # after local frame (F bytes)  S+41+F
    "local_teardown",     # after local frame removal    S+41
    "state_restored",     # after 37B restore            S+4
    "reti_done",          # after RETI                   S
)


def expected_spx_points(s, local_bytes=0):
    """Canonical A6 ledger points for one ISR layer with base SPX `s`."""
    if s < 0 or local_bytes < 0:
        raise ValueError("negative SPX base or local frame size")
    return (
        ("accept_pending", s),
        ("hardware_frame", s + FRAME_HARDWARE_BYTES),
        ("software_saved", s + FRAME_LAYER_BYTES),
        ("local_frame", s + FRAME_LAYER_BYTES + local_bytes),
        ("local_teardown", s + FRAME_LAYER_BYTES),
        ("state_restored", s + FRAME_HARDWARE_BYTES),
        ("reti_done", s),
    )


def expected_save_window_points(s):
    """Granular SPX points inside the save/restore window of one layer.

    After the hardware frame (S+4) the software save sequence pushes PSW
    (1 byte) then DR0..DR28 and DPX (4 bytes each), ending at S+41. The
    restore is the exact inverse order. Points are named "<reg>_saved" /
    "<reg>_restored" so per-window evidence can be compared against them.
    """
    points = [("hardware_frame", s + FRAME_HARDWARE_BYTES)]
    spx = s + FRAME_HARDWARE_BYTES
    for name in SAVE_SEQUENCE:
        spx += 1 if name == "PSW" else 4
        points.append(("%s_saved" % name.lower(), spx))
    for name in reversed(SAVE_SEQUENCE):
        spx -= 1 if name == "PSW" else 4
        points.append(("%s_restored" % name.lower(), spx))
    if points[-1][1] != s + FRAME_HARDWARE_BYTES:
        raise AssertionError("save window accounting is not symmetric")
    return tuple(points)


def verify_spx_ledger(observations, points):
    """Check observed (phase, spx) samples against an expected point list.

    observations: ordered sequence of (phase, spx) pairs, as sampled during a
    run. points: ordered expected pairs, e.g. from expected_spx_points() or
    expected_save_window_points().

    A mismatch is a dict {phase, expected, actual, kind}. Kinds:
      unknown-phase  - observed a phase that is not in the expected list
      out-of-order   - observed a phase whose only expected position lies
                       *before* the current cursor (i.e. it was already
                       consumed; the ledger went backwards)
      spx-mismatch   - phase seen at the right position with the wrong SPX
      missing-phase  - an expected point was never observed (including the
                       points skipped over to resynchronize after a jump)
    """
    points = list(points)
    known_index = {}
    for index, (phase, _) in enumerate(points):
        known_index.setdefault(phase, index)
    mismatches = []
    cursor = 0
    for phase, spx in observations:
        if cursor >= len(points):
            mismatches.append(
                {"phase": phase,
                 "kind": "out-of-order" if phase in known_index
                 else "unknown-phase", "actual": spx}
            )
            continue
        if phase == points[cursor][0]:
            if spx != points[cursor][1]:
                mismatches.append(
                    {"phase": phase, "kind": "spx-mismatch",
                     "expected": points[cursor][1], "actual": spx}
                )
            cursor += 1
            continue
        # Resynchronize: is this phase expected later in the ledger?
        ahead = next(
            (i for i in range(cursor, len(points))
             if points[i][0] == phase),
            None,
        )
        if ahead is not None:
            for skipped in range(cursor, ahead):
                mismatches.append(
                    {"phase": points[skipped][0], "kind": "missing-phase",
                     "expected": points[skipped][1]}
                )
            if spx != points[ahead][1]:
                mismatches.append(
                    {"phase": phase, "kind": "spx-mismatch",
                     "expected": points[ahead][1], "actual": spx}
                )
            cursor = ahead + 1
        elif phase in known_index:
            mismatches.append(
                {"phase": phase, "kind": "out-of-order", "actual": spx}
            )
        else:
            mismatches.append(
                {"phase": phase, "kind": "unknown-phase", "actual": spx}
            )
    while cursor < len(points):
        mismatches.append(
            {"phase": points[cursor][0], "kind": "missing-phase",
             "expected": points[cursor][1]}
        )
        cursor += 1
    return mismatches


# ---------------------------------------------------------------------------
# State snapshots and comparison
# ---------------------------------------------------------------------------

# Fields that must be captured for an interrupted-boundary snapshot
# (T10 card step 7): R0-R31, the R56-R63 bank, PSW, PSW1, PC, plus SPX which
# is read through the SFR window rather than the GDB register list.
SNAPSHOT_FIELDS = tuple(name for _, name, _ in rsp.GDB_REGISTERS) + ("SPX",)

# Registers whose live values the nested high layer must actually clobber
# (T10 card step 10): R0-R31 and R56-R63. PSW/PSW1/PC/SPX are excluded —
# they are frame-managed, not body sentinels.
SENTINEL_REGISTERS = frozenset(
    name for name in SNAPSHOT_FIELDS
    if re.match(r"^R\d+$", name)
    and (0 <= int(name[1:]) <= 31 or 56 <= int(name[1:]) <= 63)
)


def take_snapshot(rsp_client, timeout=None):
    """Capture one full boundary snapshot. Returns {field: value}.

    Only actually observable values are returned; nothing is fabricated for
    fields the model cannot show (per the T10 card: read the actually visible
    value first, then compare the restore).
    """
    snapshot = rsp_client.read_all_registers(timeout=timeout)
    snapshot["SPX"] = rsp_client.read_spx(timeout=timeout)
    return snapshot


def compare_snapshots(before, after, fields=SNAPSHOT_FIELDS):
    """Compare two snapshots field by field.

    Returns (mismatches, not_compared):
      mismatches    - list of dicts {field, before, after} for every field
                      present in both snapshots with different values
      not_compared  - sorted list of field names missing from at least one
                      side; these are reported, never treated as equal.
    """
    mismatches = []
    not_compared = []
    for field in fields:
        if field not in before or field not in after:
            not_compared.append(field)
            continue
        if before[field] != after[field]:
            mismatches.append(
                {"field": field, "before": before[field], "after": after[field]}
            )
    return mismatches, sorted(not_compared)


class BitWatch:
    """Shared bit-byte proof around one ISR layer (T10 card step 13).

    Samples the 16B bit-byte area (flat data addresses 0x20-0x2F) during the
    ISR body and checks after RETI that a shared bit modification survives:

      * require_change (the single-layer probe and the nested high layer):
        if no byte ever changed during the body, the fixture is not
        exercising the shared-bit contract — an assertion failure, never an
        empty pass.
      * Rollback is asserted for bytes that were still changed at the last
        body sample: a compiler/restore-window rollback returns the area to
        its entry state after the body finished, so any byte still changed
        when the ISR ended its body must remain changed after RETI. Bytes a
        scratch user cleaned up inside the body are deliberately not
        flagged — the frozen failure mode this watch targets is a rollback
        performed by the restore path, after the last body instruction.
    """

    def __init__(self, rsp_client, deadline, require_change=True):
        self._rsp = rsp_client
        self._deadline = deadline
        self._require_change = require_change
        self.before = None
        self.ever_changed = set()
        self.last_changed = set()
        self.samples = 0

    def _read(self):
        return self._rsp.read_memory(
            BIT_AREA_ADDR, BIT_AREA_SIZE, timeout=_remaining(self._deadline)
        )

    def start(self):
        self.before = self._read()

    def sample(self):
        if self.before is None:
            return
        self.samples += 1
        current = self._read()
        changed = {
            offset for offset in range(BIT_AREA_SIZE)
            if current[offset] != self.before[offset]
        }
        self.ever_changed |= changed
        self.last_changed = changed

    def finish(self):
        after = self._read()
        rolled_back = sorted(
            offset for offset in self.last_changed
            if after[offset] == self.before[offset]
        )
        failures = []
        if rolled_back:
            failures.append(
                {"field": "shared-bit-rolled-back-at-reti",
                 "before": "bit-area offsets %r still modified at the last "
                           "body sample" % rolled_back,
                 "after": "restored to entry bytes after RETI"}
            )
        if self._require_change and not self.ever_changed:
            failures.append(
                {"field": "shared-bit-change", "before": "(unchanged)",
                 "after": "(fixture ISR must modify the shared bit area; "
                          "an ISR that never writes it cannot prove the "
                          "no-rollback contract)"}
            )
        evidence = {
            "before": self.before.hex() if self.before else None,
            "after": after.hex(),
            "changed_offsets": sorted(self.ever_changed),
            "samples": self.samples,
        }
        return failures, evidence


# ---------------------------------------------------------------------------
# Manifest handling (frozen schema)
# ---------------------------------------------------------------------------

MANIFEST_SCHEMA = 1
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")

MANIFEST_REQUIRED_KEYS = (
    "schema", "qemu", "tools", "machine", "image", "map", "output_dir",
    "gdb_port", "qtest_socket", "timeout_seconds",
)


class ManifestError(Exception):
    """The manifest does not satisfy the frozen schema."""


def _is_absolute_path(value):
    """Absolute in either the POSIX (WSL) or the native sense.

    Manifest paths in this campaign are WSL POSIX paths (/home/liu/...),
    which os.path.isabs() rejects on a Windows host; a leading '/' is
    therefore accepted explicitly.
    """
    return bool(value) and (value.startswith("/") or os.path.isabs(value))


def _require_absolute(value, what):
    if not isinstance(value, str) or not value:
        raise ManifestError("%s must be a non-empty string" % what)
    if not _is_absolute_path(value):
        raise ManifestError("%s must be an absolute path: %r" % (what, value))
    return value


def _require_sha256(value, what):
    if not isinstance(value, str) or not SHA256_RE.match(value):
        raise ManifestError("%s must be 64 lowercase hex chars" % what)
    return value


def validate_manifest(doc, require_runtime_ready=True):
    """Validate a manifest document against the frozen schema.

    require_runtime_ready=True additionally rejects gdb_port=0: the card
    allows 0 only while *generating* the manifest to request port
    allocation; before any run it must be replaced by the PM-assigned
    exclusive port.
    """
    if not isinstance(doc, dict):
        raise ManifestError("manifest must be a JSON object")
    for key in MANIFEST_REQUIRED_KEYS:
        if key not in doc:
            raise ManifestError("manifest missing required key %r" % key)
    # True == 1 in Python, so the value comparison alone would let a JSON
    # `true` masquerade as schema 1; the type must be exactly int.
    if (not isinstance(doc["schema"], int) or isinstance(doc["schema"], bool)
            or doc["schema"] != MANIFEST_SCHEMA):
        raise ManifestError(
            "manifest schema %r != %d" % (doc["schema"], MANIFEST_SCHEMA)
        )
    for section in ("qemu", "image"):
        entry = doc[section]
        if not isinstance(entry, dict):
            raise ManifestError("manifest %r must be an object" % section)
        _require_absolute(entry.get("path"), "%s.path" % section)
        _require_sha256(entry.get("sha256"), "%s.sha256" % section)
    if not isinstance(doc["tools"], dict):
        raise ManifestError("manifest 'tools' must be an object")
    for name, path in sorted(doc["tools"].items()):
        _require_absolute(path, "tools[%r]" % name)
    if doc["machine"] != MACHINE:
        raise ManifestError(
            "manifest machine %r != %r" % (doc["machine"], MACHINE)
        )
    _require_absolute(doc["map"], "map")
    _require_absolute(doc["output_dir"], "output_dir")
    _require_absolute(doc["qtest_socket"], "qtest_socket")
    port = doc["gdb_port"]
    if not isinstance(port, int) or isinstance(port, bool) or not 0 <= port <= 65535:
        raise ManifestError("gdb_port must be an integer in 0..65535")
    if require_runtime_ready and port == 0:
        raise ManifestError(
            "gdb_port=0 only requests allocation at generation time; "
            "replace it with the PM-assigned exclusive port before running"
        )
    seconds = doc["timeout_seconds"]
    if not isinstance(seconds, int) or isinstance(seconds, bool) or seconds <= 0:
        raise ManifestError("timeout_seconds must be a positive integer")
    return doc


def load_manifest(path, require_runtime_ready=True):
    with open(path, "r", encoding="utf-8") as handle:
        doc = json.load(handle)
    return validate_manifest(doc, require_runtime_ready=require_runtime_ready)


def sha256_of_file(path, chunk_size=1 << 20):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        while True:
            chunk = handle.read(chunk_size)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def verify_identity(path, expected_sha256):
    """Verify a file's sha256. Returns error message or None."""
    if not os.path.isfile(path):
        return "missing file: %s" % path
    actual = sha256_of_file(path)
    if actual != expected_sha256:
        return (
            "sha256 mismatch for %s: manifest %s, actual %s"
            % (path, expected_sha256, actual)
        )
    return None


# ---------------------------------------------------------------------------
# Map parsing (T07 map format)
#
# The map contains two kinds of lines consumed here:
#   IRQ lines (frozen by the T07 card): "IRQ NNN 0xADDR TAG [SYMBOL]"
#   Section lines (LinkerCore::buildMap): "path:name 0xADDR +0xSIZE"
#
# G1 (PM ruling 2026-09-13) widened the profile to 127 slots, so the slot
# field is one to THREE decimal digits and 100..126 are three wide.  The
# pattern is a shape check only: the accepted number is validated against
# the profile and the address against the frozen formula, and any line whose
# first token is "IRQ" but that does not parse is an error rather than a
# silently dropped row (a two-digit-only pattern used to discard all 27
# high slots without a word).
# ---------------------------------------------------------------------------

# G1 profile geometry and classification, independently restated here so this
# qualification harness never imports the product table it is checking.
ISR_VECTOR_BASE = 0xFF0003
ISR_VECTOR_STRIDE = 8
ISR_VECTOR_COUNT = 127
ISR_VECTOR_MAX_SLOT = ISR_VECTOR_COUNT - 1  # 126

# The 18 in-profile non-legal slots: 16 Reserved + 2 System.
ISR_SYSTEM_SLOTS = frozenset({14, 15})
ISR_NON_LEGAL_SLOTS = frozenset({7, 13, 14, 15, 22, 23, 32, 33, 34, 35,
                                 81, 92, 93, 94, 95, 100, 101, 113})

IRQ_MAP_TAG_ISR = "ISR"
IRQ_MAP_TAG_DEFAULT = "DEFAULT"
IRQ_MAP_TAG_RESERVED = "RESERVED"
IRQ_MAP_TAG_SYSTEM = "SYSTEM"
IRQ_MAP_TAGS = frozenset({IRQ_MAP_TAG_ISR, IRQ_MAP_TAG_DEFAULT,
                          IRQ_MAP_TAG_RESERVED, IRQ_MAP_TAG_SYSTEM})
# Tags that name a registered/unregistered handler and therefore carry a
# symbol; the reserved/system rows carry none.
IRQ_MAP_SYMBOL_TAGS = frozenset({IRQ_MAP_TAG_ISR, IRQ_MAP_TAG_DEFAULT})

_IRQ_MAP_LINE = re.compile(
    r"^IRQ\s+(\d{1,3})\s+0x([0-9a-fA-F]+)\s+(\S+)(?:\s+(\S+))?\s*$"
)
_MAP_SECTION_LINE = re.compile(
    r"^(\S+?):(\S+)\s+0x([0-9a-fA-F]+)\s+\+0x([0-9a-fA-F]+)\s*$"
)


def parse_irq_map(map_path):
    """Parse the IRQ table from the link map, strictly.

    Returns {slot: {"addr": int, "tag": str, "symbol": str-or-None}}.
    Note: `addr` is the *vector slot address* (0xFF0003 + 8*slot), never the
    target function address.

    G1: every row is validated as it is read and the first bad row raises
    CaseEnvironmentError, so a malformed map can never be silently reduced to
    a smaller table.  A two-digit-only slot field used to drop all 27 of the
    100..126 rows without a word; the checks below are what make that
    impossible now:

      * a line whose first token is ``IRQ`` must match the frozen shape
        (one to three decimal digits, ``0x``-prefixed address, known tag),
      * the slot must be inside the G1 profile (0..126),
      * the tag must be one of ISR / DEFAULT / RESERVED / SYSTEM,
      * a RESERVED/SYSTEM row must carry no symbol while an ISR/DEFAULT row
        must carry one,
      * the tag must agree with the frozen G1 classification: a
        Reserved/System slot is a RESERVED/SYSTEM row (and specifically 14
        and 15 are SYSTEM), every other slot is a handler row,
      * the address must satisfy the frozen ``0xFF0003 + 8*slot`` formula,
      * a slot may appear once; a repeated slot is an error rather than a
        silent dictionary overwrite (a wrong tag on a duplicate would
        otherwise hide the first row entirely).

    Row-count completeness is deliberately NOT enforced here: a caller that
    needs the whole 127-row table checks the returned length (the image
    checker owns that assertion), while the self-test exercises the parser
    on short synthetic maps.
    """
    table = {}
    with open(map_path, "r", encoding="utf-8", errors="replace") as handle:
        for lineno, line in enumerate(handle, 1):
            stripped = line.strip()
            if not stripped:
                continue
            match = _IRQ_MAP_LINE.match(line)
            if match is None:
                # Only complain about lines that claim to be IRQ rows; other
                # map content is somebody else's business.
                if stripped.split(None, 1)[0] == "IRQ":
                    raise CaseEnvironmentError(
                        "%s:%d: malformed IRQ row (want \"IRQ NNN 0xADDR "
                        "TAG [SYMBOL]\" with one to three slot digits): %r"
                        % (map_path, lineno, stripped)
                    )
                continue
            slot = int(match.group(1))
            addr = int(match.group(2), 16)
            tag = match.group(3)
            symbol = match.group(4)
            if not 0 <= slot <= ISR_VECTOR_MAX_SLOT:
                raise CaseEnvironmentError(
                    "%s:%d: IRQ row slot %d is outside the G1 profile 0-%d"
                    % (map_path, lineno, slot, ISR_VECTOR_MAX_SLOT)
                )
            if tag not in IRQ_MAP_TAGS:
                raise CaseEnvironmentError(
                    "%s:%d: IRQ row %d has unknown tag %r (want one of %s)"
                    % (map_path, lineno, slot, tag,
                       "/".join(sorted(IRQ_MAP_TAGS)))
                )
            if (tag in IRQ_MAP_SYMBOL_TAGS) != (symbol is not None):
                raise CaseEnvironmentError(
                    "%s:%d: IRQ row %d tag %s must %s a symbol"
                    % (map_path, lineno, slot, tag,
                       "carry" if tag in IRQ_MAP_SYMBOL_TAGS else "not carry")
                )
            # The tag must agree with the frozen classification, so a map
            # cannot relabel a Reserved slot as a handler (or vice versa)
            # without being caught here.
            expected_tag = (
                IRQ_MAP_TAG_SYSTEM if slot in ISR_SYSTEM_SLOTS
                else IRQ_MAP_TAG_RESERVED if slot in ISR_NON_LEGAL_SLOTS
                else None
            )
            if expected_tag is not None:
                if tag != expected_tag:
                    raise CaseEnvironmentError(
                        "%s:%d: IRQ row %d is a non-legal slot and must be "
                        "%s, got %s"
                        % (map_path, lineno, slot, expected_tag, tag)
                    )
            elif tag not in IRQ_MAP_SYMBOL_TAGS:
                raise CaseEnvironmentError(
                    "%s:%d: IRQ row %d is a legal slot and must carry a "
                    "handler tag, got %s"
                    % (map_path, lineno, slot, tag)
                )
            expected_addr = vector_addr(slot)
            if addr != expected_addr:
                raise CaseEnvironmentError(
                    "%s:%d: IRQ row %d address 0x%x violates the frozen "
                    "formula 0xFF0003 + 8*slot = 0x%x"
                    % (map_path, lineno, slot, addr, expected_addr)
                )
            if slot in table:
                raise CaseEnvironmentError(
                    "%s:%d: IRQ row %d appears more than once"
                    % (map_path, lineno, slot)
                )
            table[slot] = {"addr": addr, "tag": tag, "symbol": symbol}
    return table


def parse_map_sections(map_path):
    """Parse "path:name 0xADDR +0xSIZE" section lines from the link map.

    Returns a list of {"path", "name", "addr", "size"} in file order.
    """
    sections = []
    with open(map_path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = _MAP_SECTION_LINE.match(line)
            if match:
                sections.append(
                    {"path": match.group(1), "name": match.group(2),
                     "addr": int(match.group(3), 16),
                     "size": int(match.group(4), 16)}
                )
    return sections


def protected_crt_ranges(map_sections):
    """Address ranges of the frozen CRT sections (HOME/BOOT).

    walk_to_main_loop() must step *through* these but never mistake a CRT
    loop for the application main loop, so every PC inside these ranges is
    rejected as a boundary candidate.
    """
    ranges = []
    for section in map_sections:
        if section["name"] in (".mcs251.HOME", ".mcs251.BOOT"):
            ranges.append((section["addr"], section["addr"] + section["size"]))
    return ranges


def default_slots(irq_map):
    """Slots the map classifies as DEFAULT (unregistered legal slots)."""
    return sorted(slot for slot, entry in irq_map.items()
                  if entry["tag"] == IRQ_MAP_TAG_DEFAULT)


# ---------------------------------------------------------------------------
# Fake transports for the self-test (no QEMU involved)
# ---------------------------------------------------------------------------


class ScriptedTransport:
    """Fake byte transport: fires scripted replies when a request appears.

    rules: list of (needle, response). When `needle` appears in the
    accumulated written bytes, `response` is appended to the receive buffer
    (each rule fires at most once). `read` raises the builtin TimeoutError
    when the receive buffer is empty, which is exactly how a silent peer
    behaves.
    """

    def __init__(self, rules=None):
        self.written = bytearray()
        self.received = bytearray()
        self.rules = list(rules or [])

    def expect(self, needle, response):
        self.rules.append((needle, response))

    def feed(self, data):
        self.received.extend(data)

    def write(self, data):
        self.written.extend(data)
        fired = []
        for rule in self.rules:
            needle, response = rule
            if needle in self.written:
                fired.append(rule)
                self.received.extend(response)
        for rule in fired:
            self.rules.remove(rule)

    def read(self, n, timeout):
        if not self.received:
            raise TimeoutError("scripted transport: no data (peer silent)")
        chunk = bytes(self.received[:n])
        del self.received[:n]
        return chunk

    def close(self):
        pass


# ---------------------------------------------------------------------------
# Scripted MCS251 model for the self-test (no QEMU involved)
# ---------------------------------------------------------------------------


class FakeIsrModel:
    """A minimal scripted model speaking the frozen interfaces.

    Implements just enough of the model side for harness-logic coverage:
      * RSP 'g'/'s'/'m'/'p'/'?' plus ack framing and ASCII-hex payloads;
      * "Qqemu.sstep=<mask>" negotiation — without clearing SSTEP_NOIRQ the
        model never takes a pending IRQ on a step, mirroring cpu-exec.c:831;
      * qtest set_irq_in with edge-latched pending state;
      * a CRT loop inside BOOT, then a stable two-instruction main loop;
      * per-slot ISR execution following A6: 4B hardware frame (PSW1,
        PC[23:16], PC[7:0], PC[15:8] at S+1..S+4), vector EJMP, granular
        37B save (PSW +1, nine +4 pushes), an 8B local frame, sentinel
        register clobbers (restored before RETI), a shared bit-byte write,
        exact inverse restore, RETI;
      * unregistered legal slots route to the 4B fail-stop default entry
        C2AF80FE whose first instruction clears EA;
      * fault injection via `faults` for detection coverage.
    """

    # Placeholder self-test addresses, re-spaced for the G1 layout: they
    # must not collide with each other.  The real CRT default is taken from
    # the symbol/map, never from here.
    CRT_LOOP = (0x00FF0500, 0x00FF0502)
    MAIN_LOOP = (0x00FF0700, 0x00FF0702)
    ISR_ENTRY = {1: 0x00FF0800, 3: 0x00FF0900}
    DEFAULT_ENTRY = 0x00FF0A00
    REGISTERED = (1, 3)
    SAVE_NAMES = ("DR0", "DR4", "DR8", "DR12", "DR16", "DR20", "DR24",
                  "DR28", "DPX")

    def __init__(self, faults=()):
        self.faults = set(faults)
        self.sstep_mask = 0x7  # ENABLE|NOIRQ|NOTIMER, as gdbstub.c:76
        self.regs = {name: 0 for _, name, _ in rsp.GDB_REGISTERS}
        self.regs["PSW"] = 0x04
        self.regs["PSW1"] = 0x20
        self.spx = 0x0400
        self.pc = self.CRT_LOOP[0]
        self.crt_steps = 0
        self.ie = 0xFF  # fixture enabled EA and the slot masks
        self.pending = set()
        self.line_level = {}
        self.stack = {}
        self.bit = bytearray(BIT_AREA_SIZE)
        self.layers = []
        self.accept_count = {}  # per-slot acceptance count (fail-second-visit)
        # "deferred-accept" / "deferred-accept-2": execute this many normal
        # instructions before taking a pending interrupt (rework ruling b
        # coverage; the -2 variant wraps the two-PC main loop back onto the
        # boundary PC, which must NOT book a PASS).
        self.defer_left = 0
        if "deferred-accept" in self.faults:
            self.defer_left = 3
        if "deferred-accept-2" in self.faults:
            self.defer_left = 2
        self.rom = {}
        # G1: fill the whole profile, but only Legal slots carry an EJMP; a
        # Reserved/System slot is a NOBITS hole with no payload, exactly as
        # the linker synthesizes it.  The class comes from the module-level
        # G1 restatement, never from a second local literal.
        for slot in range(ISR_VECTOR_COUNT):
            if slot in ISR_NON_LEGAL_SLOTS:
                continue
            target = (self.ISR_ENTRY[slot] if slot in self.REGISTERED
                      else self.DEFAULT_ENTRY)
            self.rom[vector_addr(slot)] = 0x8A
            self.rom[vector_addr(slot) + 1] = (target >> 16) & 0xFF
            self.rom[vector_addr(slot) + 2] = (target >> 8) & 0xFF
            self.rom[vector_addr(slot) + 3] = target & 0xFF
        for offset, byte in enumerate(DEFAULT_ENTRY_BYTES):
            self.rom[self.DEFAULT_ENTRY + offset] = byte

    # -- qtest side ---------------------------------------------------------

    def set_irq_line(self, num, level):
        previous = self.line_level.get(num, 0)
        self.line_level[num] = level
        if level == 1 and previous == 0:
            self.pending.add(num)

    # -- CPU side -----------------------------------------------------------

    def step(self):
        negotiated = (self.sstep_mask & SSTEP_NOIRQ) == 0
        if negotiated and self.pending:
            if self.defer_left > 0:
                # "deferred-accept": keep executing normal instructions and
                # take the interrupt later (rework ruling b coverage).
                self.defer_left -= 1
            else:
                self._accept(min(self.pending))
                return
        if self.layers:
            self._isr_step()
            return
        if self.pc in self.CRT_LOOP:
            if "no-main-loop" in self.faults:
                return
            self.crt_steps += 1
            self.pc = (self.MAIN_LOOP[0] if self.crt_steps >= 8
                       else self.CRT_LOOP[1])
            return
        self.pc = (self.MAIN_LOOP[1]
                   if self.pc == self.MAIN_LOOP[0] else self.MAIN_LOOP[0])

    def _accept(self, slot):
        self.pending.discard(slot)
        self.accept_count[slot] = self.accept_count.get(slot, 0) + 1
        default = slot not in self.REGISTERED
        entry = (self.ISR_ENTRY[slot] if not default
                 else self.DEFAULT_ENTRY)
        psw1, interrupted_pc = self.regs["PSW1"], self.pc
        corrupt = "frame-corrupt" in self.faults
        self._push(0x00 if corrupt else psw1)
        self._push((interrupted_pc >> 16) & 0xFF)
        self._push(interrupted_pc & 0xFF)
        self._push((interrupted_pc >> 8) & 0xFF)
        if "pc-only-corrupt" in self.faults:
            # Third-round counterexample A: only the frame's PC[7:0] byte
            # (at S+3) is corrupted; PSW1 and the real interrupted PC are
            # correct. The frame is "self-consistent" for its own decoded
            # PC — the pre-step snapshot comparison must reject it.
            self.stack[self.spx - 1] ^= 0x40
        self.pc = vector_addr(slot)
        # "fail-second-visit" (R3 regression): the second acceptance of the
        # same slot corrupts its save window (skips the DPX push).
        skip_dpx = ("fail-second-visit" in self.faults
                    and self.accept_count[slot] >= 2)
        self.layers.append(
            {"slot": slot, "entry": entry, "return_pc": interrupted_pc,
             "count": 0, "saved": {}, "default": default,
             "skip_dpx": skip_dpx}
        )

    def _isr_step(self):
        layer = self.layers[-1]
        if self.pc == vector_addr(layer["slot"]):
            self.pc = layer["entry"]
            return
        if layer["default"]:
            # A5 fail-stop default entry: clr EA once, then sjmp halt
            # forever. No software save, no RETI, no stack movement.
            if self.pc == self.DEFAULT_ENTRY:
                self.ie &= ~IE_EA_BIT
                self.pc = self.DEFAULT_ENTRY + 2
            return
        layer["count"] += 1
        n = layer["count"]
        silent = "silent-body" in self.faults and layer["slot"] == 3
        if n == 1:  # push PSW (1 byte)
            layer["saved"]["PSW"] = self.regs["PSW"]
            self._push(self.regs["PSW"])
        elif 2 <= n <= 10:  # push DR0..DPX (4 bytes each; skip DPX when
            # save-corrupt or fail-second-visit) — 1 + 9*4 = 37 bytes total
            name = self.SAVE_NAMES[n - 2]
            layer["saved"][name] = (0x11 * n) & 0xFF
            if not (("save-corrupt" in self.faults and name == "DPX")
                    or (layer.get("skip_dpx") and name == "DPX")):
                for _ in range(4):
                    self._push(layer["saved"][name])
        elif n == 11:  # local frame +8
            self.spx += 8
        elif n == 12:  # body: sentinel clobbers + shared bit write
            if not silent:
                layer["r5"] = self.regs["R5"]
                layer["r60"] = self.regs["R60"]
                self.regs["R5"] ^= 0xFF
                self.regs["R60"] = (self.regs["R60"] + 0x5A) & 0xFF
                # R3 ruling (f): toggle, not OR — every round produces an
                # observable change, so multi-round coverage proofs stay
                # honest. The slot's own bit is 3 (high) / 4 (low).
                layer["bit_index"] = 3 if layer["slot"] == 3 else 4
                layer["bit_old"] = self.bit[layer["bit_index"]]
                self.bit[layer["bit_index"]] ^= 0x01
                if "low-scratch-clean" in self.faults \
                        and layer["slot"] == 1:
                    layer["scratch"] = 6
                    self.bit[6] |= 0x80
        elif n == 13:  # local frame -8 (+ legal in-body scratch cleanup)
            self.spx -= 8
            if "low-scratch-clean" in self.faults \
                    and layer["slot"] == 1 and layer.get("scratch") == 6:
                self.bit[6] &= 0x7F  # cleaned inside the body: legal
        elif 14 <= n <= 22:  # pop DPX..DR0 (inverse save order, 4B each)
            name = self.SAVE_NAMES[22 - n]
            for _ in range(4):
                self.stack.pop(self.spx, 0)
                self.spx -= 1
        elif n == 23:  # pop PSW (1 byte); the compiler restores its clobbers
            self.stack.pop(self.spx, 0)
            self.spx -= 1
            self.regs["PSW"] = layer["saved"]["PSW"]
            if not silent:
                self.regs["R5"] = layer["r5"]
                self.regs["R60"] = layer["r60"]
            if "bit-rollback" in self.faults and "bit_index" in layer:
                self.bit[layer["bit_index"]] = layer["bit_old"]
            if "low-restores-clears-bit" in self.faults \
                    and layer["slot"] == 1:
                # R2 negative: the low restore path wrongly clears the bit
                # the high layer set (offset 3), after the low body last
                # observed it modified.
                self.bit[3] &= 0xFE
        elif n == 24:  # RETI: PC[15:8], PC[7:0], PC[23:16], PSW1
            hi8 = self.stack.pop(self.spx, 0)
            self.spx -= 1
            lo8 = self.stack.pop(self.spx, 0)
            self.spx -= 1
            hi16 = self.stack.pop(self.spx, 0)
            self.spx -= 1
            self.regs["PSW1"] = self.stack.pop(self.spx, 0)
            self.spx -= 1
            self.pc = (hi16 << 16) | (hi8 << 8) | lo8
            self.layers.pop()

    def _push(self, byte):
        self.spx += 1
        self.stack[self.spx] = byte & 0xFF

    # -- memory -------------------------------------------------------------

    def mem_read(self, addr, length):
        out = bytearray()
        for offset in range(length):
            address = addr + offset
            if 0x01000000 <= address < 0x01000100:
                out.append(self._sfr_read(0x80 + address - 0x01000000))
            elif BIT_AREA_ADDR <= address < BIT_AREA_ADDR + BIT_AREA_SIZE:
                out.append(self.bit[address - BIT_AREA_ADDR])
            elif address >= 0xFF0000:
                out.append(self.rom.get(address, 0x00))
            else:
                out.append(self.stack.get(address, 0x00))
        return bytes(out)

    def _sfr_read(self, sfr):
        if sfr == rsp.SFR_SP:
            return self.spx & 0xFF
        if sfr == rsp.SFR_SPH:
            return (self.spx >> 8) & 0xFF
        if sfr == rsp.SFR_IE:
            return self.ie
        return 0


class FakeRspTransport:
    """Transport that answers RSP packets by driving a FakeIsrModel."""

    def __init__(self, model):
        self.model = model
        self.rx = bytearray()
        self.buf = bytearray()

    def write(self, data):
        self.buf.extend(data)
        while True:
            start = self.buf.find(b"$")
            if start < 0:
                self.buf.clear()  # stray acks/noise
                return
            end = self.buf.find(b"#", start)
            if end < 0 or len(self.buf) < end + 3:
                if start > 0:
                    del self.buf[:start]
                return
            payload = rsp.parse_packet(bytes(self.buf[start:end + 3]))
            del self.buf[:end + 3]
            self.rx += b"+" + self._respond(payload)

    def _respond(self, payload):
        model = self.model
        if payload == b"g":
            model.regs["PC"] = model.pc & 0xFFFFFFFF  # keep 'g' in sync
            reply = rsp.encode_registers(model.regs)
        elif payload.startswith(b"m"):
            addr_text, len_text = payload[1:].split(b",")
            data = model.mem_read(int(addr_text, 16), int(len_text, 16))
            reply = data.hex().encode("ascii")
        elif payload.startswith(b"p"):
            index = int(payload[1:], 16)
            width = dict((idx, w) for idx, _, w in rsp.GDB_REGISTERS)[index]
            value = model.regs[
                [name for idx, name, _ in rsp.GDB_REGISTERS
                 if idx == index][0]
            ]
            reply = value.to_bytes(width, "big").hex().encode("ascii")
        elif payload == b"s":
            model.step()
            reply = b"S05"
        elif payload == b"?":
            reply = b"S05"
        elif payload.startswith(b"Qqemu.sstep="):
            mask = int(payload.split(b"=")[1], 16)
            if mask & ~SSTEP_IRQ_ALLOWED_MASK:
                reply = b"E22"
            else:
                model.sstep_mask = mask
                reply = b"OK"
        elif payload == b"qqemu.sstep":
            reply = ("0x%x" % model.sstep_mask).encode("ascii")
        # The historical "qR qemu.sstep" form deliberately has no branch
        # here: "qR" is not a legal read prefix and the embedded space is
        # not a valid packet name, so it falls through to the unknown-
        # packet branch below and is answered with an empty reply — exactly
        # what a compliant stub (real QEMU included) does. Giving it a
        # branch is how R04 was masked in the first place.
        elif payload == b"D":
            reply = b"OK"
        else:
            reply = b""
        return rsp.encode_packet(reply)

    def read(self, n, timeout):
        if not self.rx:
            raise TimeoutError("fake rsp: no reply")
        chunk = bytes(self.rx[:n])
        del self.rx[:n]
        return chunk

    def close(self):
        pass


class FakeQtestTransport:
    """Transport that feeds qtest command lines to a FakeIsrModel."""

    def __init__(self, model):
        self.model = model
        self.rx = bytearray()
        self.buf = bytearray()

    def write(self, data):
        self.buf.extend(data)
        while b"\n" in self.buf:
            line, _, rest = self.buf.partition(b"\n")
            self.buf = bytearray(rest)
            words = line.decode("ascii", "replace").split()
            if not words:
                continue
            if words[0] == "set_irq_in":
                self.model.set_irq_line(int(words[3]), int(words[4]))
                self.rx += b"OK\n"
            else:
                self.rx += b"OK\n"

    def read(self, n, timeout):
        if not self.rx:
            raise TimeoutError("fake qtest: no reply")
        chunk = bytes(self.rx[:n])
        del self.rx[:n]
        return chunk

    def close(self):
        pass


class FakeSession:
    """Session stand-in wiring FakeIsrModel through DeadlineTransport.

    Mirrors the QemuSession connection lifecycle (R3): connect_rsp /
    connect_qtest reuse one cached client per case so multi-round runs
    share the transport and model state instead of stacking connections.
    """

    def __init__(self, manifest, deadline=None, model=None, dead_rsp=False):
        self.manifest = manifest
        self.deadline = deadline if deadline is not None else (
            CASE_DEADLINE_SOURCE() + 300
        )
        self.model = model if model is not None else FakeIsrModel()
        self.dead_rsp = dead_rsp
        self.closed = False
        self._rsp = None
        self._qtest = None

    def connect_rsp(self):
        if self._rsp is not None:
            return self._rsp
        if self.dead_rsp:
            transport = ScriptedTransport()  # never answers
        else:
            transport = FakeRspTransport(self.model)
        self._rsp = rsp.RspClient(
            DeadlineTransport(transport, self.deadline), default_timeout=5.0
        )
        return self._rsp

    def connect_qtest(self):
        if self._qtest is not None:
            return self._qtest
        self._qtest = qtest.QTestClient(
            DeadlineTransport(FakeQtestTransport(self.model), self.deadline),
            default_timeout=5.0,
        )
        return self._qtest

    def close(self):
        self.closed = True


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------


class SelfTestFailure(AssertionError):
    pass


def _check(condition, message):
    if not condition:
        raise SelfTestFailure(message)


class SelfTest:
    """The seven self-test categories frozen by the T10 card."""

    def __init__(self):
        self.checks = []
        for name in dir(self):
            if name.startswith("check_"):
                self.checks.append((name, getattr(self, name)))
        self.checks.sort()

    # -- 1. checksum --------------------------------------------------------

    def check_checksum_known_vectors(self):
        # q=113 S=83 u=117 p=112 p=112 o=111 r=114 t=116 e=101 d=100
        # sum = 1079 -> 0x37 mod 256
        _check(rsp.checksum(b"qSupported") == 0x37,
               "checksum('qSupported') != 0x37")
        _check(rsp.checksum_hex(b"qSupported") == "37",
               "checksum_hex('qSupported') != '37'")
        _check(rsp.checksum(b"") == 0, "empty checksum must be 0")
        _check(rsp.encode_packet(b"m0,1") == b"$m0,1#fa",
               "encode_packet('m0,1') != '$m0,1#fa'")

    def check_checksum_wraps_mod_256(self):
        data = b"\xff" * 2  # 510 mod 256 = 254
        _check(rsp.checksum(data) == 254, "checksum does not wrap mod 256")
        _check(rsp.checksum_hex(data) == "fe", "checksum_hex wrap wrong")

    def check_checksum_covers_escaped_bytes(self):
        # The checksum is computed over the escaped payload as transmitted.
        packet = rsp.encode_packet(b"a#b")
        _check(packet == b"$a}\x03b#43",
               "escaped payload not covered by checksum: %r" % packet)
        _check(rsp.parse_packet(packet) == b"a#b",
               "round-trip through escape+checksum failed")

    # -- 2. packet unpacking --------------------------------------------------

    def check_parse_packet_rejects_bad_framing(self):
        for bad, why in (
            (b"g#04", "no leading '$'"),
            (b"$g04", "missing '#'"),
            (b"$g#0", "short checksum"),
            (b"$g#0g", "non-hex checksum"),
        ):
            try:
                rsp.parse_packet(bad)
            except rsp.RspProtocolError:
                pass
            else:
                raise SelfTestFailure(
                    "parse_packet accepted %r (%s)" % (bad, why)
                )

    def check_parse_packet_rejects_checksum_mismatch(self):
        try:
            rsp.parse_packet(b"$g#68")  # correct csum for 'g' is 0x67
        except rsp.RspProtocolError as exc:
            _check("checksum" in str(exc),
                   "wrong error for checksum mismatch: %s" % exc)
        else:
            raise SelfTestFailure("parse_packet accepted a bad checksum")

    def check_client_unpacks_scripted_exchange(self):
        # Full client round trip: 'g' request, '+' ack, 46-byte register
        # reply, then a notification and a second packet on the same stream.
        values = self._fake_registers()
        reply = rsp.encode_registers(values)
        transport = ScriptedTransport()
        transport.expect(b"$g#", b"+" + rsp.encode_packet(reply))
        client = rsp.RspClient(transport)
        got = client.read_all_registers()
        _check(got == values, "decoded register set mismatch")
        # A stray notification followed by the real stop reply: the client
        # must skip '%...' and return the 'T' packet.
        transport.feed(
            b"%NOP#ed"
            + rsp.encode_packet(b"T05thread:01")
        )
        _check(client.wait_stop() == "T05thread:01",
               "notification not skipped before stop reply")

    def check_g_reply_real_hex_wire(self):
        # Independent literal oracle: QEMU gdb_memtohex sends 92 ASCII chars,
        # not 46 raw bytes. Do not use encode_registers for this fixture.
        payload = (b"000102030405060708090a0b0c0d0e0f"
                   b"101112131415161718191a1b1c1d1e1f"
                   b"38393a3b3c3d3e3f556600ff0210")
        packet = b"$" + payload + b"#" + ("%02x" % (sum(payload) & 255)).encode()
        transport = ScriptedTransport([(b"$g#67", b"+" + packet)])
        got = rsp.RspClient(transport).read_all_registers()
        expected = {"R%d" % n: n for n in range(32)}
        expected.update({"R%d" % n: n for n in range(56, 64)})
        expected.update(PSW=0x55, PSW1=0x66, PC=0x00FF0210)
        _check(got == expected, "real hex g packet decoded incorrectly")
        _check(rsp.encode_registers(expected) == payload,
               "encoder does not match QEMU ASCII-hex format")
        _check(transport.written == b"$g#67+", "g request/response ACK wrong")

    def check_register_hex_rejects_malformed_payloads(self):
        for bad in (b"00" * 45, b"00" * 47, b"00" * 45 + b"gg",
                    b"00" * 45 + b"  ", b"\x00" * 46):
            try:
                rsp.decode_registers(bad)
            except rsp.RspProtocolError:
                pass
            else:
                raise SelfTestFailure("accepted malformed g payload %r" % bad)

    def check_single_register_hex_wire(self):
        transport = ScriptedTransport([
            (b"$p2a#", b"+" + rsp.encode_packet(b"00ff0210"))])
        _check(rsp.RspClient(transport).read_register(42) == 0x00FF0210,
               "PC p packet must decode ASCII hex before big-endian parsing")
        _check(rsp.decode_single_register(b"ab", 1) == 0xAB,
               "single-byte p packet decoded wrong")

    def check_client_retransmits_on_nak(self):
        transport = ScriptedTransport()
        transport.feed(b"-+" + rsp.encode_packet(b"OK"))
        reply = rsp.RspClient(transport).command(b"qSupported")
        _check(reply == b"OK", "NAK resend lost response")
        _check(transport.written == b"$qSupported#37$qSupported#37+",
               "NAK must resend the exact packet once, then ACK the reply")
        exhausted = ScriptedTransport()
        exhausted.feed(b"----")
        try:
            rsp.RspClient(exhausted).command(b"qSupported")
        except rsp.RspProtocolError:
            pass
        else:
            raise SelfTestFailure("unbounded/accepted repeated NAK")
        _check(exhausted.written == b"$qSupported#37" * 4,
               "NAK retry limit must bound sends to four packets")

    def check_qtest_ok_fail_and_async_fragments(self):
        class FragmentedTransport(ScriptedTransport):
            def read(self, n, timeout):
                return super().read(min(n, 2), timeout)

        transport = FragmentedTransport()
        transport.expect(b"set_irq_in /machine/soc/cpu unnamed-gpio-in 1 0\n",
                         b"IRQ raise 1\nOK\n")
        client = qtest.QTestClient(transport)
        client.assert_irq_input_exists(QOM_CPU_PATH, 1)
        _check(client.async_lines() == ("IRQ raise 1",),
               "fragmented async IRQ line was not queued")
        _check(transport.written ==
               b"set_irq_in /machine/soc/cpu unnamed-gpio-in 1 0\n",
               "qtest IRQ probe wire command differs")
        transport.expect(b"readl 0x1000001\n", b"OK 0x1234\n")
        _check(client.read_memory(0x1000001, 4) == 0x1234,
               "qtest OK payload lost")
        for kind in ("FAIL", "ERR"):
            bad = ScriptedTransport([(b"probe\n", (kind + " Unknown device\n").encode())])
            try:
                qtest.QTestClient(bad).command("probe")
            except qtest.QTestCommandError as exc:
                _check(exc.command == "probe" and exc.reason == "Unknown device",
                       "qtest failure attribution lost")
            else:
                raise SelfTestFailure("qtest %s was accepted" % kind)

    def check_client_unescapes_payload(self):
        # '$', '#', '}' and '*' arriving escaped inside one payload must be
        # unescaped exactly once by the client.
        payload = b"x$\x7d\x7b#\x03*\x0by"  # literal $ } # * bytes (escaped)
        transport = ScriptedTransport()
        transport.expect(b"$q", b"+" + rsp.encode_packet(payload))
        client = rsp.RspClient(transport)
        got = client.command(b"qSupported")
        _check(got == payload, "escaped payload decoded wrong: %r" % got)

    def check_client_naks_and_raises_on_bad_checksum(self):
        bad_packet = b"$g#68"  # wrong checksum for 'g'
        transport = ScriptedTransport()
        transport.expect(b"$g#", b"+" + bad_packet)
        client = rsp.RspClient(transport)
        try:
            client.read_all_registers()
        except rsp.RspProtocolError as exc:
            _check("checksum" in str(exc), "unexpected error: %s" % exc)
        else:
            raise SelfTestFailure("bad checksum accepted by client")
        _check(b"-" in transport.written,
               "client did not NAK ('-') the bad packet")

    def check_rle_decoding(self):
        # '*'<ch> repeats the previous byte ord(ch)-29 times; register and
        # memory replies never use it, qXfer-style binary data may.
        data = b"ab" + b"*" + bytes([29 + 3]) + b"z"
        _check(rsp.apply_rle(data) == b"abbbbz", "run-length decode wrong")
        for bad in (b"*" + bytes([29 + 3]), b"a*", b"a*" + bytes([1])):
            try:
                rsp.apply_rle(bad)
            except rsp.RspProtocolError:
                pass
            else:
                raise SelfTestFailure("apply_rle accepted %r" % bad)

    # -- 3. timeout -----------------------------------------------------------

    def check_rsp_timeout_on_silent_peer(self):
        client = rsp.RspClient(ScriptedTransport(), default_timeout=0.05)
        started = time.monotonic()
        try:
            client.recv_packet()
        except rsp.RspTimeout:
            pass
        else:
            raise SelfTestFailure("silent peer did not raise RspTimeout")
        _check(time.monotonic() - started < 5.0,
               "timeout took unreasonably long")

    def check_rsp_timeout_on_missing_ack(self):
        transport = ScriptedTransport()
        # Rule fires and swallows the request, but answers no '+', so the
        # acknowledgement wait must time out.
        transport.expect(b"$g#", b"")
        client = rsp.RspClient(transport, default_timeout=0.05)
        try:
            client.command(b"g")
        except rsp.RspTimeout:
            pass
        else:
            raise SelfTestFailure("missing ack did not raise RspTimeout")

    def check_qtest_timeout_on_silent_peer(self):
        client = qtest.QTestClient(ScriptedTransport(), default_timeout=0.05)
        try:
            client.command("clock_step")
        except qtest.QTestTimeout:
            pass
        else:
            raise SelfTestFailure("silent qtest peer did not raise QTestTimeout")

    # -- 4. big-endian register decode -----------------------------------------

    def _fake_registers(self):
        """Hand-built register set with independent, recognizable values."""
        values = {}
        for index, name, width in rsp.GDB_REGISTERS:
            if name == "PC":
                values[name] = 0x00FF0210
            elif name == "PSW":
                values[name] = 0x55
            elif name == "PSW1":
                values[name] = 0x66
            else:
                values[name] = (index * 0x11) & 0xFF
        return values

    def check_g_reply_layout_and_endianness(self):
        values = self._fake_registers()
        wire_reply = rsp.encode_registers(values)
        _check(len(wire_reply) == 92, "g wire reply must have 92 hex characters")
        reply = bytes.fromhex(wire_reply.decode("ascii"))
        # 42 single-byte registers (indices 0..41) + 4-byte big-endian PC.
        _check(len(reply) == 46,
               "'g' reply must be 46 bytes (42x1 + PC32), got %d" % len(reply))
        # Independent byte-level proof of the frozen layout:
        _check(reply[40] == 0x55, "PSW must sit at offset 40")
        _check(reply[41] == 0x66, "PSW1 must sit at offset 41")
        _check(reply[42:46] == bytes([0x00, 0xFF, 0x02, 0x10]),
               "PC must be 4 bytes big-endian at offset 42: %r"
               % reply[42:46].hex())
        _check(reply[0] == values["R0"] and reply[31] == values["R31"],
               "R0/R31 bytes misplaced")
        decoded = rsp.decode_registers(wire_reply)
        _check(decoded == values, "decode_registers round-trip mismatch")
        _check(decoded["PC"] == 0x00FF0210, "PC decoded wrong")
        # A little-endian reading of the PC bytes must NOT be what we got.
        _check(decoded["PC"] != int.from_bytes(reply[42:46], "little"),
               "endianness check is vacuous")

    def check_g_reply_length_enforced(self):
        for bad_len in (45, 47):
            try:
                rsp.decode_registers(b"00" * bad_len)
            except rsp.RspProtocolError:
                pass
            else:
                raise SelfTestFailure(
                    "decode_registers accepted %d-byte reply" % bad_len
                )

    def check_register_names_frozen(self):
        names = [name for _, name, _ in rsp.GDB_REGISTERS]
        _check(names[:4] == ["R0", "R1", "R2", "R3"], "R0-R31 misnumbered")
        _check(names[31] == "R31" and names[32] == "R56",
               "R56-R63 must start at GDB index 32")
        _check(names[39] == "R63" and names[40] == "PSW"
               and names[41] == "PSW1" and names[42] == "PC",
               "PSW/PSW1/PC indices drifted")
        widths = {name: width for _, name, width in rsp.GDB_REGISTERS}
        _check(set(widths.values()) == {1, 4} and widths["PC"] == 4,
               "only the PC may be wider than one byte")

    def check_sfr_window_math(self):
        _check(rsp.sfr_phys_addr(0x80) == 0x01000000, "window base wrong")
        _check(rsp.sfr_phys_addr(0x81) == 0x01000001, "SP window wrong")
        _check(rsp.sfr_phys_addr(0x85) == 0x01000005, "SPH window wrong")
        try:
            rsp.sfr_phys_addr(0x7F)
        except rsp.RspProtocolError:
            pass
        else:
            raise SelfTestFailure("SFR window accepted address below 0x80")

    def check_spx_read_via_window(self):
        # SPX = SPH:SP through the SFR window; script both memory reads and
        # verify the exact request bytes the client put on the wire.
        transport = ScriptedTransport()
        transport.expect(b"$m1000005,1#",
                         b"+" + rsp.encode_packet(b"02"))
        transport.expect(b"$m1000001,1#",
                         b"+" + rsp.encode_packet(b"c8"))
        client = rsp.RspClient(transport)
        _check(client.read_spx() == 0x02C8,
               "SPX composition from SPH:SP wrong")
        _check(b"$m1000005,1#" in transport.written
               and b"$m1000001,1#" in transport.written,
               "SFR window requests not seen on the wire")

    # -- 5. state comparison ---------------------------------------------------

    def _boundary_snapshot(self, spx, psw=0x10, pc=0x00FF0400):
        values = {name: 0 for _, name, _ in rsp.GDB_REGISTERS}
        values.update({"PSW": psw, "PSW1": 0x00, "PC": pc, "SPX": spx})
        for index in range(32):
            values["R%d" % index] = 0x20 + index
        for index in range(8):
            values["R%d" % (56 + index)] = 0xA0 + index
        return values

    def check_identical_snapshots_compare_clean(self):
        before = self._boundary_snapshot(spx=0x0400)
        mismatches, not_compared = compare_snapshots(before, dict(before))
        _check(mismatches == [], "identical snapshots reported mismatches")
        _check(not_compared == [], "nothing should be missing here")

    def check_psw_difference_is_detected(self):
        before = self._boundary_snapshot(spx=0x0400)
        after = dict(before)
        after["PSW"] = 0x11
        mismatches, _ = compare_snapshots(before, after)
        _check(len(mismatches) == 1 and mismatches[0]["field"] == "PSW",
               "PSW corruption not pinpointed: %r" % mismatches)
        result = classify_result(assertion_failures=mismatches)
        _check(result == RESULT_FAIL,
               "state mismatch must classify as FAIL, got %s" % result)

    def test_pc_difference_is_detected(self):
        before = self._boundary_snapshot(spx=0x0400)
        after = dict(before)
        after["PC"] = 0x00FF0404
        mismatches, _ = compare_snapshots(before, after)
        _check(any(m["field"] == "PC" for m in mismatches),
               "PC change after RETI not detected")

    # Guard: the above helper is intentionally private-but-run; keep a check
    # wrapper so the runner executes it too.
    def check_pc_difference_is_detected(self):
        self.test_pc_difference_is_detected()

    def check_unread_field_is_reported_not_fabricated(self):
        before = self._boundary_snapshot(spx=0x0400)
        after = dict(before)
        del after["SPX"]  # e.g. SFR window read failed in this run
        mismatches, not_compared = compare_snapshots(before, after)
        _check(mismatches == [], "missing field must not invent a mismatch")
        _check(not_compared == ["SPX"],
               "missing field must be reported, got %r" % not_compared)

    def check_full_field_set_is_compared(self):
        before = self._boundary_snapshot(spx=0x0400)
        after = dict(before)
        after["R7"] ^= 0xFF
        after["R63"] ^= 0xFF
        mismatches, not_compared = compare_snapshots(before, after)
        fields_hit = {m["field"] for m in mismatches}
        _check(fields_hit == {"R7", "R63"},
               "low and high bank must both be compared: %r" % fields_hit)
        _check(not_compared == [], "unexpected missing fields")

    # -- 6. frame ledger ---------------------------------------------------------

    def check_frame_constants(self):
        _check(FRAME_SOFTWARE_SAVE_BYTES == 1 + 9 * 4,
               "37B software save != 1 + 9*4")
        _check(FRAME_LAYER_BYTES == FRAME_HARDWARE_BYTES + FRAME_SOFTWARE_SAVE_BYTES,
               "41B layer != 4 + 37")
        _check(SAVE_SEQUENCE == ("PSW", "DR0", "DR4", "DR8", "DR12", "DR16",
                                 "DR20", "DR24", "DR28", "DPX"),
               "A6 save order drifted")
        _check(rsp.GDB_REG_REPLY_SIZE == 46, "GDB reply size drifted")

    def check_canonical_ledger_accepts_correct_sequence(self):
        s, f = 0x0400, 12
        observations = [
            ("accept_pending", 0x400), ("hardware_frame", 0x404),
            ("software_saved", 0x429), ("local_frame", 0x435),
            ("local_teardown", 0x429), ("state_restored", 0x404),
            ("reti_done", 0x400),
        ]
        mismatches = verify_spx_ledger(observations,
                                       expected_spx_points(s, local_bytes=f))
        _check(mismatches == [], "correct A6 ledger reported %r" % mismatches)

    def check_ledger_point_values(self):
        s = 0x0400
        points = dict(expected_spx_points(s, local_bytes=0))
        _check(points["hardware_frame"] == s + 4, "hardware point != S+4")
        _check(points["software_saved"] == s + 41, "save point != S+41")
        _check(points["reti_done"] == s, "RETI point != S")

    def check_ledger_detects_corrupt_save_point(self):
        s = 0x0400
        observations = [(phase, value) for phase, value
                        in expected_spx_points(s, local_bytes=0)]
        observations = [
            (phase, s + 40 if phase == "software_saved" else value)
            for phase, value in observations
        ]
        mismatches = verify_spx_ledger(observations,
                                       expected_spx_points(s, local_bytes=0))
        _check(len(mismatches) == 1
               and mismatches[0]["phase"] == "software_saved"
               and mismatches[0]["kind"] == "spx-mismatch"
               and mismatches[0]["expected"] == s + 41
               and mismatches[0]["actual"] == s + 40,
               "corrupt save point not pinpointed: %r" % mismatches)

    def check_ledger_detects_missing_and_extra_phases(self):
        s = 0x0400
        points = expected_spx_points(s, local_bytes=0)
        observations = [p for p in points if p[0] != "hardware_frame"]
        mismatches = verify_spx_ledger(observations, points)
        _check(any(m["kind"] == "missing-phase"
                   and m["phase"] == "hardware_frame" for m in mismatches),
               "missing hardware phase not reported: %r" % mismatches)
        observations = list(points) + [("not-a-phase", s)]
        mismatches = verify_spx_ledger(observations, points)
        _check(any(m["kind"] == "unknown-phase" for m in mismatches),
               "extra phase not reported: %r" % mismatches)
        mismatches = verify_spx_ledger(list(points) + [("reti_done", s)], points)
        _check(mismatches == [{"phase": "reti_done", "kind": "out-of-order",
                               "actual": s}],
               "repeat after ledger exhaustion must be out-of-order: %r"
               % mismatches)

    def check_ledger_detects_out_of_order(self):
        # A phase observed after its only expected position was already
        # consumed must produce the documented out-of-order kind.
        s = 0x0400
        points = list(expected_spx_points(s, local_bytes=0))
        reordered = [points[0], points[2], points[1]] + points[3:]
        mismatches = verify_spx_ledger(reordered, points)
        _check(any(m["kind"] == "out-of-order"
                   and m["phase"] == "hardware_frame" for m in mismatches),
               "out-of-order ledger kind not produced: %r" % mismatches)
        _check(any(m["kind"] == "missing-phase"
                   and m["phase"] == "hardware_frame" for m in mismatches),
               "the skipped position must also be reported: %r" % mismatches)
        # The in-order prefix and suffix must still be clean.
        _check(not any(m["kind"] in ("spx-mismatch", "unknown-phase")
                       for m in mismatches),
               "out-of-order run invented other kinds: %r" % mismatches)

    def check_save_window_granular_ledger(self):
        # Fake step-by-step observation of an empty ISR: hardware frame,
        # PSW push (+1), nine 4-byte pushes, nine 4-byte pops, PSW pop,
        # RETI. Verifies the granular window ledger and its symmetry.
        s = 0x0500
        points = expected_save_window_points(s)
        observations = [
            ("hardware_frame", 0x504), ("psw_saved", 0x505),
            ("dr0_saved", 0x509), ("dr4_saved", 0x50D),
            ("dr8_saved", 0x511), ("dr12_saved", 0x515),
            ("dr16_saved", 0x519), ("dr20_saved", 0x51D),
            ("dr24_saved", 0x521), ("dr28_saved", 0x525),
            ("dpx_saved", 0x529), ("dpx_restored", 0x525),
            ("dr28_restored", 0x521), ("dr24_restored", 0x51D),
            ("dr20_restored", 0x519), ("dr16_restored", 0x515),
            ("dr12_restored", 0x511), ("dr8_restored", 0x50D),
            ("dr4_restored", 0x509), ("dr0_restored", 0x505),
            ("psw_restored", 0x504),
        ]
        _check(tuple(observations) == points, "A6 granular point values drifted")
        mismatches = verify_spx_ledger(observations, points)
        _check(mismatches == [], "granular window reported %r" % mismatches)
        # Corrupt one DR restore (simulate a missed DR20 restore byte).
        corrupted = []
        for phase, value in observations:
            if phase == "dr20_restored":
                value -= 1
            corrupted.append((phase, value))
        mismatches = verify_spx_ledger(corrupted, points)
        _check(any(m["phase"] == "dr20_restored" for m in mismatches),
               "corrupt DR20 restore not detected: %r" % mismatches)
        # The sequence must end back at the hardware-frame level (S+4).
        _check(points[-1][1] == s + 4,
               "restore window must end at S+4, got %r" % (points[-1],))

    def check_nested_layers_link_ledgers(self):
        # Main -> low -> high -> low -> main. The high ISR's accept_pending
        # SPX is exactly the low ISR's SPX at the takeover point: low's
        # software_saved (+ its local frame if one is active).
        s_low, f_low, s_high = 0x0400, 0, 0x0400 + 41
        low = expected_spx_points(s_low, local_bytes=f_low)
        high = expected_spx_points(s_high, local_bytes=0)
        _check(dict(low)["software_saved"] == dict(high)["accept_pending"],
               "high layer must anchor at low layer's S+41")
        # With a low-layer local frame active at takeover the anchor shifts.
        low_f = expected_spx_points(s_low, local_bytes=8)
        high2 = expected_spx_points(0x431, local_bytes=0)
        _check(dict(low_f)["local_frame"] == 0x431,
               "low local frame must end at 0x400+41+8")
        _check(high2 == (
            ("accept_pending", 0x431), ("hardware_frame", 0x435),
            ("software_saved", 0x45A), ("local_frame", 0x45A),
            ("local_teardown", 0x45A), ("state_restored", 0x435),
            ("reti_done", 0x431)), "high ledger must balance to low's 0x431")
        _check(high == (
            ("accept_pending", 0x429), ("hardware_frame", 0x42D),
            ("software_saved", 0x452), ("local_frame", 0x452),
            ("local_teardown", 0x452), ("state_restored", 0x42D),
            ("reti_done", 0x429)), "high ledger must balance to low's 0x429")
        # Each layer individually balances back to its own base.
        for layer_points in (low, high, low_f, high2):
            as_dict = dict(layer_points)
            base = as_dict["accept_pending"]
            _check(as_dict["reti_done"] == base,
                   "layer with S=0x%04x does not balance to its base" % base)

    # -- 7. result classification -------------------------------------------------

    def check_classification_paths(self):
        _check(classify_result() == RESULT_PASS, "clean run must be PASS")
        _check(classify_result(assertion_failures=[{"field": "PSW"}])
               == RESULT_FAIL, "assertion failure must be FAIL")
        _check(classify_result(model_unsupported=True)
               == RESULT_MODEL_UNSUPPORTED,
               "capability limit must be MODEL_UNSUPPORTED")
        _check(classify_result(baseline_error="sha mismatch")
               == RESULT_BASELINE_MISMATCH,
               "identity failure must be BASELINE_MISMATCH")

    def check_timeout_is_never_pass(self):
        result = classify_result(timeout=True)
        _check(result == RESULT_TIMEOUT, "timeout must be TIMEOUT")
        _check(result != RESULT_PASS, "timeout classified as PASS")
        # Timeout keeps priority even over other findings.
        _check(classify_result(timeout=True, model_unsupported=True,
                               assertion_failures=[{"field": "PSW"}])
               == RESULT_TIMEOUT, "timeout must win classification")
        # Board booking has no TIMEOUT state: it books as FAIL.
        _check(board_state_for(RESULT_TIMEOUT) == BOARD_STATE_FAIL,
               "timeout must book as board FAIL")
        _check(board_state_for(RESULT_PASS) == BOARD_STATE_PASS,
               "PASS must book as board PASS")

    def check_board_states_frozen(self):
        _check(BOARD_STATES == ("PASS", "FAIL", "NOT_RUN",
                                "MODEL_UNSUPPORTED", "BASELINE_MISMATCH",
                                "BLOCKED_NO_BOARD"),
               "six-state board enum drifted")
        _check(board_state_for(RESULT_FAIL, board_provided=False)
               == BOARD_STATE_BLOCKED_NO_BOARD,
               "missing board must book BLOCKED_NO_BOARD")
        _check(board_state_for(RESULT_PASS, attempted=False)
               == BOARD_STATE_NOT_RUN,
               "unattempted item must book NOT_RUN")
        _check(board_state_for(RESULT_MODEL_UNSUPPORTED)
               == BOARD_STATE_MODEL_UNSUPPORTED,
               "MODEL_UNSUPPORTED must survive booking")
        _check(board_state_for(RESULT_BASELINE_MISMATCH)
               == BOARD_STATE_BASELINE_MISMATCH,
               "BASELINE_MISMATCH must survive booking")

    def check_manifest_validation(self):
        good = self._manifest_doc()
        validate_manifest(good, require_runtime_ready=True)

        def bad(mutator, why):
            doc = self._manifest_doc()
            mutator(doc)
            try:
                validate_manifest(doc, require_runtime_ready=True)
            except ManifestError:
                pass
            else:
                raise SelfTestFailure("manifest accepted despite %s" % why)

        bad(lambda d: d.update(schema=2), "wrong schema")
        bad(lambda d: d.update(schema=True), "bool schema (True == 1 in "
            "Python, must be rejected by type)")
        bad(lambda d: d.pop("timeout_seconds"), "missing key")
        bad(lambda d: d.update(machine="other-machine"), "wrong machine")
        bad(lambda d: d["qemu"].update(sha256="nothex"), "bad sha256")
        bad(lambda d: d["image"].update(path="relative/path.elf"),
            "relative image path")
        bad(lambda d: d.update(map="C:relative.map"), "relative map path")
        bad(lambda d: d.update(gdb_port="1234"), "non-integer port")
        bad(lambda d: d.update(timeout_seconds=0), "zero timeout")

        # gdb_port=0 is only acceptable while generating the manifest.
        doc = self._manifest_doc()
        doc["gdb_port"] = 0
        validate_manifest(doc, require_runtime_ready=False)
        try:
            validate_manifest(doc, require_runtime_ready=True)
        except ManifestError:
            pass
        else:
            raise SelfTestFailure("gdb_port=0 accepted for a run")

    def check_sha256_identity_verification(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            payload = b"MCS251 qualification baseline identity\n"
            path = os.path.join(tmp, "image.hex")
            with open(path, "wb") as handle:
                handle.write(payload)
            good = hashlib.sha256(payload).hexdigest()
            _check(verify_identity(path, good) is None,
                   "matching sha256 reported as mismatch")
            err = verify_identity(path, "0" * 64)
            _check(err is not None and "sha256 mismatch" in err,
                   "wrong sha256 not detected: %r" % err)
            err = verify_identity(os.path.join(tmp, "absent.hex"), good)
            _check(err is not None and "missing file" in err,
                   "missing file not detected: %r" % err)
            result = classify_result(baseline_error=err)
            _check(result == RESULT_BASELINE_MISMATCH,
                   "identity failure must classify BASELINE_MISMATCH")

    def check_map_parsing(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "fw.map")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(
                    "other line\n"
                    "MCS251 map\n"
                    "crt-irq.o:.mcs251.HOME 0xff0000 +0x3\n"
                    "crt-irq.o:.mcs251.BOOT 0xff0500 +0x106\n"
                    "fw.o:.mcs251.CSEG 0xff0700 +0x100\n"
                    "IRQ 00 0xff0003 ISR _irq0\n"
                    "IRQ 01 0xff000b DEFAULT __mcs251_isr_unhandled\n"
                    "IRQ 07 0xff003b RESERVED\n"
                    "IRQ 14 0xff0073 SYSTEM\n"
                    "IRQ 100 0xff0323 RESERVED\n"
                    "IRQ 102 0xff0333 ISR _irq102\n"
                    "IRQ 126 0xff03f3 DEFAULT __mcs251_isr_unhandled\n"
                )
            table = parse_irq_map(path)
            _check(table[0] == {"addr": 0xFF0003, "tag": "ISR",
                                "symbol": "_irq0"},
                   "ISR map line parsed wrong: %r" % table.get(0))
            _check(table[1]["tag"] == "DEFAULT"
                   and table[1]["symbol"] == "__mcs251_isr_unhandled",
                   "DEFAULT map line parsed wrong")
            _check(table[7]["symbol"] is None, "RESERVED must have no symbol")
            _check(table[14]["tag"] == "SYSTEM" and table[14]["symbol"] is None,
                   "SYSTEM row parsed wrong: %r" % table.get(14))
            # G1: the slot field spans one to three digits. A two-digit-only
            # pattern silently dropped every 100..126 row; these three cover
            # the high Reserved row, an ISR row and the profile maximum.
            _check(table[100] == {"addr": 0xFF0323, "tag": "RESERVED",
                                  "symbol": None},
                   "three-digit RESERVED row 100 parsed wrong: %r"
                   % table.get(100))
            _check(table[102] == {"addr": 0xFF0333, "tag": "ISR",
                                  "symbol": "_irq102"},
                   "three-digit ISR row 102 parsed wrong: %r"
                   % table.get(102))
            _check(table[126] == {"addr": 0xFF03F3, "tag": "DEFAULT",
                                  "symbol": "__mcs251_isr_unhandled"},
                   "profile-max row 126 parsed wrong: %r" % table.get(126))
            _check(len(table) == 7,
                   "all seven rows (including the three 100+ ones) must be "
                   "parsed, got %d: %r" % (len(table), sorted(table)))
            # The map's addr column is the *vector slot address*; the
            # DEFAULT function address is resolved at runtime through the
            # vector EJMP targets, never from this column.
            _check(default_slots(table) == [1, 126],
                   "DEFAULT slot classification wrong: %r"
                   % default_slots(table))
            _check(table[1]["addr"] == vector_addr(1)
                   and table[126]["addr"] == vector_addr(126),
                   "DEFAULT map addr must be the vector slot address")
            sections = parse_map_sections(path)
            _check(sections == [
                {"path": "crt-irq.o", "name": ".mcs251.HOME",
                 "addr": 0xFF0000, "size": 0x3},
                {"path": "crt-irq.o", "name": ".mcs251.BOOT",
                 "addr": 0xFF0500, "size": 0x106},
                {"path": "fw.o", "name": ".mcs251.CSEG",
                 "addr": 0xFF0700, "size": 0x100},
            ], "map section lines parsed wrong: %r" % sections)
            protected = protected_crt_ranges(sections)
            _check(protected == [(0xFF0000, 0xFF0003),
                                 (0xFF0500, 0xFF0606)],
                   "protected CRT ranges wrong: %r" % protected)
            _check(_addr_in_ranges(0xFF0500, protected)
                   and not _addr_in_ranges(0xFF0700, protected),
                   "boundary exclusion ranges misjudge CRT vs CSEG")

    def check_map_parsing_rejects_malformed_rows(self):
        """A bad IRQ row stops the case instead of shrinking the table.

        G1 regression: the old two-digit slot field dropped every 100..126
        row silently, so a malformed or truncated map could still look like
        a valid short table. Each case below must raise, and each must be the
        *only* defect in an otherwise valid map so the raise can only come
        from the intended check.
        """
        import tempfile

        def parse_text(text):
            with tempfile.TemporaryDirectory() as tmp:
                path = os.path.join(tmp, "fw.map")
                with open(path, "w", encoding="utf-8") as handle:
                    handle.write(text)
                return parse_irq_map(path)

        head = ("MCS251 map\n"
                "crt-irq.o:.mcs251.HOME 0xff0000 +0x3\n"
                "crt-irq.o:.mcs251.BOOT 0xff0500 +0x106\n"
                "IRQ 01 0xff000b ISR _irq1\n")

        # Baseline: this exact head parses, so every failure below is caused
        # by the one mutated row.
        _check(parse_text(head + "IRQ 02 0xff0013 ISR _irq2\n")[2]["tag"]
               == "ISR", "baseline map must parse")

        cases = [
            ("IRQ 127 0xff03fb ISR _irq127\n", "slot outside the profile"),
            ("IRQ 999 0xff1f3b ISR _irq999\n", "four-digit slot"),
            ("IRQ 02 0xff0014 ISR _irq2\n", "address off the frozen formula"),
            ("IRQ 02 0xff0013 WRONG _irq2\n", "unknown tag"),
            ("IRQ 02 0xff0013 ISR\n", "ISR tag without a symbol"),
            ("IRQ 09 0xff004b DEFAULT\n", "DEFAULT tag without a symbol"),
            ("IRQ 07 0xff003b RESERVED _oops\n",
             "reserved row with a symbol"),
            ("IRQ 02 0xff0013\n", "missing tag"),
            ("IRQ 02 0XFF0013 ISR _irq2\n", "upper-case 0X prefix"),
            ("IRQ 1 0xff000b ISR _irq1\n", "duplicate slot 1"),
            ("IRQ 07 0xff003b ISR _irq7\n",
             "Reserved slot relabelled as a handler"),
            ("IRQ 14 0xff0073 RESERVED\n",
             "System slot relabelled as Reserved"),
            ("IRQ 02 0xff0013 RESERVED\n",
             "legal slot relabelled as Reserved"),
        ]
        for row, what in cases:
            raised = None
            try:
                parse_text(head + row)
            except CaseEnvironmentError as exc:
                raised = str(exc)
            _check(raised is not None,
                   "malformed IRQ row must raise (%s): %r" % (what, row))
            _check("fw.map" in raised and "IRQ" in raised,
                   "malformed-row error must name the map and the row "
                   "(%s): %r" % (what, raised))

        # Non-IRQ lines stay ignored; only a line whose first token is IRQ is
        # held to the frozen shape.
        _check(parse_text("MCS251 map\n"
                          "IRQ_TABLE = 0x03f8\n"
                          "IRQ 01 0xff000b ISR _irq1\n")[1]["symbol"]
               == "_irq1", "non-IRQ map lines must be ignored")

    @staticmethod
    def _manifest_doc():
        return {
            "schema": 1,
            "qemu": {"path": "/opt/qemu/bin/qemu-system-mcs251",
                     "sha256": "a" * 64},
            "tools": {},
            "machine": MACHINE,
            "image": {"path": "/opt/qual/fw.hex", "sha256": "b" * 64},
            "map": "/opt/qual/fw.map",
            "output_dir": "/opt/qual/out",
            "gdb_port": 1234,
            "qtest_socket": "/tmp/qual-qtest.sock",
            "timeout_seconds": 30,
        }

    # -- 8. deadline enforcement (absolute, per-read) -----------------------

    def check_deadline_transport_derives_read_budgets(self):
        class Recorder:
            def __init__(self):
                self.budgets = []

            def write(self, data):
                pass

            def read(self, n, timeout):
                self.budgets.append(timeout)
                return b"ok"

            def close(self):
                pass

        inner = Recorder()
        transport = DeadlineTransport(inner, CASE_DEADLINE_SOURCE() + 0.5)
        transport.read(1, 30.0)
        transport.read(1, 30.0)
        _check(len(inner.budgets) == 2, "both reads must reach the inner "
                                        "transport")
        _check(all(budget <= 0.5 for budget in inner.budgets),
               "read budget not derived from the absolute deadline: %r"
               % inner.budgets)
        _check(inner.budgets[1] < inner.budgets[0],
               "budget must shrink with elapsed time (slow-drip defense)")

    def check_deadline_transport_expires_without_inner_read(self):
        class MustNotRead:
            def read(self, n, timeout):
                raise AssertionError("inner read after deadline expiry")

            def write(self, data):
                pass

            def close(self):
                pass

        transport = DeadlineTransport(MustNotRead(),
                                      CASE_DEADLINE_SOURCE() - 0.01)
        try:
            transport.read(1, 30.0)
        except TimeoutError:
            pass
        else:
            raise SelfTestFailure("expired deadline did not stop the read")

    def check_rsp_timeout_over_deadline_transport(self):
        client = rsp.RspClient(
            DeadlineTransport(ScriptedTransport(),
                              CASE_DEADLINE_SOURCE() - 0.01)
        )
        try:
            client.read_all_registers()
        except rsp.RspTimeout:
            pass
        else:
            raise SelfTestFailure("deadline breach did not raise RspTimeout")

    # -- 9. single-step IRQ negotiation --------------------------------------

    def check_sstep_negotiation_wire_and_readback(self):
        # Protocol expectations are pinned by independent literals (R04: the
        # self-test once repeated the production typo and masked it). The
        # read query is the registered packet name "qemu.sstep" under the
        # 'q' dispatcher prefix — "qqemu.sstep" on the wire, no space.
        transport = ScriptedTransport()
        transport.expect(
            b"$Qqemu.sstep=1#",
            b"+" + rsp.encode_packet(b"OK"))
        transport.expect(
            b"$qqemu.sstep#",
            b"+" + rsp.encode_packet(b"0x1"))
        mask = negotiate_sstep_irq(rsp.RspClient(transport))
        _check(mask == SSTEP_IRQ_NEGOTIATION_MASK,
               "negotiation returned the wrong mask")
        _check(b"$Qqemu.sstep=1#" in transport.written,
               "negotiation must clear NOIRQ/NOTIMER (mask = SSTEP_ENABLE "
               "only), wire: %r" % transport.written)
        _check(b"$qqemu.sstep#" in transport.written,
               "negotiation must read the mask back as proof")
        _check(b"$qR qemu.sstep#" not in transport.written,
               "negotiation must not send the invalid 'qR qemu.sstep' form "
               "(unknown packet for every compliant stub), wire: %r"
               % transport.written)

    def check_sstep_negotiation_rejects_e22(self):
        transport = ScriptedTransport()
        transport.expect(
            b"$Qqemu.sstep=1#",
            b"+" + rsp.encode_packet(b"E22"))
        try:
            negotiate_sstep_irq(rsp.RspClient(transport))
        except CaseEnvironmentError:
            pass
        else:
            raise SelfTestFailure("E22 negotiation reply was accepted")

    def check_sstep_negotiation_detects_non_sticky_mask(self):
        # A stub that answers OK but does not apply the mask is exposed by
        # the readback; without it every case would silently never accept.
        transport = ScriptedTransport()
        transport.expect(
            b"$Qqemu.sstep=1#",
            b"+" + rsp.encode_packet(b"OK"))
        transport.expect(
            b"$qqemu.sstep#",
            b"+" + rsp.encode_packet(b"0x7"))
        try:
            negotiate_sstep_irq(rsp.RspClient(transport))
        except CaseEnvironmentError as exc:
            _check("did not stick" in str(exc),
                   "wrong error for a non-sticky mask: %s" % exc)
        else:
            raise SelfTestFailure("non-sticky sstep mask was accepted")

    def check_sstep_negotiation_rejects_empty_readback(self):
        # A compliant stub answers an unknown packet with an empty reply; a
        # harness that sent a wrong query would read exactly this and must
        # stop, never treat the silence as success.
        transport = ScriptedTransport()
        transport.expect(
            b"$Qqemu.sstep=1#",
            b"+" + rsp.encode_packet(b"OK"))
        transport.expect(
            b"$qqemu.sstep#",
            b"+" + rsp.encode_packet(b""))
        try:
            negotiate_sstep_irq(rsp.RspClient(transport))
        except CaseEnvironmentError as exc:
            _check("did not stick" in str(exc),
                   "wrong error for an empty readback: %s" % exc)
        else:
            raise SelfTestFailure("empty sstep readback was accepted")

    def check_sstep_negotiation_rejects_malformed_readback(self):
        for bad in (b"1", b"0x", b"0X1"):
            transport = ScriptedTransport()
            transport.expect(
                b"$Qqemu.sstep=1#",
                b"+" + rsp.encode_packet(b"OK"))
            transport.expect(
                b"$qqemu.sstep#",
                b"+" + rsp.encode_packet(bad))
            try:
                negotiate_sstep_irq(rsp.RspClient(transport))
            except CaseEnvironmentError as exc:
                _check("did not stick" in str(exc),
                       "wrong error for malformed readback %r: %s"
                       % (bad, exc))
            else:
                raise SelfTestFailure(
                    "malformed sstep readback %r was accepted" % bad)

    def check_sstep_negotiation_deadline_is_enforced(self):
        transport = DeadlineTransport(ScriptedTransport(),
                                      CASE_DEADLINE_SOURCE() - 0.01)
        try:
            negotiate_sstep_irq(rsp.RspClient(transport, default_timeout=5.0))
        except rsp.RspTimeout:
            pass
        else:
            raise SelfTestFailure(
                "expired deadline did not stop the negotiation")

    def check_sstep_query_old_form_is_unknown_packet(self):
        # R04 negative proof: the fake model must behave like a real stub —
        # the historical "qR qemu.sstep" form is an unknown packet (empty
        # reply) while the legal forms are answered. Literals are pinned
        # here independently of the production code.
        client = rsp.RspClient(FakeRspTransport(FakeIsrModel()),
                               default_timeout=5.0)
        _check(client.command(b"qqemu.sstep") == b"0x7",
               "legal sstep query must return the factory mask 0x7")
        _check(client.command(b"qR qemu.sstep") == b"",
               "the invalid 'qR qemu.sstep' form must be answered as an "
               "unknown packet (empty reply), like every compliant stub")
        _check(client.command(b"Qqemu.sstep=1") == b"OK",
               "legal sstep write must be acknowledged")
        _check(client.command(b"qqemu.sstep") == b"0x1",
               "post-write sstep query must read back 0x1")

    def check_negotiation_failure_precedes_qtest_probe(self):
        # Q-01: the negotiation check must fail before any qtest IRQ probe
        # or injection can happen (connect_and_probe ordering).
        events = []

        class OrderingSession:
            def __init__(self):
                self.deadline = CASE_DEADLINE_SOURCE() + 30

            def connect_rsp(self):
                events.append("rsp-connect")
                transport = ScriptedTransport()
                transport.expect(
                    b"$Qqemu.sstep=1#",
                    b"+" + rsp.encode_packet(b"OK"))
                transport.expect(
                    b"$qqemu.sstep#",
                    b"+" + rsp.encode_packet(b""))
                return rsp.RspClient(transport, default_timeout=5.0)

            def connect_qtest(self):
                events.append("qtest-connect")

                class NoTraffic:
                    def write(self, data):
                        events.append("qtest-write")

                    def read(self, n, timeout):
                        raise TimeoutError("no qtest traffic expected")

                    def close(self):
                        pass

                return qtest.QTestClient(NoTraffic(), default_timeout=1.0)

            def close(self):
                pass

        try:
            connect_and_probe(OrderingSession(), 1)
        except CaseEnvironmentError as exc:
            _check("did not stick" in str(exc),
                   "wrong error when the negotiation fails: %s" % exc)
        else:
            raise SelfTestFailure("failing negotiation did not stop probing")
        _check(events == ["rsp-connect"],
               "negotiation failure must precede any qtest traffic: %r"
               % events)

    def check_observe_requires_negotiation_for_acceptance(self):
        # With the factory NOIRQ mask still in force the injected interrupt
        # is never taken on the first single-step: the timeline classifies
        # this as a deferred acceptance — an explicit fixture prerequisite
        # mismatch, never a PASS and never a generic assertion failure.
        model = FakeIsrModel()
        rsp_client, qtest_client = self._fake_clients(model)
        deadline = CASE_DEADLINE_SOURCE() + 60
        boundary, _ = walk_to_main_loop(rsp_client, deadline,
                                        self._protected())
        before = take_snapshot(rsp_client, timeout=_remaining(deadline))
        try:
            observe_one_injection(rsp_client, qtest_client, 1,
                                  before, deadline)
        except CaseEnvironmentError as exc:
            _check("deferred acceptance" in str(exc),
                   "un-negotiated stepping must book deferred acceptance: "
                   "%s" % exc)
        else:
            raise SelfTestFailure("un-negotiated stepping did not stop the "
                                  "observation")

    # -- 10. hardware frame content and boundary walk ------------------------

    def check_hardware_frame_bytes_oracle(self):
        # A6 profile 1 push order: PSW1, PC[23:16], PC[7:0], PC[15:8].
        # PC 0x00FF0210 is a 24-bit address: PC[23:16]=0xFF, PC[15:8]=0x02,
        # PC[7:0]=0x10.
        frame = hardware_frame_bytes(0x20, 0x00FF0210)
        _check(frame == bytes([0x20, 0xFF, 0x10, 0x02]),
               "frame byte order drifted: %s" % frame.hex())
        _check(hardware_frame_bytes(0, 0x000001) == bytes([0, 0, 1, 0]),
               "frame byte order drifted (low PC)")

    def check_walk_to_main_loop_finds_real_boundary(self):
        model = FakeIsrModel()
        rsp_client, _ = self._fake_clients(model)
        deadline = CASE_DEADLINE_SOURCE() + 60
        rsp_client.command(b"Qqemu.sstep=1")
        boundary, steps = walk_to_main_loop(rsp_client, deadline,
                                            self._protected())
        _check(boundary["PC"] in FakeIsrModel.MAIN_LOOP,
               "boundary must sit in the application loop, PC=0x%06x"
               % boundary["PC"])
        _check(boundary["SPX"] == 0x0400, "boundary SPX must be stable")
        _check(steps >= MAIN_LOOP_MIN_STEPS,
               "boundary declared before the CRT could have run")
        # The boundary is a real instruction edge: stepping again keeps
        # alternating the main loop and never wanders into the CRT.
        rsp_client.step(timeout=_remaining(deadline))
        after = take_snapshot(rsp_client, timeout=_remaining(deadline))
        _check(after["PC"] in FakeIsrModel.MAIN_LOOP,
               "boundary is not inside a stable loop")

    def check_walk_to_main_loop_rejects_crt_only_image(self):
        model = FakeIsrModel(faults=("no-main-loop",))
        rsp_client, _ = self._fake_clients(model)
        deadline = CASE_DEADLINE_SOURCE() + 60
        rsp_client.command(b"Qqemu.sstep=1")
        try:
            walk_to_main_loop(rsp_client, deadline, self._protected(),
                              budget=500)
        except CaseEnvironmentError as exc:
            _check("main-loop" in str(exc),
                   "wrong prereq error for a CRT-only image: %s" % exc)
        else:
            raise SelfTestFailure("CRT-only image produced a boundary")

    # -- 11. full layer observation against the fake model -------------------

    def check_observe_single_layer_happy_path(self):
        model = FakeIsrModel()
        rsp_client, qtest_client = self._fake_clients(model)
        deadline = CASE_DEADLINE_SOURCE() + 120
        negotiate_sstep_irq(rsp_client, deadline=deadline)
        boundary, _ = walk_to_main_loop(rsp_client, deadline,
                                        self._protected())
        failures, evidence = observe_one_injection(
            rsp_client, qtest_client, 1, boundary, deadline,
            collect_windows=True,
        )
        _check(failures == [], "correct ISR reported %r" % failures)
        _check(evidence["hardware_frame"]["match"] is True,
               "frame content evidence missing")
        _check(evidence["local_bytes"] == 8,
               "observed local frame must be 8, got %r"
               % evidence["local_bytes"])
        _check(evidence["body_register_writes"] == ["R5", "R60"],
               "body sentinel writes not observed: %r"
               % evidence["body_register_writes"])
        windows = evidence["windows"]
        names = [w.get("window") for w in windows]
        for prescribed in ("vector-before", "vector-after", "psw_saved",
                           "dr0_saved", "dpx_saved", "dpx_restored",
                           "dr0_restored", "psw_restored", "reti-done"):
            _check(prescribed in names,
                   "prescribed window %s missing from evidence: %r"
                   % (prescribed, names))
        # The single-layer probe (slot 1) toggles its own shared bit at
        # offset 4 (the nested high layer owns offset 3).
        _check(evidence["bit_area"]["changed_offsets"] == [4],
               "shared bit change not observed: %r" % evidence["bit_area"])

    def check_observe_detects_frame_content_mismatch(self):
        model = FakeIsrModel(faults=("frame-corrupt",))
        rsp_client, qtest_client = self._fake_clients(model)
        deadline = CASE_DEADLINE_SOURCE() + 120
        negotiate_sstep_irq(rsp_client, deadline=deadline)
        boundary, _ = walk_to_main_loop(rsp_client, deadline,
                                        self._protected())
        failures, evidence = observe_one_injection(
            rsp_client, qtest_client, 1, boundary, deadline
        )
        _check(any(f["field"].startswith("hardware-frame-content")
                   for f in failures),
               "corrupt hardware frame not pinpointed: %r" % failures)

    def check_observe_detects_bit_rollback(self):
        model = FakeIsrModel(faults=("bit-rollback",))
        rsp_client, qtest_client = self._fake_clients(model)
        deadline = CASE_DEADLINE_SOURCE() + 120
        negotiate_sstep_irq(rsp_client, deadline=deadline)
        boundary, _ = walk_to_main_loop(rsp_client, deadline,
                                        self._protected())
        failures, evidence = observe_one_injection(
            rsp_client, qtest_client, 1, boundary, deadline
        )
        _check(any(f["field"] == "shared-bit-rolled-back-at-reti"
                   for f in failures),
               "shared-bit rollback not detected: %r" % failures)

    def check_observe_detects_missing_save_push(self):
        model = FakeIsrModel(faults=("save-corrupt",))
        rsp_client, qtest_client = self._fake_clients(model)
        deadline = CASE_DEADLINE_SOURCE() + 120
        negotiate_sstep_irq(rsp_client, deadline=deadline)
        boundary, _ = walk_to_main_loop(rsp_client, deadline,
                                        self._protected())
        failures, evidence = observe_one_injection(
            rsp_client, qtest_client, 1, boundary, deadline
        )
        _check(any(f["field"] == "SPX@dpx_saved" for f in failures),
               "missing DPX save push not pinpointed: %r" % failures)

    # -- 12. run_case end-to-end over the fake session ------------------------

    FAKE_MAP = (
        "MCS251 map\n"
        "crt-irq.o:.mcs251.HOME 0xff0000 +0x3\n"
        "crt-irq.o:.mcs251.BOOT 0xff0500 +0x106\n"
        "fw.o:.mcs251.CSEG 0xff0700 +0x100\n"
        "IRQ 01 0xff000b ISR _irq1\n"
        "IRQ 03 0xff001b ISR _irq3\n"
        "IRQ 06 0xff0033 DEFAULT __mcs251_isr_unhandled\n"
    )

    def _protected(self):
        # Home/vector floor plus the placeholder CRT loop range used by the
        # self-test fake model (the real CRT ranges come from the map).
        return [(0xFF0000, 0xFF0003), (0xFF0500, 0xFF0503)]

    def _fake_clients(self, model):
        deadline = CASE_DEADLINE_SOURCE() + 300
        rsp_client = rsp.RspClient(
            DeadlineTransport(FakeRspTransport(model), deadline),
            default_timeout=5.0,
        )
        qtest_client = qtest.QTestClient(
            DeadlineTransport(FakeQtestTransport(model), deadline),
            default_timeout=5.0,
        )
        return rsp_client, qtest_client

    def _fake_manifest(self, tmp, map_text=FAKE_MAP, image_sha=None):
        qemu_path = os.path.join(tmp, "qemu-fake")
        image_path = os.path.join(tmp, "fw.hex")
        map_path = os.path.join(tmp, "fw.map")
        with open(qemu_path, "wb") as handle:
            handle.write(b"fake-qemu")
        os.chmod(qemu_path, 0o755)  # run_case requires an executable qemu
        with open(image_path, "wb") as handle:
            handle.write(b"fake-image")
        with open(map_path, "w", encoding="utf-8") as handle:
            handle.write(map_text)
        return {
            "schema": 1,
            "qemu": {"path": qemu_path,
                     "sha256": hashlib.sha256(b"fake-qemu").hexdigest()},
            "tools": {},
            "machine": MACHINE,
            "image": {"path": image_path,
                      "sha256": image_sha
                      or hashlib.sha256(b"fake-image").hexdigest()},
            "map": map_path,
            "output_dir": os.path.join(tmp, "out"),
            "gdb_port": 1234,
            "qtest_socket": os.path.join(tmp, "q.sock"),
            "timeout_seconds": 30,
        }

    def check_run_case_single_layer_books_pass(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(manifest, deadline=None)
            record = run_case(manifest, "single",
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_PASS,
                   "fake single-layer run must PASS, got %s: %r"
                   % (record["result"], record["details"]))
            _check(record["iterations"] == 1, "single must run once")
            boundary = record["details"][0]["boundary_walk"]
            _check(boundary["pc"] in FakeIsrModel.MAIN_LOOP,
                   "boundary walk evidence missing a main-loop PC: %r"
                   % boundary)

    def check_run_case_nested_windows_books_pass(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(manifest, deadline=None)
            record = run_case(manifest, "nested-windows",
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_PASS,
                   "fake nested run must PASS, got %s: %r"
                   % (record["result"], record["details"]))
            evidence = record["details"][0]["evidence"]
            _check(set(evidence["boundary"]["regs"]) == set(SNAPSHOT_FIELDS),
                   "main boundary must have a full snapshot")
            _check(evidence["high_base_spx"] == 0x0400 + FRAME_LAYER_BYTES,
                   "high layer must anchor at low S+41, got %r"
                   % evidence.get("high_base_spx"))
            _check(evidence["high_body_register_writes"] == ["R5", "R60"],
                   "high layer sentinel writes missing: %r"
                   % evidence.get("high_body_register_writes"))
            high_windows = evidence["high_windows"]
            _check([w["window"] for w in high_windows["vector"]]
                   == ["high-vector-before", "high-vector-after"],
                   "high vector boundaries missing")
            for window in high_windows["vector"]:
                _check(set(window["regs"]) == set(SNAPSHOT_FIELDS),
                       "high vector boundary must have a full snapshot")
            names = ([w.get("window") for w in high_windows["save"]]
                     + [w.get("window") for w in high_windows["restore"]])
            for prescribed in ("psw_saved", "dpx_saved", "dpx_restored",
                               "psw_restored"):
                _check(prescribed in names,
                       "high-layer window %s missing: %r"
                       % (prescribed, names))
            _check(not evidence["not_compared"]["high_to_low"]
                   and not evidence["not_compared"]["low_to_main"],
                   "nothing may be missing from the boundary comparisons")

    def check_run_case_detects_empty_high_layer(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(
                manifest, deadline=None,
                model=FakeIsrModel(faults=("silent-body",)))
            record = run_case(manifest, "nested-windows",
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_FAIL,
                   "an empty high-layer ISR must FAIL, got %s"
                   % record["result"])
            failure_text = json.dumps(record["details"])
            _check("high-layer-sentinel-writes" in failure_text,
                   "sentinel failure not reported: %s" % failure_text[:400])
            _check("shared-bit-change" in failure_text,
                   "empty high-layer bit proof not reported")

    def check_run_case_default_books_pass(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(manifest, deadline=None)
            record = run_case(manifest, "default",
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_PASS,
                   "fake default run must PASS, got %s: %r"
                   % (record["result"], record["details"]))
            evidence = record["details"][-1]["evidence"]
            _check(evidence["default_entry"] == FakeIsrModel.DEFAULT_ENTRY,
                   "default entry must resolve via the vector EJMP target, "
                   "got 0x%06x" % evidence["default_entry"])
            _check(evidence["pcs"][-1] == evidence["pcs"][-2],
                   "default entry must halt in a stable loop: %r"
                   % evidence["pcs"])

    def check_run_case_transport_timeout_books_timeout(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(manifest, deadline=None, dead_rsp=True)
            record = run_case(manifest, "single",
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_TIMEOUT,
                   "a transport timeout must book TIMEOUT, got %s"
                   % record["result"])

    def check_run_case_sha_mismatch_books_baseline(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp, image_sha="0" * 64)
            record = run_case(manifest, "single",
                              session_factory=FakeSession)
            _check(record["result"] == RESULT_BASELINE_MISMATCH,
                   "sha mismatch must book BASELINE_MISMATCH, got %s"
                   % record["result"])
            _check(record["details"][0]["baseline_error"]
                   and "sha256 mismatch" in record["details"][0]
                   ["baseline_error"],
                   "baseline detail missing: %r" % record["details"])

    def check_run_case_missing_map_is_prereq(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            os.remove(manifest["map"])
            try:
                run_case(manifest, "single", session_factory=FakeSession)
            except CaseEnvironmentError as exc:
                _check("map" in str(exc),
                       "missing map must be named: %s" % exc)
            else:
                raise SelfTestFailure("missing map did not stop the case")

    def check_run_case_crt_only_image_is_prereq(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(
                manifest, deadline=None,
                model=FakeIsrModel(faults=("no-main-loop",)))
            try:
                run_case(manifest, "single",
                         session_factory=lambda m, d: session)
            except CaseEnvironmentError as exc:
                _check("main-loop" in str(exc),
                       "wrong prereq error: %s" % exc)
            else:
                raise SelfTestFailure("CRT-only image did not stop the case")

    def check_run_case_ea_clear_is_prereq_all_cases(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            for case in CASES:
                model = FakeIsrModel()
                model.ie = 0
                session = FakeSession(manifest, model=model)
                try:
                    run_case(manifest, case, iterations=1,
                             session_factory=lambda m, d: session)
                except CaseEnvironmentError as exc:
                    _check("EA is clear" in str(exc),
                           "wrong EA prerequisite error for %s: %s"
                           % (case, exc))
                    _check(session.closed, "prerequisite must close session")
                    _check(not model.line_level.get(1, 0)
                           and not model.line_level.get(6, 0),
                           "EA-clear fixture must not receive an injection")
                else:
                    raise SelfTestFailure("EA-clear %s was not NOT RUN" % case)

    # -- 13. R1: connection retries bounded by the case deadline ------------

    def check_connect_deadline_bounded_tcp(self):
        recorded = []
        deadline = CASE_DEADLINE_SOURCE() + 0.4

        def connect(timeout):
            recorded.append(timeout)
            raise OSError("connection refused")

        started = CASE_DEADLINE_SOURCE()
        try:
            connect_with_deadline(deadline, connect, "RSP tcp",
                                  retries=20, delay=0.05,
                                  base_timeout=30.0)
        except CaseTimeout:
            pass
        else:
            raise SelfTestFailure("deadline expiry during TCP connect must "
                                  "raise CaseTimeout (TIMEOUT), not "
                                  "CaseEnvironmentError")
        elapsed = CASE_DEADLINE_SOURCE() - started
        _check(len(recorded) >= 2, "retries must have been attempted")
        _check(all(t <= 0.4 + 1e-9 for t in recorded),
               "connect budgets must derive from the remaining deadline: %r"
               % recorded)
        _check(recorded[-1] < recorded[0],
               "connect budgets must shrink as the deadline approaches: %r"
               % recorded)
        _check(elapsed < 2.0,
               "deadline-bounded connect took %.2fs; fixed retries would "
               "burn the full retry loop past the deadline" % elapsed)

    def check_connect_deadline_bounded_unix(self):
        recorded = []
        deadline = CASE_DEADLINE_SOURCE() + 5.0

        def connect(timeout):
            recorded.append(timeout)
            if len(recorded) < 3:
                raise OSError("unix socket not ready")
            return "connected"

        result = connect_with_deadline(deadline, connect,
                                       "qtest unix socket",
                                       retries=20, delay=0.001,
                                       base_timeout=30.0)
        _check(result == "connected" and len(recorded) == 3,
               "transient unix-socket failures must be retried within the "
               "deadline")
        _check(all(t <= 5.0 for t in recorded),
               "unix connect budgets must be deadline-derived: %r"
               % recorded)

    def check_connect_retry_budget_exhaustion_is_env_error(self):
        deadline = CASE_DEADLINE_SOURCE() + 60.0

        def connect(timeout):
            raise OSError("connection refused")

        try:
            connect_with_deadline(deadline, connect, "RSP", retries=3,
                                  delay=0.001, base_timeout=10.0)
        except CaseEnvironmentError as exc:
            _check("could not connect" in str(exc),
                   "wrong environment error: %s" % exc)
        except CaseTimeout:
            raise SelfTestFailure("retry exhaustion with time left is an "
                                  "environment error, not TIMEOUT")
        else:
            raise SelfTestFailure("exhausted retries were accepted")

    def check_session_connections_are_reused(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(manifest, deadline=None)
            _check(session.connect_rsp() is session.connect_rsp(),
                   "the RSP client must be reused across rounds (R3)")
            _check(session.connect_qtest() is session.connect_qtest(),
                   "the qtest client must be reused across rounds (R3)")

    def check_qemu_session_reuse_and_cleanup(self):
        # Exercise the production session, not just FakeSession's duplicate
        # cache implementation. No QEMU process or real socket is created.
        import tempfile
        from unittest.mock import Mock, patch
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            model = FakeIsrModel()
            tcp = FakeRspTransport(model)
            unix = FakeQtestTransport(model)
            tcp.close = Mock()
            unix.close = Mock()
            process = Mock()
            process.poll.return_value = None
            with patch.object(subprocess, "Popen", return_value=process), \
                    patch.object(rsp.SocketTransport, "connect_tcp",
                                 return_value=tcp) as connect_tcp, \
                    patch.object(rsp.SocketTransport, "connect_unix",
                                 return_value=unix) as connect_unix:
                session = QemuSession(manifest)
                try:
                    for iteration in range(2):
                        failures, _ = _run_nested_case(session, session.deadline)
                        _check(not failures, "production-session round %d: %r"
                               % (iteration + 1, failures))
                        _check(not model.pending and not model.layers
                               and model.line_level == {1: 0, 3: 0}
                               and model.spx == 0x400,
                               "round must leave IRQ and stack state clean")
                        _check(not tcp.rx and not tcp.buf
                               and not unix.rx and not unix.buf,
                               "round must consume all protocol replies")
                    _check(connect_tcp.call_count == 1
                           and connect_unix.call_count == 1,
                           "production session must connect once per transport")
                finally:
                    session.close()
                _check(tcp.close.call_count == 1 and unix.close.call_count == 1,
                       "case teardown must close both cached transports once")
                _check(process.terminate.call_count == 1
                       and process.wait.call_count == 1
                       and session.qemu_log.closed,
                       "case teardown must reap the process and close its log")

    def check_long_run_failure_not_counted(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(
                manifest, model=FakeIsrModel(faults=("fail-second-visit",)))
            record = run_case(manifest, "long-run", iterations=2,
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_FAIL,
                   "second-visit long-run fault must FAIL")
            _check(record["completed_iterations"] == 1,
                   "failed long-run iteration must not count as completed: %r"
                   % record["completed_iterations"])
            rounds = [d for d in record["details"] if "iteration" in d]
            _check(len(rounds) == 2 and not rounds[0]["failures"]
                   and any(f["field"] == "SPX@dpx_saved"
                           for f in rounds[1]["failures"]),
                   "count regression must exercise a clean then failing round")

    # -- 14. R3: multi-round nested with honest completion counting ---------

    def check_nested_two_rounds_books_pass(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(manifest, deadline=None)
            record = run_case(manifest, "nested-windows", iterations=2,
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_PASS,
                   "two clean nested rounds must PASS, got %s: %r"
                   % (record["result"], record["details"])[:400])
            _check(record["iterations"] == 2
                   and record["completed_iterations"] == 2,
                   "two completed two-level sequences must be counted, "
                   "got requested=%r completed=%r"
                   % (record["iterations"], record["completed_iterations"]))
            _check(len(record["details"]) == 2,
                   "both rounds must be recorded")
            round2 = record["details"][1]["evidence"]
            _check(round2["high_bit_area"]["changed_offsets"] == [3],
                   "round 2 must show a fresh observable shared-bit change "
                   "(toggle fixture, ruling f): %r"
                   % round2["high_bit_area"])

    def check_nested_midrun_failure_counts(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(
                manifest, deadline=None,
                model=FakeIsrModel(faults=("fail-second-visit",)))
            record = run_case(manifest, "nested-windows", iterations=2,
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_FAIL,
                   "a mid-run failure must FAIL, got %s" % record["result"])
            _check(record["completed_iterations"] == 1,
                   "the failing second round must not count as completed, "
                   "got %r" % record["completed_iterations"])
            second = record["details"][1]
            _check(second["iteration"] == 2 and second["failures"],
                   "the second-round failure must be recorded")
            _check(any(f["field"] == "SPX@dpx_saved"
                       for f in second["failures"]),
                   "second-round save corruption not pinpointed: %r"
                   % second["failures"])

    # -- 15. R2: cross-layer shared-bit protection ---------------------------

    def check_low_layer_protects_high_bit(self):
        # Negative: the high layer sets the shared bit, the low body never
        # touches it, the low restore path clears it -> the low layer must
        # FAIL on rollback. With the pre-R2 baseline (taken after the high
        # RETI) this booked a false PASS.
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(
                manifest, deadline=None,
                model=FakeIsrModel(faults=("low-restores-clears-bit",)))
            record = run_case(manifest, "nested-windows",
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_FAIL,
                   "a low restore clearing the high layer's shared bit must "
                   "FAIL, got %s" % record["result"])
            text = json.dumps(record["details"])
            _check("shared-bit-rolled-back-at-reti" in text,
                   "cross-layer rollback not detected: %s" % text[:400])

    def check_low_scratch_cleanup_is_legal(self):
        # Positive: the low body legitimately cleans its own scratch bit
        # inside the body (before the last sample) — never a rollback.
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(
                manifest, deadline=None,
                model=FakeIsrModel(faults=("low-scratch-clean",)))
            record = run_case(manifest, "nested-windows",
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_PASS,
                   "in-body scratch cleanup must not be flagged as rollback,"
                   " got %s: %r" % (record["result"],
                                    record["details"])[:400])

    # -- 16. ruling (b): deferred acceptance is explicit ---------------------

    def check_deferred_acceptance_is_prereq(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(
                manifest, deadline=None,
                model=FakeIsrModel(faults=("deferred-accept",)))
            try:
                run_case(manifest, "single",
                         session_factory=lambda m, d: session)
            except CaseEnvironmentError as exc:
                _check("deferred acceptance" in str(exc)
                       and "immediate acceptance" in str(exc),
                       "deferred acceptance must be an explicit fixture "
                       "prerequisite mismatch: %s" % exc)
            else:
                raise SelfTestFailure("deferred acceptance was not rejected")

    # -- 17. ruling (b) third round: pre-step snapshot verification ----------

    def check_accept_pc_only_corruption_fails(self):
        # Counterexample A: immediate acceptance whose frame has only the
        # PC[7:0] byte flipped (PSW1 correct). The frame is self-consistent
        # for its own decoded PC — the old tautology booked NOT RUN. The
        # pre-step snapshot comparison must FAIL it instead.
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(
                manifest, deadline=None,
                model=FakeIsrModel(faults=("pc-only-corrupt",)))
            record = run_case(manifest, "single",
                              session_factory=lambda m, d: session)
            _check(record["result"] == RESULT_FAIL,
                   "a PC-only corrupted frame must FAIL, got %s"
                   % record["result"])
            iteration = record["details"][1]
            _check(any(f["field"].startswith("hardware-frame-content")
                       for f in iteration["failures"]),
                   "PC-only corruption not pinpointed: %r"
                   % iteration["failures"])
            _check(all(f["field"].startswith("hardware-frame-content")
                       for f in iteration["failures"]),
                   "PC-only corruption must be an assertion FAIL, never "
                   "downgraded to a prerequisite mismatch")

    def check_accept_deferred_two_steps_is_prereq(self):
        # Counterexample B: a fixture that defers acceptance by two steps.
        # The two-PC main loop wraps back onto the boundary PC, so the
        # frame equals the boundary frame and the old self-consistency
        # check booked a PASS. The single-step timeline must classify this
        # as deferred acceptance — NOT RUN, immune to PC wrap-around.
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            session = FakeSession(
                manifest, deadline=None,
                model=FakeIsrModel(faults=("deferred-accept-2",)))
            try:
                run_case(manifest, "single",
                         session_factory=lambda m, d: session)
            except CaseEnvironmentError as exc:
                _check("deferred acceptance" in str(exc)
                       and "immediate acceptance" in str(exc),
                       "deferred acceptance must be explicit: %s" % exc)
            else:
                raise SelfTestFailure("a 2-step deferred acceptance with a "
                                      "wrapped PC must not PASS")

    def check_frame_snapshot_comparator_mutations(self):
        # Mutation test of the independent comparator: every single-byte
        # mutation of the frame must be detected, the exact snapshot frame
        # must match, and the comparator must track the snapshot it is
        # given (not constants, not the frame's own decode).
        pre_step = self._boundary_snapshot(spx=0x0400)
        good = hardware_frame_bytes(pre_step["PSW1"], pre_step["PC"])
        _check(frame_matches_snapshot(good, pre_step),
               "the exact pre-step frame must verify")
        for index in range(len(good)):
            mutated = bytearray(good)
            mutated[index] ^= 0x40
            _check(not frame_matches_snapshot(bytes(mutated), pre_step),
                   "byte %d mutation not detected by the snapshot "
                   "comparator" % index)
        other = self._boundary_snapshot(spx=0x0500, psw=0x11, pc=0x00FF0602)
        other_frame = hardware_frame_bytes(other["PSW1"], other["PC"])
        _check(frame_matches_snapshot(other_frame, other),
               "comparator must follow the snapshot it is given")
        _check(not frame_matches_snapshot(other_frame, pre_step),
               "a frame from another boundary must not verify here")

    def check_nested_accept_corruption_classified(self):
        # Every alternate caller must preserve the complete corrupt-frame
        # candidate, including the stop PC/SPX, in the serialized record.
        import tempfile

        class SlotCorruptModel(FakeIsrModel):
            def __init__(self, target):
                super().__init__()
                self.target = target

            def _accept(self, slot):
                if slot == self.target:
                    self.faults.add("pc-only-corrupt")
                super()._accept(slot)
                self.faults.discard("pc-only-corrupt")

        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._fake_manifest(tmp)
            for case, slot, layer in (("nested-windows", 1, "low"),
                                     ("nested-windows", 3, "high"),
                                     ("default", 6, "default")):
                session = FakeSession(
                    manifest, deadline=None, model=SlotCorruptModel(slot))
                record = run_case(manifest, case,
                                  session_factory=lambda m, d: session)
                _check(record["result"] == RESULT_FAIL,
                       "%s corrupted acceptance frame must FAIL" % layer)
                detail = record["details"][-1]
                _check(any(f["field"].startswith(
                    "hardware-frame-content@%s@" % layer)
                    for f in detail["failures"]),
                    "%s frame corruption not pinpointed" % layer)
                key = ("frame_candidates" if layer == "default"
                       else layer + "_frame_candidates")
                candidate = detail["evidence"][key][0]
                _check(candidate["frame"] != candidate["expected"]
                       and candidate["deferred"] is False
                       and candidate["pc"] == vector_addr(slot)
                       and candidate["spx"] == session.model.spx,
                       "%s corrupt-frame evidence lost: %r"
                       % (layer, candidate))
                _check("deferred acceptance" not in json.dumps(record),
                       "frame corruption must not be classified as deferred")

    def check_schedule_case_budget_matrix(self):
        iters, budget = schedule_case_budget("long-run", None, 30)
        _check(iters == LONG_RUN_DEFAULT_ITERATIONS
               and budget == 30 * (LONG_RUN_DEFAULT_ITERATIONS + 1),
               "long-run default schedule wrong: %r" % ((iters, budget),))
        iters, budget = schedule_case_budget("nested-windows", 2, 30)
        _check(iters == 2 and budget == 30 * 5,
               "nested schedule must cost two units per iteration: %r"
               % ((iters, budget),))
        iters, budget = schedule_case_budget("default", None, 30)
        _check(iters == 1 and budget == 90, "default schedule wrong")
        iters, budget = schedule_case_budget("single", 4, 30)
        _check(iters == 4 and budget == 150, "single schedule wrong")
        try:
            schedule_case_budget("bogus", None, 30)
        except CaseEnvironmentError:
            pass
        else:
            raise SelfTestFailure("unknown case must not be schedulable")

    # -- driver --------------------------------------------------------------

    def run(self, out=sys.stdout):
        passed = failed = 0
        for name, check in self.checks:
            try:
                check()
            except SelfTestFailure as exc:
                failed += 1
                print("FAIL %s: %s" % (name, exc), file=out)
            except Exception as exc:  # unexpected: still a failure, with type
                failed += 1
                print("FAIL %s: unexpected %s: %s"
                      % (name, type(exc).__name__, exc), file=out)
            else:
                passed += 1
                print("PASS %s" % name, file=out)
        print("self-test methods (not atomic assertions): %d passed, %d failed" % (passed, failed),
              file=out)
        if failed:
            print(SELF_TEST_FAIL_LINE, file=out)
            return False
        print(SELF_TEST_PASS_LINE, file=out)
        return True


# ---------------------------------------------------------------------------
# Real-run infrastructure (exercised only with a manifest and the frozen T09
# assets; the self-test never touches any of this)
# ---------------------------------------------------------------------------


class CaseEnvironmentError(Exception):
    """Qualification prerequisites are missing (no NOT_RUN disguise).

    Used for fixture-prerequisite mismatches as well (e.g. EA left clear by
    the fixture image): the case cannot run as specified, so it must stop
    and book NOT RUN — it is never a model capability limit and never a
    disguised PASS.
    """


class CaseTimeout(Exception):
    """The case exceeded its deadline. Classified as TIMEOUT."""


class ModelCapabilityError(Exception):
    """The model cannot support a required capability.

    Recorded as MODEL_UNSUPPORTED; expectations are never lowered to
    manufacture a PASS.
    """


class FrozenBaselineError(Exception):
    """Frozen baseline bytes of the image did not match.

    Covers the A5 vector EJMP form and the A5 default-entry machine bytes.
    Classified as BASELINE_MISMATCH, never mixed into ordinary assertion
    FAIL (board-results.json status_rules: "frozen baseline bytes did not
    match the manifest").
    """


# One case deadline, one time source: time.monotonic(). Every transport read
# in the run derives its budget from the absolute deadline.
CASE_DEADLINE_SOURCE = time.monotonic

# Single-step IRQ mask negotiation (verified against the model source,
# read-only):
#   * gdbstub/gdbstub.c:76 defaults sstep_flags to
#     SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER;
#   * accel/tcg/cpu-exec.c:831 masks CPU_INTERRUPT_HARD out of every step
#     while SSTEP_NOIRQ is set, so a pending IRQ is never accepted while
#     single-stepping;
#   * gdbstub/gdbstub.c:1553 handle_set_qemu_sstep accepts "Qqemu.sstep=<hex
#     mask>", answers OK, and rejects bits the accelerator does not allow
#     with E22;
#   * gdbstub/gdbstub.c:1572 handle_query_qemu_sstep serves the read query
#     "qqemu.sstep" (registered name "qemu.sstep" at gdbstub.c:1793 under
#     the dispatcher's 'q' prefix) and answers "0x<hex mask>". RSP packet
#     names carry no spaces and "qR" is not a legal read prefix, so a form
#     like "qR qemu.sstep" is an unknown packet that a compliant stub
#     answers with an empty reply (R04);
#   * include/hw/core/cpu.h:1128-1130: SSTEP_ENABLE=0x1, SSTEP_NOIRQ=0x2,
#     SSTEP_NOTIMER=0x4.
# Clearing NOIRQ|NOTIMER (mask = SSTEP_ENABLE only) is required so that a
# single-stepped CPU actually takes an injected interrupt.
SSTEP_ENABLE = 0x1
SSTEP_NOIRQ = 0x2
SSTEP_NOTIMER = 0x4
SSTEP_IRQ_ALLOWED_MASK = SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER
SSTEP_IRQ_NEGOTIATION_MASK = SSTEP_ENABLE  # clear NOIRQ and NOTIMER

# A6 hardware frame profile 1: 4 bytes pushed in the order PSW1, PC[23:16],
# PC[7:0], PC[15:8]; push writes at SPX+1 (target/mcs51/helper.c:143-154,
# 2118-2126), so the frame occupies [S+1 .. S+4] after acceptance.
HARDWARE_FRAME_ADDR_OFFSET = 1  # frame bytes start at S+1


def hardware_frame_bytes(psw1, pc):
    """The frozen 4B hardware frame for interrupted (psw1, pc), push order."""
    return bytes(
        [
            psw1 & 0xFF,
            (pc >> 16) & 0xFF,
            pc & 0xFF,
            (pc >> 8) & 0xFF,
        ]
    )


# Shared bit-byte area (idata 0x20-0x2F, EDATA at flat data address 0x20;
# target/mcs51/cpu.h MCS51_IDATA region, include/hw/mcs51/stc32g.h
# STC32G_EDATA_BASE=0x000000). The T10 card requires proof that a shared bit
# modification performed by an ISR survives the RETI (no rollback).
BIT_AREA_ADDR = 0x20
BIT_AREA_SIZE = 16

# Main-loop boundary detection (walk_to_main_loop): the application loop
# must repeat its PC with SPX stable this many consecutive steps before the
# boundary counts as real.
MAIN_LOOP_STABLE_STEPS = 4
MAIN_LOOP_MIN_STEPS = 8
CRT_WALK_BUDGET = 20000


class DeadlineTransport:
    """Wraps a transport and enforces one absolute monotonic deadline.

    Every underlying read receives min(requested_timeout, remaining) as its
    budget, so a slow-drip peer cannot stretch a single command past the
    case deadline by keeping each individual read just under its own
    relative timeout. Expiry raises the builtin TimeoutError, which
    RspClient/QTestClient normalize into RspTimeout/QTestTimeout and
    run_case records as TIMEOUT (never PASS).
    """

    def __init__(self, inner, deadline):
        self._inner = inner
        self._deadline = deadline

    @property
    def deadline(self):
        return self._deadline

    def write(self, data):
        self._inner.write(data)

    def read(self, n, timeout):
        remaining = self._deadline - CASE_DEADLINE_SOURCE()
        if remaining <= 0:
            raise TimeoutError("case deadline exhausted before read")
        return self._inner.read(n, min(timeout, remaining))

    def close(self):
        self._inner.close()


def build_qemu_command(manifest):
    """Frozen QEMU launch line from the T10 card."""
    qemu_log = os.path.join(manifest["output_dir"], "qemu.log")
    return [
        manifest["qemu"]["path"],
        "-M", manifest["machine"],
        "-S",
        "-gdb", "tcp:127.0.0.1:%d" % manifest["gdb_port"],
        "-qtest", "unix:%s,server=on,wait=off" % manifest["qtest_socket"],
        "-display", "none",
        "-serial", "file:%s" % os.path.join(manifest["output_dir"],
                                            "serial.log"),
        "-monitor", "none",
        "-kernel", manifest["image"]["path"],
    ], qemu_log


class QemuSession:
    """One exclusive QEMU instance per qualification run.

    All RSP/qtest traffic flows through a DeadlineTransport bound to the
    case's absolute monotonic deadline, so every per-byte/per-fragment read
    budget shrinks with elapsed time and a slow peer cannot outlive the
    case deadline.
    """

    def __init__(self, manifest, deadline=None):
        self.manifest = manifest
        self.timeout = manifest["timeout_seconds"]
        self.deadline = deadline if deadline is not None else (
            CASE_DEADLINE_SOURCE() + self.timeout
        )
        self.output_dir = manifest["output_dir"]
        os.makedirs(self.output_dir, exist_ok=True)
        command, self.qemu_log_path = build_qemu_command(manifest)
        self.qemu_log = open(self.qemu_log_path, "ab")
        self.process = subprocess.Popen(
            command, stdout=self.qemu_log, stderr=subprocess.STDOUT
        )
        self._rsp = None
        self._qtest = None

    def connect_rsp(self, retries=20, delay=0.25):
        if self._rsp is not None:
            return self._rsp  # one connection per case, reused (R3)
        def connect(timeout):
            if self.process.poll() is not None:
                raise CaseEnvironmentError(
                    "QEMU exited early (rc=%s), see %s"
                    % (self.process.returncode, self.qemu_log_path)
                )
            sock = rsp.SocketTransport.connect_tcp(
                "127.0.0.1", self.manifest["gdb_port"], timeout=timeout
            )
            self._rsp = rsp.RspClient(
                DeadlineTransport(sock, self.deadline),
                default_timeout=self.timeout,
            )
            return self._rsp
        return connect_with_deadline(self.deadline, connect, "RSP",
                                     retries=retries, delay=delay,
                                     base_timeout=self.timeout)

    def connect_qtest(self, retries=20, delay=0.25):
        if self._qtest is not None:
            return self._qtest  # one connection per case, reused (R3)
        def connect(timeout):
            if self.process.poll() is not None:
                raise CaseEnvironmentError(
                    "QEMU exited early (rc=%s), see %s"
                    % (self.process.returncode, self.qemu_log_path)
                )
            sock = rsp.SocketTransport.connect_unix(
                self.manifest["qtest_socket"], timeout=timeout
            )
            self._qtest = qtest.QTestClient(
                DeadlineTransport(sock, self.deadline),
                default_timeout=self.timeout,
            )
            return self._qtest
        return connect_with_deadline(self.deadline, connect, "qtest socket",
                                     retries=retries, delay=delay,
                                     base_timeout=self.timeout)

    def close(self):
        for client in (self._rsp, self._qtest):
            if client is not None:
                try:
                    client.close()
                except OSError:
                    pass
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
        self.qemu_log.close()


def connect_with_deadline(deadline, connect, what, retries=20, delay=0.25,
                          base_timeout=10.0):
    """Connect with retries; every wait is bounded by one absolute deadline.

    R1: both the connect timeout handed to `connect` and each retry sleep
    derive from the remaining time to the case deadline — a retry loop can
    never outlive it. Deadline exhaustion raises CaseTimeout (classified
    TIMEOUT: a hung or too-slow model is never disguised as an environment
    error); exhausting the retry budget while time remains is a
    CaseEnvironmentError (environment not ready). Used for both the TCP
    (RSP) and the unix-socket (qtest) paths.
    """
    last_error = None
    for _ in range(retries):
        remaining = deadline - CASE_DEADLINE_SOURCE()
        if remaining <= 0:
            raise CaseTimeout(
                "case deadline exhausted while connecting %s: %s"
                % (what, last_error)
            )
        try:
            return connect(min(base_timeout, remaining))
        except OSError as exc:
            last_error = exc
        time.sleep(min(delay, max(deadline - CASE_DEADLINE_SOURCE(), 0.0)))
    if deadline - CASE_DEADLINE_SOURCE() <= 0:
        raise CaseTimeout(
            "case deadline exhausted while connecting %s: %s"
            % (what, last_error)
        )
    raise CaseEnvironmentError(
        "could not connect %s: %s" % (what, last_error)
    )


def negotiate_sstep_irq(rsp_client, deadline=None):
    """Clear SSTEP_NOIRQ so single-stepping can accept injected IRQs.

    Sends "Qqemu.sstep=1" (SSTEP_ENABLE only — see the SSTEP_* provenance
    comment near the constants), then reads the mask back with the query
    "qqemu.sstep" (the registered name "qemu.sstep" under the 'q'
    dispatcher prefix; RSP packets carry no spaces) as proof.
    Any E reply, malformed reply or deadline breach is a prerequisite
    failure: without this negotiation an injected interrupt is never
    accepted while single-stepping and every case would misreport.
    """
    timeout = _remaining(deadline) if deadline is not None else None
    reply = rsp_client.command(b"Qqemu.sstep=%x" % SSTEP_IRQ_NEGOTIATION_MASK,
                               timeout=timeout)
    if reply != b"OK":
        raise CaseEnvironmentError(
            "single-step IRQ negotiation rejected: Qqemu.sstep=%x -> %r"
            % (SSTEP_IRQ_NEGOTIATION_MASK, reply)
        )
    timeout = _remaining(deadline) if deadline is not None else None
    mask_text = rsp_client.command(b"qqemu.sstep", timeout=timeout)
    match = re.match(r"^0x([0-9a-fA-F]+)$", mask_text.decode("ascii", "replace"))
    if not match or int(match.group(1), 16) != SSTEP_IRQ_NEGOTIATION_MASK:
        raise CaseEnvironmentError(
            "single-step IRQ negotiation did not stick: qqemu.sstep -> %r"
            % mask_text
        )
    return SSTEP_IRQ_NEGOTIATION_MASK


def precheck_environment(manifest, need_default_slot=False):
    """Existence precheck for every case, before anything can PASS.

    Missing manifest-referenced files (qemu, image, map, every tools entry)
    stop the case as a prerequisite failure — a missing map can never book
    PASS. Also requires the frozen CRT section lines (.mcs251.HOME /
    .mcs251.BOOT) the main-loop walk anchors on, and — for the default case
    — at least one DEFAULT-classified slot.
    """
    missing = []
    for what, path in (
        ("qemu", manifest["qemu"]["path"]),
        ("image", manifest["image"]["path"]),
        ("map", manifest["map"]),
    ):
        if not os.path.isfile(path):
            missing.append("%s file: %s" % (what, path))
    for name, path in sorted(manifest["tools"].items()):
        if not os.path.isfile(path):
            missing.append("tools[%r] file: %s" % (name, path))
    if missing:
        raise CaseEnvironmentError(
            "missing %s" % "; ".join(missing)
        )
    irq_map = parse_irq_map(manifest["map"])
    map_sections = parse_map_sections(manifest["map"])
    if not protected_crt_ranges(map_sections):
        raise CaseEnvironmentError(
            "map %s has no .mcs251.HOME/.mcs251.BOOT section lines; the "
            "main-loop boundary walk cannot be anchored" % manifest["map"]
        )
    if need_default_slot and not default_slots(irq_map):
        raise CaseEnvironmentError(
            "map %s has no DEFAULT entry line" % manifest["map"]
        )
    return {"irq_map": irq_map, "map_sections": map_sections}


def connect_and_probe(session, slot):
    """Common setup: RSP connect, sstep negotiation, qtest path probe.

    Ordering designed around the set_irq_in limitations verified in
    system/qtest.c (read-only):

      * The probe (assert_irq_input_exists, a level-0 write) runs exactly
        once, before any injection on that input, so it can never clear a
        pending state raised by this harness. A device-internal pending
        state left over from reset is theoretically possible and accepted;
        the authoritative acceptance check is the first post-injection
        single-step and its pre-step snapshot comparison, not the probe.
      * The CPU exposes MCS251_NUM_IRQS = 64 unnamed GPIO inputs
        (target/mcs51/cpu.c:717), so nums 0..63 always exist; a num >= 64
        would hit a g_assert and abort the model instead of answering FAIL,
        so probes and injections never use num >= 64.
      * "FAIL Unknown device" (unresolvable QOM path) is authoritative and
        stops the case — paths are never guessed.
    """
    rsp_client = session.connect_rsp()
    negotiate_sstep_irq(rsp_client, deadline=session.deadline)
    qtest_client = session.connect_qtest()
    try:
        qtest_client.assert_irq_input_exists(
            QOM_CPU_PATH, slot, timeout=_remaining(session.deadline)
        )
    except qtest.QTestCommandError as exc:
        raise ModelCapabilityError(
            "CPU IRQ input %d not available at %s: %s"
            % (slot, QOM_CPU_PATH, exc.reason)
        ) from None
    return rsp_client, qtest_client


MAX_ISR_STEPS = 4000
# Acceptance is decided on the FIRST single-step after injection (rework
# ruling b, closure condition 2); there is no multi-step acceptance budget.

# A5 frozen vector geometry lives next to the map parser (see the G1 profile
# block above): ISR_VECTOR_BASE / ISR_VECTOR_STRIDE / ISR_VECTOR_COUNT /
# ISR_VECTOR_MAX_SLOT are defined there, once, so the parser and every
# consumer share one restatement of the profile.


def vector_addr(slot):
    """Frozen A5 vector address of a legal slot."""
    return ISR_VECTOR_BASE + ISR_VECTOR_STRIDE * slot


def frame_matches_snapshot(frame, pre_step):
    """Independent frame verification (rework ruling b, closure condition 1).

    True iff the candidate frame equals the frozen hardware frame of the
    pre-step snapshot — the genuinely interrupted instruction edge, read
    immediately before the accepting single step. The frame's own decoded
    bytes are never evidence: "self-consistency" is a tautology (any
    PSW1-preserving frame decodes to itself) and PC equality cannot prove
    immediacy.
    """
    return frame == hardware_frame_bytes(pre_step["PSW1"], pre_step["PC"])


def step_until_irq_accepted(rsp_client, deadline):
    """Take exactly one single-step and verify immediate IRQ acceptance.

    Rework ruling (b), third-round closure: the interrupted context is
    verified against an independent pre-step snapshot taken immediately
    before the accepting single step.

      * Acceptance must land on the FIRST single-step after injection;
        the frozen fixture guarantees immediate acceptance. If the first
        stop does not show the hardware frame (SPX == S+4), a normal
        instruction executed before acceptance — a deferred acceptance,
        booked as a fixture prerequisite mismatch (NOT RUN). The
        classification is made by the single-step timeline, so a main
        loop wrapping the PC back to the boundary value cannot turn a
        deferred acceptance into a PASS.
      * On an immediate acceptance the 4B frame at [S+1..S+4] must equal
        hardware_frame_bytes(pre_step["PSW1"], pre_step["PC"]) via
        frame_matches_snapshot. PC-only or PSW1-only corruption
        mismatches the snapshot and is a hardware-frame-content assertion
        FAIL — never downgraded to NOT RUN.

    Returns (steps, accepted_state_or_None, observed_pcs, candidates).
    candidates preserves the evidence: the offending frame plus the
    expected snapshot frame (immediate corruption), or the observed
    boundary (deferral).
    """
    pre_step = take_snapshot(rsp_client, timeout=_remaining(deadline))
    observed_pcs = [pre_step["PC"]]
    rsp_client.step(timeout=_remaining(deadline))
    after = take_snapshot(rsp_client, timeout=_remaining(deadline))
    if after["SPX"] != pre_step["SPX"] + FRAME_HARDWARE_BYTES:
        return 1, None, observed_pcs, [
            {"pc": after["PC"], "spx": after["SPX"], "deferred": True}
        ]
    frame = rsp_client.read_memory(
        pre_step["SPX"] + HARDWARE_FRAME_ADDR_OFFSET, FRAME_HARDWARE_BYTES,
        timeout=_remaining(deadline)
    )
    if frame_matches_snapshot(frame, pre_step):
        return 1, after, observed_pcs, []
    return 1, None, observed_pcs, [
        {"pc": after["PC"], "spx": after["SPX"], "deferred": False,
         "frame": frame.hex(),
         "expected": hardware_frame_bytes(pre_step["PSW1"],
                                          pre_step["PC"]).hex()}
    ]


def _classify_non_acceptance(candidates):
    """Classify a non-accepting first step (rework ruling b, closure 2-3).

    A deferred acceptance — a normal instruction executed before the
    interrupt was taken — is a fixture prerequisite mismatch (NOT RUN);
    the classification rests on the single-step timeline, so PC wrap-around
    is irrelevant. An immediate acceptance whose frame does not match the
    pre-step snapshot (PC-only or PSW1-only corruption) stays a
    hardware-frame-content assertion failure and is returned unchanged for
    the caller's failure record — never downgraded to NOT RUN. Single,
    nested layers and every other call path share this one classifier.
    """
    if candidates and candidates[0].get("deferred"):
        raise CaseEnvironmentError(
            "deferred acceptance: a normal instruction executed before the "
            "interrupt was accepted (observed boundary PC 0x%06x, "
            "SPX 0x%04x); the frozen fixture must guarantee immediate "
            "acceptance at the injection boundary"
            % (candidates[0]["pc"], candidates[0]["spx"])
        )
    return candidates


def walk_to_main_loop(rsp_client, deadline, protected, budget=CRT_WALK_BUDGET):
    """Single-step from reset through CRT init to a real main-loop boundary.

    The acceptance baseline must never be "leave reset and inject": the
    harness single-steps through HOME/BOOT (the `protected` ranges from the
    map) and only declares the boundary once the PC has left the CRT, is
    revisiting an earlier PC (a real loop) and SPX has been stable for
    MAIN_LOOP_STABLE_STEPS consecutive observations. The returned snapshot
    is the injection boundary: its SPX is S and its PC is the real
    interrupted return PC. Budget exhaustion is a fixture prerequisite
    failure, never a PASS and never guessed.
    """
    seen_pcs = set()
    last_spx = None
    stable = 0
    state = take_snapshot(rsp_client, timeout=_remaining(deadline))
    for step in range(1, budget + 1):
        pc, spx = state["PC"], state["SPX"]
        stable = stable + 1 if spx == last_spx else 0
        last_spx = spx
        if (step >= MAIN_LOOP_MIN_STEPS and stable >= MAIN_LOOP_STABLE_STEPS
                and pc in seen_pcs and not _addr_in_ranges(pc, protected)):
            return state, step - 1
        seen_pcs.add(pc)
        rsp_client.step(timeout=_remaining(deadline))
        state = take_snapshot(rsp_client, timeout=_remaining(deadline))
    raise CaseEnvironmentError(
        "no stable application main-loop boundary within %d single steps; "
        "the fixture must loop in CSEG outside the HOME/BOOT sections"
        % budget
    )


def _addr_in_ranges(addr, ranges):
    return any(lo <= addr < hi for lo, hi in ranges)


def require_ea_enabled(rsp_client, deadline):
    """A disabled fixture cannot qualify interrupt acceptance in any case."""
    ie = rsp_client.read_sfr(rsp.SFR_IE, timeout=_remaining(deadline))
    if not ie & IE_EA_BIT:
        raise CaseEnvironmentError(
            "EA is clear at the main-loop boundary; the fixture image must "
            "enable interrupts (IE.EA plus the slot masks) — fixture "
            "prerequisite mismatch, not a model capability limit"
        )


def observe_one_injection(rsp_client, qtest_client, slot, before, deadline,
                          collect_windows=True, require_body_writes=False,
                          bit_require_change=True):
    """Inject one IRQ at `slot` from `before` and observe the full layer.

    `before` is a full snapshot at a real main-loop instruction boundary
    (walk_to_main_loop), so S = before["SPX"] and the return PC is the real
    interrupted PC. Sequence: raise; verify first-step acceptance and frame
    content (step_until_irq_accepted); lower the line; verify the A5 vector EJMP
    form (frozen baseline — mismatch is BASELINE_MISMATCH); single-step the
    granular A6 save window down to exactly S+41; single-step the body
    (any local frame F, helper ECALL/ERET — SPX excursions are observed and
    only max SPX is reported) and the exact inverse restore window to RETI
    at SPX == S; compare the full boundary snapshot; prove shared bit-byte
    changes survive the RETI.

    Returns (assertion_failures, evidence). Fields that could not be read
    are listed in evidence["not_compared"] — reported, never treated as
    equal and never fabricated (T10 card step 8).
    """
    s = before["SPX"]
    failures = []
    evidence = {"slot": slot, "trigger": "qtest-set_irq_in",
                "base_spx": s, "boundary_pc": before["PC"]}
    qtest_client.raise_irq(QOM_CPU_PATH, slot, timeout=_remaining(deadline))
    accept_steps, accepted, main_pcs, candidates = step_until_irq_accepted(
        rsp_client, deadline
    )
    evidence["accept_steps"] = accept_steps
    evidence["main_pcs_before_accept"] = main_pcs[-4:]
    if accepted is None:
        qtest_client.lower_irq(QOM_CPU_PATH, slot, timeout=_remaining(deadline))
        candidates = _classify_non_acceptance(candidates)
        failures.append(
            {"field": "hardware-frame-content@0x%06x"
                      % (s + HARDWARE_FRAME_ADDR_OFFSET),
             "before": candidates[0]["expected"],
             "after": candidates[0]["frame"]}
        )
        evidence["frame_candidates"] = candidates
        return failures, evidence
    qtest_client.lower_irq(QOM_CPU_PATH, slot, timeout=_remaining(deadline))
    evidence["hardware_frame"] = {
        "addr": s + HARDWARE_FRAME_ADDR_OFFSET,
        "bytes": hardware_frame_bytes(before["PSW1"], before["PC"]).hex(),
        "match": True,
    }

    # A5 frozen baseline: the vector slot must hold the EJMP form. The
    # target bytes are proven against where the CPU actually lands after
    # the EJMP executes (below) — the acceptance stop itself sits *at* the
    # vector when the EJMP has not executed yet, so comparing the target
    # against that stop's PC would be meaningless.
    vector = vector_addr(slot)
    vec_bytes = rsp_client.read_memory(vector, 4, timeout=_remaining(deadline))
    if vec_bytes[0] != 0x8A:
        raise FrozenBaselineError(
            "vector @0x%06x is %s, expected 8A + 3-byte target"
            % (vector, vec_bytes.hex())
        )
    ejmp_target = int.from_bytes(vec_bytes[1:4], "big")
    evidence["vector"] = {"addr": vector, "bytes": vec_bytes.hex(),
                          "target": ejmp_target}

    # Normalize the entry stop: some stops land before the vector EJMP
    # executes (PC == vector address); step once through it so the entry
    # state is always the first ISR instruction.
    windows = []
    entry = accepted
    if entry["PC"] == vector:
        rsp_client.step(timeout=_remaining(deadline))
        after_vector = take_snapshot(rsp_client, timeout=_remaining(deadline))
        if after_vector["SPX"] != entry["SPX"]:
            failures.append({"field": "SPX@vector-EJMP",
                             "before": entry["SPX"],
                             "after": after_vector["SPX"]})
        if after_vector["PC"] != ejmp_target:
            raise FrozenBaselineError(
                "vector @0x%06x targets 0x%06x but the CPU landed at 0x%06x"
                % (vector, ejmp_target, after_vector["PC"])
            )
        windows.append({"window": "vector-before", "pc": entry["PC"],
                        "spx": entry["SPX"], "regs": entry})
        windows.append({"window": "vector-after", "pc": after_vector["PC"],
                        "spx": after_vector["SPX"], "regs": after_vector})
        entry = after_vector
    else:
        # The stop already sits past the EJMP: it must be the target.
        if entry["PC"] != ejmp_target:
            raise FrozenBaselineError(
                "vector @0x%06x targets 0x%06x but the CPU entered at "
                "0x%06x" % (vector, ejmp_target, entry["PC"])
            )
        windows.append({"window": "vector-after", "pc": entry["PC"],
                        "spx": entry["SPX"], "regs": entry})

    save_failures, save_windows, body_entry = walk_save_window(
        rsp_client, s, deadline, entry, collect_windows=collect_windows
    )
    failures.extend(save_failures)
    windows.extend(save_windows)
    if body_entry is None:
        if collect_windows:
            evidence["windows"] = windows
        return failures, evidence

    bit_watch = BitWatch(rsp_client, deadline, require_change=bit_require_change)
    bit_watch.start()
    body_failures, final, meta = walk_body_and_restore(
        rsp_client, s, deadline, before, collect_windows=collect_windows,
        require_body_writes=require_body_writes, bit_watch=bit_watch
    )
    failures.extend(body_failures)
    bit_failures, bit_evidence = bit_watch.finish()
    failures.extend(bit_failures)
    evidence["bit_area"] = bit_evidence
    evidence["local_bytes"] = meta["local_bytes"]
    evidence["max_spx"] = meta["max_spx"]
    evidence["body_register_writes"] = meta["body_register_writes"]

    mismatches, not_compared = compare_snapshots(before, final)
    failures.extend(mismatches)
    evidence["not_compared"] = not_compared
    if collect_windows:
        windows.extend(meta["windows"]["body"])
        windows.extend(meta["windows"]["restore"])
        evidence["windows"] = windows
    return failures, evidence


def walk_save_window(rsp_client, s, deadline, entry_state,
                     collect_windows=True):
    """Single-step from the first ISR instruction through the save window.

    Consumes the granular A6 points psw_saved..dpx_saved (PSW +1 byte, then
    DR0..DR28/DPX +4 bytes each), ending exactly at S+41. Every SPX change
    must land exactly on the next granular point; zero-delta stops are
    tolerated and recorded (they move no frame bytes); any descent or
    non-matching increase is an spx-mismatch failure and aborts the walk.
    The save anchor is the first boundary at S+41 with all ten pushes
    consumed — never the run's max SPX, so any local frame size F is
    supported without misreporting software_saved.

    Returns (failures, windows, body_entry_state_or_None).
    """
    points = expected_save_window_points(s)
    granular = list(points[1:1 + len(SAVE_SEQUENCE)])
    failures = []
    windows = []
    state = entry_state
    index = 0
    steps = 0
    while index < len(granular):
        if steps >= MAX_ISR_STEPS:
            raise CaseTimeout(
                "save window did not complete within %d single steps"
                % MAX_ISR_STEPS
            )
        rsp_client.step(timeout=_remaining(deadline))
        after = take_snapshot(rsp_client, timeout=_remaining(deadline))
        steps += 1
        expected_phase, expected_spx = granular[index]
        if after["SPX"] == state["SPX"]:
            if collect_windows:
                windows.append({"window": "save-zero-delta", "step": steps,
                                "pc": after["PC"], "spx": after["SPX"]})
            state = after
            continue
        if after["SPX"] == expected_spx:
            if collect_windows:
                windows.append({"window": expected_phase, "step": steps,
                                "pc": after["PC"], "spx": after["SPX"],
                                "regs": after})
            state = after
            index += 1
            continue
        failures.append({"field": "SPX@%s" % expected_phase,
                         "before": expected_spx, "after": after["SPX"]})
        return failures, windows, None
    return failures, windows, state


def walk_body_and_restore(rsp_client, s, deadline, reference,
                          collect_windows=False, require_body_writes=False,
                          bit_watch=None):
    """Single-step from save-complete (SPX == S+41) through body and RETI.

    Body SPX excursions are free-form — any local frame size F >= 0 and any
    helper ECALL/ERET depth are legal (A6: S -> +4 -> +41 -> +41+F -> +41
    -> +4 -> S); only max SPX is reported, never assumed to be the save
    anchor. The restore ledger anchors at the first observed SPX below
    S+41, which must be exactly the first restore point (dpx_restored,
    S+37); from there every step must land on the granular restore chain
    (dpx_restored..dr0_restored, psw_restored at S+4) and the next step
    must be RETI at SPX == S.

    require_body_writes=True (the nested high layer) additionally requires
    the body to have written at least one sentinel register (R0-R31 /
    R56-R63): a layer that only pushes and pops without ever touching the
    live register set is a fixture gap and fails — never an empty pass
    (T10 card step 10).

    Returns (failures, final_state, meta).
    """
    restore_points = list(
        expected_save_window_points(s)[1 + len(SAVE_SEQUENCE):]
    )
    failures = []
    windows = {"body": [], "restore": []}
    state = take_snapshot(rsp_client, timeout=_remaining(deadline))
    if state["SPX"] < s + FRAME_LAYER_BYTES:
        failures.append(
            {"field": "SPX@body-entry", "before": s + FRAME_LAYER_BYTES,
             "after": state["SPX"]}
        )
        return failures, state, {"windows": windows, "max_spx": state["SPX"],
                                 "local_bytes": 0, "body_register_writes": []}
    max_spx = state["SPX"]
    body_wrote = set()
    phase = "body"
    restore_index = 0
    steps = 0
    while True:
        if steps >= MAX_ISR_STEPS:
            raise CaseTimeout(
                "ISR did not RETI within %d single steps" % MAX_ISR_STEPS
            )
        if phase == "body":
            body_wrote.update(
                name for name in SENTINEL_REGISTERS
                if name in state and state[name] != reference.get(name)
            )
            if bit_watch is not None:
                bit_watch.sample()
            if collect_windows:
                changed = sorted(
                    name for name in SNAPSHOT_FIELDS
                    if name in state and name not in ("PC", "SPX")
                    and state[name] != reference.get(name)
                )
                windows["body"].append(
                    {"window": "body-step", "step": steps + 1,
                     "pc": state["PC"], "spx": state["SPX"],
                     "regs_changed_vs_reference": changed}
                )
                if state["SPX"] == max_spx:
                    windows["body"].append(
                        {"window": "body-maximum", "pc": state["PC"],
                         "spx": state["SPX"], "regs": state}
                    )
        rsp_client.step(timeout=_remaining(deadline))
        after = take_snapshot(rsp_client, timeout=_remaining(deadline))
        steps += 1
        max_spx = max(max_spx, after["SPX"])
        if phase == "body":
            if after["SPX"] >= s + FRAME_LAYER_BYTES:
                state = after
                continue
            if not restore_points or after["SPX"] != restore_points[0][1]:
                failures.append(
                    {"field": "SPX@restore-anchor",
                     "before": restore_points[0][1] if restore_points
                     else s + FRAME_LAYER_BYTES - 4,
                     "after": after["SPX"]}
                )
                return failures, after, _body_meta(windows, max_spx, s,
                                                   body_wrote)
            phase = "restore"
            state = after
            if collect_windows:
                windows["restore"].append(
                    {"window": restore_points[0][0], "pc": after["PC"],
                     "spx": after["SPX"], "regs": after}
                )
            # The anchor observation itself consumed restore_points[0]
            # (dpx_restored); the next pop must land on restore_points[1].
            restore_index = 1
            continue
        if phase == "restore":
            if after["SPX"] == state["SPX"]:
                state = after
                continue
            expected_phase, expected_spx = restore_points[restore_index]
            if after["SPX"] == expected_spx:
                state = after
                if collect_windows:
                    windows["restore"].append(
                        {"window": expected_phase, "pc": after["PC"],
                         "spx": after["SPX"], "regs": after}
                    )
                restore_index += 1
                if restore_index == len(restore_points):
                    phase = "reti"
                continue
            failures.append(
                {"field": "SPX@%s" % expected_phase, "before": expected_spx,
                 "after": after["SPX"]}
            )
            return failures, after, _body_meta(windows, max_spx, s,
                                               body_wrote)
        # phase == "reti": the very next stop must be RETI back at S.
        if after["SPX"] != s:
            failures.append(
                {"field": "SPX@reti_done", "before": s, "after": after["SPX"]}
            )
            return failures, after, _body_meta(windows, max_spx, s,
                                               body_wrote)
        state = after
        break
    if collect_windows:
        windows["body"].append(
            {"window": "reti-done", "pc": state["PC"], "spx": state["SPX"],
             "regs": state}
        )
    if require_body_writes and not (body_wrote & SENTINEL_REGISTERS):
        # T10 card step 10: the high layer must actually use different
        # register values — a layer that only pushes and pops without ever
        # clobbering the live register set proves nothing.
        failures.append(
            {"field": "high-layer-sentinel-writes",
             "before": "(no R0-R31/R56-R63 write observed in the layer body)",
             "after": "(fixture ISR must actually clobber registers, not "
                      "just push/pop)"}
        )
    return failures, state, _body_meta(windows, max_spx, s, body_wrote)


def _body_meta(windows, max_spx, s, body_wrote):
    return {"windows": windows, "max_spx": max_spx,
            "local_bytes": max(max_spx - (s + FRAME_LAYER_BYTES), 0),
            "body_register_writes": sorted(body_wrote)}


def _remaining(deadline):
    left = deadline - CASE_DEADLINE_SOURCE()
    if left <= 0:
        raise CaseTimeout("case deadline exceeded")
    return left


def _run_nested_case(session, deadline):
    """Two-level takeover observed window-by-window on every layer.

    main -> low -> high -> low -> main. The low slot is raised at the real
    main-loop boundary; its granular save window is single-stepped to
    exactly S_low+41 and the high slot is raised at that takeover boundary.
    The high layer is then observed through its own complete window set
    (vector before/after, every save point, body, every restore point,
    RETI) with full register snapshots at each prescribed window, and must
    actually clobber sentinel registers (no empty push/pop). After the high
    RETI the interrupted low takeover boundary is compared in full, the low
    layer's remaining body and restore window are single-stepped, and the
    main boundary is compared in full.

    Requires the fixture image to configure IE (EA plus slot masks) and
    IP/IPH so slot 3 (Timer1) can preempt slot 1 (Timer0) — T10 card step
    15: fixture configuration, not an automatic priority feature. EA clear
    at the boundary is a fixture prerequisite mismatch (NOT RUN), not a
    model capability limit.
    """
    low_slot, high_slot = 1, 3
    rsp_client, qtest_client = connect_and_probe(session, low_slot)
    try:
        qtest_client.assert_irq_input_exists(
            QOM_CPU_PATH, high_slot, timeout=_remaining(deadline)
        )
    except qtest.QTestCommandError as exc:
        raise ModelCapabilityError(
            "CPU IRQ input %d not available: %s" % (high_slot, exc.reason)
        ) from None

    protected = protected_crt_ranges(
        parse_map_sections(session.manifest["map"])
    )
    before_main, walk_steps = walk_to_main_loop(
        rsp_client, deadline, protected
    )
    require_ea_enabled(rsp_client, deadline)

    failures = []
    evidence = {"low_slot": low_slot, "high_slot": high_slot,
                "trigger": "qtest-set_irq_in",
                "boundary": {"pc": before_main["PC"],
                             "spx": before_main["SPX"],
                             "walk_steps": walk_steps,
                             "regs": before_main}}
    s_low = before_main["SPX"]

    # --- main -> low: accept, frame content, vector baseline, save window --
    qtest_client.raise_irq(QOM_CPU_PATH, low_slot,
                           timeout=_remaining(deadline))
    _, low_accepted, _, low_candidates = step_until_irq_accepted(
        rsp_client, deadline
    )
    if low_accepted is None:
        qtest_client.lower_irq(QOM_CPU_PATH, low_slot,
                               timeout=_remaining(deadline))
        low_candidates = _classify_non_acceptance(low_candidates)
        evidence["low_frame_candidates"] = low_candidates
        failures.append(
            {"field": "hardware-frame-content@low@0x%06x"
                      % (s_low + HARDWARE_FRAME_ADDR_OFFSET),
             "before": low_candidates[0]["expected"],
             "after": low_candidates[0]["frame"]}
        )
        return failures, evidence
    qtest_client.lower_irq(QOM_CPU_PATH, low_slot,
                           timeout=_remaining(deadline))
    evidence["low_frame"] = hardware_frame_bytes(
        before_main["PSW1"], before_main["PC"]).hex()

    low_windows = []
    low_entry = low_accepted
    if low_entry["PC"] == vector_addr(low_slot):
        rsp_client.step(timeout=_remaining(deadline))
        after_vector = take_snapshot(rsp_client, timeout=_remaining(deadline))
        if after_vector["SPX"] != low_entry["SPX"]:
            failures.append({"field": "SPX@low-vector-EJMP",
                             "before": low_entry["SPX"],
                             "after": after_vector["SPX"]})
        low_windows.append({"window": "low-vector-before",
                            "pc": low_entry["PC"], "spx": low_entry["SPX"],
                            "regs": low_entry})
        low_windows.append({"window": "low-vector-after",
                            "pc": after_vector["PC"],
                            "spx": after_vector["SPX"],
                            "regs": after_vector})
        low_entry = after_vector
    else:
        low_windows.append({"window": "low-vector-after",
                            "pc": low_entry["PC"], "spx": low_entry["SPX"],
                            "regs": low_entry})
    vec_bytes = rsp_client.read_memory(vector_addr(low_slot), 4,
                                       timeout=_remaining(deadline))
    if vec_bytes[0] != 0x8A or int.from_bytes(vec_bytes[1:4], "big") \
            != low_entry["PC"]:
        raise FrozenBaselineError(
            "low-slot vector @0x%06x is %s, expected 8A + target 0x%06x"
            % (vector_addr(low_slot), vec_bytes.hex(), low_entry["PC"])
        )
    evidence["low_vector"] = {"addr": vector_addr(low_slot),
                              "bytes": vec_bytes.hex()}

    save_failures, low_save_windows, takeover = walk_save_window(
        rsp_client, s_low, deadline, low_entry, collect_windows=True
    )
    failures.extend(save_failures)
    low_windows.extend(low_save_windows)
    if takeover is None:
        low_windows.append({"window": "low-save-incomplete"})
        evidence["low_windows"] = low_windows
        return failures, evidence
    s_high = takeover["SPX"]  # high layer anchors exactly here (A6)
    evidence["base_spx"] = s_low
    evidence["high_base_spx"] = s_high

    # Low-layer bit watch (R2): the baseline MUST be captured before the
    # high layer runs. Starting it after the high RETI would fold the high
    # layer's shared-bit modifications into the low layer's baseline, and a
    # low restore path that clears them would pass silently. With the
    # baseline taken here, the first continuation sample observes the
    # high's modifications against the low entry state and the rollback
    # check protects them through the low RETI.
    low_bit = BitWatch(rsp_client, deadline, require_change=False)
    low_bit.start()

    # --- low -> high: accept and observe the high layer completely --------
    qtest_client.raise_irq(QOM_CPU_PATH, high_slot,
                           timeout=_remaining(deadline))
    _, high_accepted, _, high_candidates = step_until_irq_accepted(
        rsp_client, deadline
    )
    if high_accepted is None:
        qtest_client.lower_irq(QOM_CPU_PATH, high_slot,
                               timeout=_remaining(deadline))
        high_candidates = _classify_non_acceptance(high_candidates)
        evidence["high_frame_candidates"] = high_candidates
        failures.append(
            {"field": "hardware-frame-content@high@0x%06x"
                      % (s_high + HARDWARE_FRAME_ADDR_OFFSET),
             "before": high_candidates[0]["expected"],
             "after": high_candidates[0]["frame"]}
        )
        evidence["low_windows"] = low_windows
        return failures, evidence
    qtest_client.lower_irq(QOM_CPU_PATH, high_slot,
                           timeout=_remaining(deadline))
    evidence["high_frame"] = hardware_frame_bytes(
        takeover["PSW1"], takeover["PC"]).hex()

    high_entry = high_accepted
    high_vector_windows = []
    if high_entry["PC"] == vector_addr(high_slot):
        high_vector_windows.append(
            {"window": "high-vector-before", "pc": high_entry["PC"],
             "spx": high_entry["SPX"], "regs": high_entry})
        rsp_client.step(timeout=_remaining(deadline))
        after_vector = take_snapshot(rsp_client, timeout=_remaining(deadline))
        if after_vector["SPX"] != high_entry["SPX"]:
            failures.append({"field": "SPX@high-vector-EJMP",
                             "before": high_entry["SPX"],
                             "after": after_vector["SPX"]})
        high_entry = after_vector
    high_vector_windows.append(
        {"window": "high-vector-after", "pc": high_entry["PC"],
         "spx": high_entry["SPX"], "regs": high_entry})
    vec_bytes = rsp_client.read_memory(vector_addr(high_slot), 4,
                                       timeout=_remaining(deadline))
    if vec_bytes[0] != 0x8A or int.from_bytes(vec_bytes[1:4], "big") \
            != high_entry["PC"]:
        raise FrozenBaselineError(
            "high-slot vector @0x%06x is %s, expected 8A + target 0x%06x"
            % (vector_addr(high_slot), vec_bytes.hex(), high_entry["PC"])
        )
    evidence["high_vector"] = {"addr": vector_addr(high_slot),
                               "bytes": vec_bytes.hex()}

    high_save_failures, high_save_windows, high_body = walk_save_window(
        rsp_client, s_high, deadline, high_entry, collect_windows=True
    )
    failures.extend(high_save_failures)
    if high_body is None:
        evidence["high_windows"] = {"vector": high_vector_windows,
                                    "save": high_save_windows,
                                    "body": [], "restore": []}
        evidence["low_windows"] = low_windows
        return failures, evidence

    high_bit = BitWatch(rsp_client, deadline, require_change=True)
    high_bit.start()
    high_failures, after_high, high_meta = walk_body_and_restore(
        rsp_client, s_high, deadline, takeover, collect_windows=True,
        require_body_writes=True, bit_watch=high_bit
    )
    failures.extend(high_failures)
    high_bit_failures, high_bit_evidence = high_bit.finish()
    failures.extend(high_bit_failures)
    evidence["high_bit_area"] = high_bit_evidence
    evidence["high_windows"] = {
        "vector": high_vector_windows,
        "save": high_save_windows,
        "body": high_meta["windows"]["body"],
        "restore": high_meta["windows"]["restore"],
        "reti": {"pc": after_high["PC"], "spx": after_high["SPX"],
                 "regs": after_high},
    }
    evidence["high_body_register_writes"] = high_meta["body_register_writes"]

    # --- high -> low: RETI must restore the low takeover boundary ----------
    mismatches, not_compared_high = compare_snapshots(takeover, after_high)
    failures.extend(mismatches)

    # --- low -> main: finish the low body and restore window ---------------
    low_failures, after_low, low_meta = walk_body_and_restore(
        rsp_client, s_low, deadline, before_main, collect_windows=True,
        require_body_writes=False, bit_watch=low_bit
    )
    failures.extend(low_failures)
    low_bit_failures, low_bit_evidence = low_bit.finish()
    failures.extend(low_bit_failures)
    evidence["low_bit_area"] = low_bit_evidence
    mismatches, not_compared_low = compare_snapshots(before_main, after_low)
    failures.extend(mismatches)
    low_windows.extend(low_meta["windows"]["body"])
    low_windows.extend(low_meta["windows"]["restore"])
    low_windows.append({"window": "low-reti-done", "pc": after_low["PC"],
                        "spx": after_low["SPX"], "regs": after_low})
    evidence["low_windows"] = low_windows
    evidence["not_compared"] = {"high_to_low": not_compared_high,
                                "low_to_main": not_compared_low}
    return failures, evidence


def schedule_case_budget(case, iterations, timeout_seconds):
    """Pure per-case iteration/budget scheduler (self-test covered).

    Returns (iterations, budget_seconds). The budget is one timeout_seconds
    unit per scheduled sub-run plus a margin unit; nested-windows costs two
    units per iteration (two layers). Real execution may still finish well
    within the budget — the budget is an upper bound that bounds TIMEOUT,
    never a target to run up to.
    """
    if case == "long-run":
        iters = iterations or LONG_RUN_DEFAULT_ITERATIONS
        units = iters + 1
    elif case == "nested-windows":
        iters = iterations or 1
        units = 2 * iters + 1
    elif case == "default":
        iters = 1
        units = 3
    elif case in ("hardware-frame", "single"):
        iters = iterations or 1
        units = iters + 1
    else:
        raise CaseEnvironmentError("unknown case %r" % case)
    return iters, timeout_seconds * units


def run_case(manifest, case, iterations=None, session_factory=QemuSession):
    """Execute one frozen case against the real model. Returns a record."""
    started = time.time()
    # One scheduler, one absolute monotonic deadline for the whole case.
    iterations, budget = schedule_case_budget(
        case, iterations, manifest["timeout_seconds"]
    )
    deadline = CASE_DEADLINE_SOURCE() + budget
    details = []
    timeout = False
    model_unsupported = False
    baseline_error = None
    assertion_failures = []
    trigger = "qtest-set_irq_in"
    # R3: the record must state how many iterations actually completed
    # (a nested iteration is one complete main->low->high->low->main
    # sequence), never just the requested count.
    completed_iterations = 0

    # Prerequisites first: missing manifest-referenced files (the map
    # included) stop the case before anything could book PASS.
    precheck_environment(manifest, need_default_slot=(case == "default"))

    # Identity: a present file with the wrong sha256 is a frozen-identity
    # finding (BASELINE_MISMATCH); nothing is executed.
    baseline_error = (
        verify_identity(manifest["qemu"]["path"],
                        manifest["qemu"]["sha256"])
        or verify_identity(manifest["image"]["path"],
                           manifest["image"]["sha256"])
    )

    session = None
    if baseline_error is None:
        # X_OK is only a meaningful predicate on POSIX; the qualification
        # runs happen under WSL. On a Windows host os.access(X_OK) depends
        # on filename patterns and would reject valid manifests.
        if os.name == "posix" and not os.access(
                manifest["qemu"]["path"], os.X_OK):
            raise CaseEnvironmentError(
                "QEMU binary is not executable: %s"
                % manifest["qemu"]["path"]
            )
        session = session_factory(manifest, deadline)

    try:
        if session is None:
            # Identity failed: record it, execute nothing.
            details.append({"baseline_error": baseline_error})
        elif case == "default":
            mismatches, evidence, notes = _run_default_case(session,
                                                            deadline)
            details.extend(notes)
            assertion_failures.extend(mismatches)
            details.append({"failures": mismatches, "evidence": evidence})
            if not mismatches:
                completed_iterations = 1
        elif case == "nested-windows":
            for iteration in range(iterations):
                nested_failures, evidence = _run_nested_case(session,
                                                             deadline)
                assertion_failures.extend(nested_failures)
                details.append({"iteration": iteration + 1,
                                "failures": nested_failures,
                                "evidence": evidence})
                if nested_failures:
                    break
                completed_iterations += 1
        elif case in ("hardware-frame", "single", "long-run"):
            slot = 1
            rsp_client, qtest_client = connect_and_probe(session, slot)
            protected = protected_crt_ranges(
                parse_map_sections(manifest["map"])
            )
            boundary, walk_steps = walk_to_main_loop(
                rsp_client, deadline, protected
            )
            require_ea_enabled(rsp_client, deadline)
            details.append(
                {"boundary_walk": {"steps": walk_steps,
                                   "pc": boundary["PC"],
                                   "spx": boundary["SPX"]}}
            )
            for iteration in range(iterations):
                before = take_snapshot(rsp_client, timeout=_remaining(deadline))
                iteration_failures, evidence = observe_one_injection(
                    rsp_client, qtest_client, slot, before,
                    deadline=deadline,
                    collect_windows=(iteration == 0),
                    require_body_writes=False,
                    bit_require_change=(iteration == 0),
                )
                assertion_failures.extend(iteration_failures)
                details.append(
                    {"iteration": iteration + 1, "slot": slot,
                     "trigger": trigger,
                     "failures": iteration_failures,
                     "evidence": None if iteration else evidence}
                )
                if assertion_failures and case != "long-run":
                    break
                if not iteration_failures:
                    completed_iterations += 1
        else:
            raise CaseEnvironmentError("unknown case %r" % case)
    except CaseTimeout:
        timeout = True
    except (rsp.RspTimeout, qtest.QTestTimeout) as exc:
        # Transport-level timeouts of every kind book as TIMEOUT, with the
        # highest classification priority: a timeout is never a PASS.
        timeout = True
        details.append({"transport_timeout": str(exc)})
    except ModelCapabilityError as exc:
        model_unsupported = True
        details.append({"model_unsupported": str(exc)})
    except FrozenBaselineError as exc:
        baseline_error = str(exc)
        details.append({"baseline_error": baseline_error})
    except CaseEnvironmentError:
        raise
    finally:
        if session is not None:
            session.close()

    result = classify_result(
        timeout=timeout,
        model_unsupported=model_unsupported,
        baseline_error=baseline_error,
        assertion_failures=assertion_failures,
    )
    return {
        "schema": 1,
        "case": case,
        "iterations": iterations,
        "completed_iterations": completed_iterations,
        "trigger": trigger,
        "result": result,
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(started)),
        "finished_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "identity": {
            "machine": manifest["machine"],
            "qemu_sha256": manifest["qemu"]["sha256"],
            "image_sha256": manifest["image"]["sha256"],
        },
        "details": details,
    }


def _run_default_case(session, deadline):
    """Default-source checks (T10 card step 14): 4B hardware entry only.

    Injects on slot 6 (LVD — legal, not wired to a device by the SoC, and
    unregistered in fixtures) and verifies by single-stepping only:

      * the DEFAULT *function* address is resolved through the record
        association the linker verified: every map-DEFAULT slot's vector
        EJMP target, which must agree across all DEFAULT slots. The map's
        slot address column is the vector slot address (0xFF0003+8*slot),
        never the function address;
      * the DEFAULT entry holds the frozen A5 machine bytes C2AF80FE
        (clr EA; sjmp halt) — a frozen baseline check;
      * exactly the 4-byte hardware frame is pushed — SPX == S+4 and it
        stays there (no software stack);
      * IE.EA reads cleared *after the clr EA instruction has actually
        executed* (the sample point is the boundary after the first default
        entry instruction, not the entry boundary itself);
      * the PC is stable in the terminate loop and never RETIs, never
        leaves the 4-byte default entry.

    No QEMU free-running: every transition is a single-step stop, and the
    injection baseline is a real main-loop boundary, not leave-reset.
    """
    slot = 6
    irq_map = parse_irq_map(session.manifest["map"])
    if slot not in default_slots(irq_map):
        raise CaseEnvironmentError(
            "map does not classify slot %d as DEFAULT; the default case "
            "requires an unregistered legal slot" % slot
        )
    rsp_client, qtest_client = connect_and_probe(session, slot)
    slots = default_slots(irq_map)

    # Resolve the DEFAULT function address via the vector EJMP targets of
    # all map-DEFAULT slots; they must agree on one address.
    targets = {}
    for default_slot in slots:
        vec = vector_addr(default_slot)
        vec_bytes = rsp_client.read_memory(vec, 4, timeout=_remaining(deadline))
        if vec_bytes[0] != 0x8A:
            raise FrozenBaselineError(
                "DEFAULT vector @0x%06x is %s, expected 8A + target"
                % (vec, vec_bytes.hex())
            )
        targets[default_slot] = int.from_bytes(vec_bytes[1:4], "big")
    if len(set(targets.values())) != 1:
        raise FrozenBaselineError(
            "DEFAULT slots disagree on the default entry target: %s"
            % {slot: "0x%06x" % target for slot, target in targets.items()}
        )
    default_entry = next(iter(set(targets.values())))

    protected = protected_crt_ranges(
        parse_map_sections(session.manifest["map"])
    )
    before, walk_steps = walk_to_main_loop(rsp_client, deadline, protected)
    require_ea_enabled(rsp_client, deadline)
    s = before["SPX"]

    failures = []
    qtest_client.raise_irq(QOM_CPU_PATH, slot, timeout=_remaining(deadline))
    _, entry, _, entry_candidates = step_until_irq_accepted(
        rsp_client, deadline
    )
    if entry is None:
        qtest_client.lower_irq(QOM_CPU_PATH, slot, timeout=_remaining(deadline))
        entry_candidates = _classify_non_acceptance(entry_candidates)
        return [{"field": "hardware-frame-content@default@0x%06x"
                          % (s + HARDWARE_FRAME_ADDR_OFFSET),
                 "before": entry_candidates[0]["expected"],
                 "after": entry_candidates[0]["frame"]}], {
                     "frame_candidates": entry_candidates}, []

    qtest_client.lower_irq(QOM_CPU_PATH, slot, timeout=_remaining(deadline))

    # The injected slot's own vector must target the same resolved default
    # entry; normalize through the EJMP so PC sits at the default entry's
    # first instruction (clr EA, not yet executed).
    vector = vector_addr(slot)
    if targets.get(slot, default_entry) != default_entry:
        raise FrozenBaselineError(
            "injected slot %d vector targets 0x%06x, other DEFAULT slots "
            "target 0x%06x" % (slot, targets.get(slot), default_entry)
        )
    if entry["PC"] == vector:
        rsp_client.step(timeout=_remaining(deadline))
        after_vector = take_snapshot(rsp_client, timeout=_remaining(deadline))
        if after_vector["SPX"] != entry["SPX"]:
            failures.append({"field": "SPX@vector-EJMP",
                             "before": entry["SPX"],
                             "after": after_vector["SPX"]})
        entry = after_vector

    # 1. Entered at the resolved DEFAULT entry function address. The vector
    #    target bytes and the CPU's observed landing must agree.
    if entry["PC"] != default_entry:
        raise FrozenBaselineError(
            "slot %d vector targets 0x%06x but the CPU entered the default "
            "path at 0x%06x" % (slot, default_entry, entry["PC"])
        )
    # 2. Frozen A5 machine bytes at the DEFAULT entry (baseline check).
    image_bytes = rsp_client.read_memory(default_entry,
                                         len(DEFAULT_ENTRY_BYTES),
                                         timeout=_remaining(deadline))
    if image_bytes != DEFAULT_ENTRY_BYTES:
        raise FrozenBaselineError(
            "default entry @0x%06x is %s, expected %s"
            % (default_entry, image_bytes.hex(), DEFAULT_ENTRY_BYTES.hex())
        )
    # 3. Only the 4B hardware frame: SPX == S+4 at entry and it stays there.
    spx_at_entry = entry["SPX"]
    if spx_at_entry != s + FRAME_HARDWARE_BYTES:
        failures.append({"field": "SPX", "before": s + FRAME_HARDWARE_BYTES,
                         "after": spx_at_entry})
    # 4. Execute clr EA (one step), *then* sample IE.EA. Sampling before the
    #    instruction executes would flag a correct fail-stop entry.
    rsp_client.step(timeout=_remaining(deadline))
    after_clr = take_snapshot(rsp_client, timeout=_remaining(deadline))
    if after_clr["SPX"] != spx_at_entry:
        failures.append({"field": "SPX-in-default", "before": spx_at_entry,
                         "after": after_clr["SPX"]})
    ie = rsp_client.read_sfr(rsp.SFR_IE, timeout=_remaining(deadline))
    if ie & IE_EA_BIT:
        failures.append({"field": "IE.EA", "before": 0, "after": 1})
    # 5. PC stable in the terminate loop; no RETI, no stack movement.
    pcs = [entry["PC"], after_clr["PC"]]
    for _ in range(2):
        rsp_client.step(timeout=_remaining(deadline))
        state = take_snapshot(rsp_client, timeout=_remaining(deadline))
        pcs.append(state["PC"])
        if state["SPX"] != spx_at_entry:
            failures.append({"field": "SPX-in-default",
                             "before": spx_at_entry,
                             "after": state["SPX"]})
        if not default_entry <= state["PC"] < default_entry + 4:
            failures.append({"field": "PC-in-default",
                             "before": default_entry,
                             "after": state["PC"]})
    if pcs[-1] != pcs[-2]:
        failures.append({"field": "PC-stable", "before": pcs[-2],
                         "after": pcs[-1]})

    evidence = {"slot": slot, "default_entry": default_entry,
                "default_slots": slots, "vector": vector, "pcs": pcs,
                "spx": [s, spx_at_entry],
                "entry_bytes": image_bytes.hex(),
                "boundary": {"pc": before["PC"], "spx": s,
                             "walk_steps": walk_steps},
                "trigger": "qtest-set_irq_in"}
    notes = [{"entry_bytes": image_bytes.hex()}]
    return failures, evidence, notes


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def write_result_record(manifest, record):
    out_dir = os.path.join(manifest["output_dir"], "results")
    os.makedirs(out_dir, exist_ok=True)
    name = "%s-%s.json" % (record["case"],
                           time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()))
    path = os.path.join(out_dir, name)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(record, handle, indent=2, sort_keys=True)
        handle.write("\n")
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="MCS251 ISR model qualification (T10)")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--self-test", action="store_true",
                      help="run the fake-transport self-test (no QEMU)")
    mode.add_argument("--manifest", metavar="ABS_JSON",
                      help="qualification manifest (frozen schema)")
    parser.add_argument("--case", choices=CASES,
                        help="qualification case to run")
    parser.add_argument("--iterations", type=int, default=None,
                        help="injection count (long-run default %d)"
                             % LONG_RUN_DEFAULT_ITERATIONS)
    args = parser.parse_args(argv)

    if args.self_test:
        ok = SelfTest().run()
        return EXIT_OK if ok else EXIT_FAILED

    if not args.case or not args.manifest:
        parser.error("--manifest mode requires --case (and vice versa)")

    try:
        manifest = load_manifest(args.manifest, require_runtime_ready=True)
    except (OSError, ValueError) as exc:
        print("manifest unreadable: %s" % exc, file=sys.stderr)
        return EXIT_USAGE
    except ManifestError as exc:
        print("manifest invalid: %s" % exc, file=sys.stderr)
        return EXIT_USAGE

    if args.iterations is not None and args.iterations < 1:
        parser.error("--iterations must be >= 1")

    try:
        record = run_case(manifest, args.case, iterations=args.iterations)
    except CaseEnvironmentError as exc:
        print("NOT RUN (%s): %s" % (args.case, exc), file=sys.stderr)
        return EXIT_PREREQ_MISSING

    path = write_result_record(manifest, record)
    print("CASE %s RESULT %s (record: %s)"
          % (record["case"], record["result"], path))
    return EXIT_OK if record["result"] == RESULT_PASS else EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
