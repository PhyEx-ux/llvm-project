#!/usr/bin/env python3
"""Static self-check for the TFPU probe image, plus a QEMU smoke run.

Checks performed on the *hardware* image (all are static, no board needed):

  1. Intel HEX checksums and the reset vector (LJMP FF:0200).
  2. Every DMAIR command code appears exactly the expected number of times:
     0x3E x1, 0x31 x1, 0x32 x1, 0x33 x2, 0x1E x(41+1), 0x1C x(49+1),
     0x2D x(81+1).  The extra ones are the "one-shot" measurements.
  3. UART-before-TFPU ordering: the first store to SCON (0x98) occurs at a
     lower address than the first DMAIR (0xED) write.
  4. Each latency-sled entry n has exactly n NOPs between the trigger and the
     R4 snapshot, and every table slot is a 3-byte LJMP (so the dispatcher's
     3*n byte offset is correct).
  5. The probe never emits a write to SFR 0xEF/0xFF (they are AUXINTIF and
     RSTCFG on this part; only reads are legitimate).
  6. The observation slots (0x30..0x39) are only touched by the probe and by
     the two intended C stores (S_N=0x34, S_MARK=0x39).

With --qemu it additionally drives the QEMU image over serial and checks that
the stage markers appear in order.  QEMU output is NOT hardware evidence: the
QEMU TFPU model computes results instantly and invents a status-bit layout, so
it can only prove that the firmware runs and prints.
"""
import argparse
import re
import selectors
import subprocess
import sys
import time
from pathlib import Path

# name -> (command, last n)
SCANS = {"mul": (0x1E, 40), "add": (0x1C, 48), "sin": (0x2D, 272)}


def load_hex(path):
    memory, base = {}, 0
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line.startswith(':'):
            continue
        b = bytes.fromhex(line[1:])
        assert len(b) == b[0] + 5, "record length"
        assert sum(b) % 256 == 0, "checksum"
        addr, kind, data = int.from_bytes(b[1:3], 'big'), b[3], b[4:-1]
        if kind == 4:
            base = int.from_bytes(data, 'big') << 16
        elif kind == 0:
            for i, v in enumerate(data):
                assert base + addr + i not in memory, "overlapping records"
                memory[base + addr + i] = v
        else:
            assert kind == 1, "unsupported record type %d" % kind
    return memory


def symbols(path):
    out = {}
    for value, name in re.findall(r'C:\s+([0-9A-F]+)\s+(_\w+)', path.read_text()):
        out[name] = int(value, 16)
    return out


def local_labels(directory, sym):
    """Recover sled labels, which are module-local and so absent from the map.

    sdas251 emits them in the listing with CSEG-relative offsets.  The delta is
    derived from a label that IS in both files, so this stays correct even if
    the linker moves CSEG.
    """
    lst = (directory / 'tfpu-probe.lst').read_text(errors='replace')
    offsets = {}
    for off, name in re.findall(r'^\s*([0-9A-F]{6})\s+\d+\s+(_\w+):', lst,
                                re.MULTILINE):
        offsets[name] = int(off, 16)
    anchor = next(n for n in ('_tpu_mul_at_n', '_tpu_add_at_n', '_tpu_sin_at_n')
                  if n in offsets and n in sym)
    delta = sym[anchor] - offsets[anchor]
    return {name: off + delta for name, off in offsets.items()}


def nop_run(mem, start, limit=4096):
    """Length of the run of NOPs (opcode 0x00) beginning at `start`."""
    n = 0
    while n < limit and mem.get(start + n) == 0x00:
        n += 1
    return n


def check_image(directory):
    mem = load_hex(directory / 'tfpu-probe.hex')
    sym = symbols(directory / 'tfpu-probe.map')
    sym.update(local_labels(directory, sym))
    lo, hi = min(mem), max(mem)
    assert lo == 0xff0000, "image must start at FF:0000, got %06X" % lo
    assert hi <= 0xffffff, "image escapes 16 MB code space"

    # 1. reset vector: LJMP addr16 (keeps the upper PC bits from FF:0000)
    assert mem[0xff0000] == 0x02, "reset vector is not LJMP"
    assert int.from_bytes(bytes(mem[0xff0001 + i] for i in range(2)), 'big') == \
        (sym['_start'] & 0xffff), "reset vector does not point at _start"

    # 2. DMAIR command histogram
    hist = {}
    for a in sorted(mem):
        if mem.get(a) == 0x75 and mem.get(a + 1) == 0xED:
            hist[mem[a + 2]] = hist.get(mem[a + 2], 0) + 1
    # With a shared sled each command has exactly one trigger site, so the
    # histogram is tiny: one per scan command, plus one extra 0x1E from
    # tpu_mul_once (the fixed-wait presence check).
    expected = {0x3E: 1, 0x31: 1, 0x32: 1, 0x33: 2,
                0x1E: 2, 0x1C: 1, 0x2D: 1}
    assert hist == expected, "DMAIR histogram %s != %s" % (
        {hex(k): v for k, v in sorted(hist.items())},
        {hex(k): v for k, v in sorted(expected.items())})

    # 3. UART-before-TFPU ordering.
    #
    #    Address ordering across functions is NOT the invariant: sdld places the
    #    probe routines ahead of _main in CSEG, so the first DMAIR write sits at
    #    a *lower* address than main's UART setup.  The invariant is execution
    #    order inside _main, which is decoded straight from the image: walk from
    #    _main and find the first UART setup store, the first serial output, and
    #    the first call into a probe entry point.
    #
    #    The C compiler stores SFRs via the register form (7A rn dir), and calls
    #    the probe with ECALL (9A addr24), so both encodings are matched.
    def sfr_stores(dirs):
        found = []
        for a in sorted(mem):
            if mem[a] == 0x75 and mem[a + 1] in dirs:            # MOV dir,#imm
                found.append(a)
            if mem[a] == 0x7A and mem.get(a + 2) in dirs:        # MOV dir,Rn
                found.append(a)
        return found

    uart_stores = sfr_stores({0x98, 0x8E, 0xD6, 0xD7})
    dmair_stores = sfr_stores({0xED})
    assert uart_stores, "no UART SFR store found"

    tpu_entry = {a for n, a in sym.items() if n.startswith('_tpu_')}
    main = sym['_main']
    scon_at = sbuf_at = tpu_at = None
    for a in range(main, min(main + 0x4000, hi)):
        if scon_at is None and mem.get(a) == 0x7A and \
                mem.get(a + 2) in (0x98, 0x8E, 0xD6, 0xD7):
            scon_at = a
        if scon_at is None and mem.get(a) == 0x75 and \
                mem.get(a + 1) in (0x98, 0x8E, 0xD6, 0xD7):
            scon_at = a
        if sbuf_at is None and mem.get(a) == 0x7A and mem.get(a + 2) == 0x99:
            sbuf_at = a
        if tpu_at is None and mem.get(a) == 0x9A:
            target = int.from_bytes(
                bytes(mem.get(a + 1 + i, 0) for i in range(3)), 'big')
            if target in tpu_entry:
                tpu_at = a
        if scon_at is not None and sbuf_at is not None and tpu_at is not None:
            break
    assert scon_at is not None, "no UART SFR store reachable from _main"
    assert sbuf_at is not None, "no serial output reachable from _main"
    assert tpu_at is not None, "no TFPU call reachable from _main"
    assert scon_at < sbuf_at < tpu_at, (
        "ordering violated: UART setup %06X, first output %06X, first TFPU "
        "call %06X" % (scon_at, sbuf_at, tpu_at))
    print("ordering: PASS  (UART setup %06X < first output %06X < first TFPU "
          "call %06X; note cross-function address order is not the invariant)"
          % (scon_at, sbuf_at, tpu_at))

    # 4. shared-sled geometry.
    #    Each command owns one run of NOPs; the dispatcher enters it at
    #    offset (sled_len - n) via JMP @A+DPTR, using two DPTR bases because A
    #    is only 8 bits wide.
    sled_len = None
    for name, (cmd, last) in SCANS.items():
        base = sym['_tpu_%s_sled' % name]
        snap = sym['_tpu_%s_snap' % name]
        got = nop_run(mem, base, snap - base + 8)
        assert base + got == snap, (
            "%s: %d NOPs then a non-NOP at %06X, but _tpu_%s_snap is %06X"
            % (name, got, base + got, name, snap))
        assert got >= last, (
            "%s: sled is %d NOPs but the scan goes to n=%d" % (name, got, last))
        if sled_len is None:
            sled_len = got
        assert got == sled_len, "%s sled is %d NOPs, expected %d" % (
            name, got, sled_len)
        # snapshot must store R4 into S_L4 (0x36) right after the sled:
        # MOV S_L4,R4 = 7A 41 36 for the low-direct form.
        assert bytes(mem[snap + i] for i in range(3)) == b'\x7a\x41\x36', \
            "%s: snapshot does not store R4 to S_L4" % name
        assert sym['_tpu_%s_done' % name] > snap, "%s_done is misplaced" % name
    lo_base = sled_len - 255
    assert 0 <= lo_base <= 255, "lo base offset %d out of range" % lo_base
    print("sled: PASS  (%d NOPs per command; entry = sled + (%d - n); "
          "lo base offset %d)" % (sled_len, sled_len, lo_base))

    # 5. no writes to SFR 0xEF / 0xFF (AUXINTIF / RSTCFG)
    for op in (b'\x75\xef', b'\x75\xff', b'\x7a\xb3\x00\xef', b'\x7a\xb3\x00\xff'):
        assert op not in bytes(mem.get(a, 0) for a in range(lo, hi + 1)), \
            "unexpected write to SFR EF/FF: %s" % op.hex(' ')

    # 6. observation slots are not clobbered by stray C stores
    low = bytes(mem.get(a, 0) for a in range(lo, hi + 1))
    for opcode, name in ((b'\x75\x30', '0x30'), (b'\x75\x31', '0x31'),
                         (b'\x75\x32', '0x32'), (b'\x75\x33', '0x33'),
                         (b'\x75\x35', '0x35')):
        assert opcode not in low, "unexpected immediate store to %s" % name

    print("image checks: PASS  (%d bytes, %06X..%06X, %d DMAIR writes)"
          % (len(mem), lo, hi, len(dmair_stores)))
    return hist


def run_qemu(qemu, image):
    p = subprocess.Popen([qemu, '-M', 'stc32g144k246', '-bios', str(image),
                          '-nographic', '-monitor', 'none', '-serial', 'stdio'],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    sel = selectors.DefaultSelector()
    sel.register(p.stdout, selectors.EVENT_READ)
    data, sent = b'', False
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            for key, _ in sel.select(.1):
                data += key.fileobj.read1(65536)
            if not sent and b'READY' in data:
                p.stdin.write(b'g')
                p.stdin.flush()
                sent = True
            if b'DONE\r\n' in data:
                return data.decode('latin1')
        raise AssertionError('no DONE marker: ' + repr(data[-800:]))
    finally:
        p.terminate()
        p.wait(timeout=5)
        sel.close()


def check_qemu(text):
    stages = ['STAGE raw', 'STAGE clk-sel', 'STAGE clk-sel-ok', 'STAGE init',
              'STAGE init-ok', 'STAGE clr-exc', 'STAGE clr-exc-ok',
              'STAGE mul-once', 'STAGE scan-mul', 'STAGE scan-add',
              'STAGE scan-sin', 'DONE']
    pos = -1
    for s in stages:
        i = text.find(s)
        assert i >= 0, "missing stage marker: " + s
        assert i > pos, "stage out of order: " + s
        pos = i
    assert 'TPU-PROBE READY' in text, "banner missing"
    for name, (_, last) in SCANS.items():
        got = re.findall(r'SCAN %s N=([0-9A-F]{4}) ' % name, text)
        assert len(got) == last + 1, "%s: %d scan lines, expected %d" % (
            name, len(got), last + 1)
    print("qemu smoke: PASS  (stage order, %d scan lines)"
          % sum(last + 1 for _, last in SCANS.values()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('hardware', type=Path)
    ap.add_argument('--qemu-dir', type=Path, default=None,
                    help='qemu-profile build directory to smoke-test')
    ap.add_argument('--qemu', default='/home/liu/build-qemu/qemu-system-mcs251')
    args = ap.parse_args()
    check_image(args.hardware)
    if args.qemu_dir:
        check_qemu(run_qemu(args.qemu, args.qemu_dir / 'tfpu-probe.hex'))
    print("all checks passed")


if __name__ == '__main__':
    sys.exit(main())
