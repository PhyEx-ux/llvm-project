#!/usr/bin/env python3
"""Byte-exact golden for one MCS251 TFPU hardware sequence (G7 S3, design 2.4).

The complete load-trigger-wait-readback window is one MCS251 function, so for
a whole-symbol input the comparison pins every field of the sequence:

    AR load   `mov dr4, <park>`   -- the AR window half (design step 1)
    [BR load  `mov dr0, <park>`]  -- the BR window half (binary four only)
    `mov 0xED, #<cmd>`            -- the pinned TFPU_TRG bytes 75 ED <cmd>
    `<nops>` x 0x00               -- the fixed worst-case delay chain
    `mov <dst>, dr4`              -- the readback, still INSIDE the window
    epilogue                      -- the DR4-derived lanes into the ABI slots

Structurally (this is what makes ONE golden cover all nine commands): the
sequence is a PROLOGUE + trigger + wait + READBACK + EPILOGUE, where

  * the prologue is the same for every unary command and the same for every
    binary command -- it is the entry/parameter materialisation up to and
    including the two window loads;
  * the trigger bytes are `75 ED <cmd>`;
  * the wait is `<nops>` zero bytes;
  * the readback is `mov <register outside the window>, dr4`;
  * the epilogue is identical for all nine (the return sequence).

The opt-level shape (register allocation) differs, so each shape is pinned
separately (`--shape o0` / `--shape o2`). Both shapes are captured from the
real emitter, never hand-written.

FIELD LOCATION (the round-3 fix). The self-test used to derive its injection
offsets by counting backwards from the end of the prologue (`pre_len - 1`,
`pre_len - 3`, `pre_len - 5`) and to delete the "wait byte" at `pre_len + 2`.
Those offsets were only correct for the pre-readback pseudo shape:

  * for the binary shapes `pre_len - 1` is the last byte of the BR load, not
    the AR load, so the "wrong AR load" injection actually mutated the BR
    load, and the "wrong BR load" one mutated a reload displacement;
  * `good[:pre_len+2] + good[pre_len+3:]` deletes index `pre_len + 2`, i.e.
    the COMMAND CODE byte, not a wait byte.

Every injection is now located by SCANNING the real bytes for the field it
claims to mutate, and the located byte is asserted against that field's
architectural encoding before the mutation. An injection can no longer be
silently aimed at a neighbouring byte, so a passing `--self-test` proves the
six classes really are rejected rather than merely that the comparison bit
some byte.

`--self-test` mutates the REAL bytes the caller just accepted -- the AR
window load source nibble, the BR window load source nibble, the command
code, one wait byte (deleted), the readback source lane, and the sequence
length -- and requires the SAME comparison to fail for each.

Modes:
  --text F.bin --shape o2 --cmd 2d --nops 270 [--binary]
  --obj F.o --symbol _seq --shape o2 --cmd 2d --nops 270 [--binary]
"""

import argparse
import pathlib
import struct
import sys

# The llc register-allocation shape is opt-level dependent and deterministic.
# Each entry is (pre_unary, pre_binary, suf): the fixed bytes before the
# trigger immediate and after the wait chain. `pre_*` ENDS immediately before
# the `75 ED` trigger opcode. For the unary five its last two bytes are the
# AR window load `mov dr4, dr12` (`7f 13`); for the binary four the AR load
# is followed, after the dr12 reload of the BR operand, by the BR window load
# `mov dr0, dr12` (`7f 03`), which abuts the trigger.
#
# All nine commands share one pre_unary (or pre_binary) and one suffix at a
# given opt level: the command is the ONLY difference. Both shapes were read
# back from the emitter's own output (see the test's RUN lines) and are pinned
# as hex literals; changing register allocation on purpose means re-capturing
# them, which is the point of a byte golden.
SHAPES = {
    "o2": {
        "pre_unary": "7ccb7ed1f07ee1837ef1827f13",
        "pre_binary": (
            "0bfe0bfe7ccb7ed1f07ee1837ef182"
            "796ffff9797ffff"
            "b7e0800007a0c00007e0bc029d0000129e0000229f00003"
            "796ffffd797fffff696ffff9697ffff"
            "b7f13696ffffd697fffff7f03"
        ),
        "suf": "7f317af1827ae1837ad1f07cbcaa",
        "suf_binary": "7f317af1827ae1837ad1f07cbc1bfe1bfeaa",
    },
    "o0": {
        "pre_unary": "a5f87e21f07ee1837e11827cf17c127d607f13",
        "pre_binary": (
            "0bfe0bfe0bfe0bfca5fc7e61f07ee1837e5182"
            "7e0800007a0c00007e0b80297000017c97297000027afb7029000003"
            "7efb207c307d0479"
            "0ffffc791ffffe7cf57c567d6279"
            "6ffff8797ffffa696ffffc697ffffe"
            "796ffff4797ffff6696ffff8697ffffa7f13696ffff4697ffff67f03"
        ),
        "suf": "7f317d077c317c207d267c157c047a31827a21837a11f0a5e8aa",
        "suf_binary": "7f317d077c317c207d267c157c047a31827a21837a11f0a5e81bfe1bfe1bfe1bfcaa",
    },
    # Both command operands the SAME value (tfpu_mul(x, x)): the singleton
    # parking class is read twice, so the BR window load is the SAME
    # instruction as the AR one (`7f 03` right after `7f 13`) and the
    # allocator marks only one of the two reads killing. The regression this
    # shape guards is the expand pass forcing kill on both loads, which made
    # the second read a use of an already-killed register and tripped
    # -verify-machineinstrs.
    "o2_same": {
        "pre_unary": "7ccb7ed1f07ee1837ef1827f137f03",
        "pre_binary": "7ccb7ed1f07ee1837ef1827f137f03",
        "suf": "7f317af1827ae1837ad1f07cbcaa",
        "suf_binary": "7f317af1827ae1837ad1f07cbcaa",
    },
    "o0_same": {
        "pre_unary": "a5f87e21f07ee1837e11827cf17c127d607f137f03",
        "pre_binary": "a5f87e21f07ee1837e11827cf17c127d607f137f03",
        "suf": "7f317d077c317c207d267c157c047a31827a21837a11f0a5e8aa",
        "suf_binary": "7f317d077c317c207d267c157c047a31827a21837a11f0a5e8aa",
    },
}

# Architectural encodings this checker asserts on the located fields
# (MCS251MCCodeEmitter.cpp regCode: dr0 -> 0, dr4 -> 1, dr12 -> 3; a 32-bit
# register-register move is 0x7f (dst<<4)|src).
MOV32RR = 0x7F
DR0, DR4, DR12 = 0x0, 0x1, 0x3
TRIG_OP, TRIG_SFR = 0x75, 0xED


class FieldError(AssertionError):
    """A located field does not have the architectural encoding it must."""


def _find_mov32rr(blob, dst, src, before):
    """Offsets < before of a `mov <dst>, <src>` (0x7f dst src) in blob."""
    want = bytes((MOV32RR, (dst << 4) | src))
    return [i for i in range(before - 1) if blob[i : i + 2] == want]


def fields(blob, nops, cmd, binary):
    """Locate every field by scanning the real bytes; return their offsets.

    Raises FieldError if a field is missing or mis-encoded, so a malformed
    artifact fails here instead of being compared byte-for-byte.
    """
    trig = [
        i
        for i in range(len(blob) - 2)
        if blob[i] == TRIG_OP and blob[i + 1] == TRIG_SFR
    ]
    if len(trig) != 1:
        raise FieldError("expected exactly one 75 ED trigger, found %d" % len(trig))
    t = trig[0]
    # The wait chain is the run of zero bytes right after the command code.
    w = t + 3
    e = w
    while e < len(blob) and blob[e] == 0:
        e += 1
    if e - w != nops:
        raise FieldError("wait chain is %d bytes, --nops says %d" % (e - w, nops))
    # The readback is the first instruction after the wait chain:
    # `mov <register outside the window>, dr4`.
    rd = e
    if blob[rd] != MOV32RR or (blob[rd + 1] & 0x0F) != DR4:
        raise FieldError(
            "readback at +%d is %02x %02x, not `mov <r>, dr4`"
            % (rd, blob[rd], blob[rd + 1])
        )
    if (blob[rd + 1] >> 4) == DR4:
        raise FieldError("readback destination is dr4 itself (no snapshot)")
    # The AR window load: `mov dr4, <park>` before the trigger. Take the
    # CLOSEST one: the prologue may materialise unrelated dr values earlier.
    ars = _find_mov32rr(blob, DR4, DR12, t)
    if not ars:
        raise FieldError("no `mov dr4, dr12` AR window load before the trigger")
    ar = ars[-1]
    br = None
    if binary:
        brs = _find_mov32rr(blob, DR0, DR12, t)
        if not brs:
            raise FieldError("no `mov dr0, dr12` BR window load before the trigger")
        br = brs[-1]
        if br != t - 2:
            raise FieldError(
                "BR window load at +%d does not abut the trigger at +%d" % (br, t)
            )
    elif ar != t - 2:
        raise FieldError(
            "AR window load at +%d does not abut the trigger at +%d" % (ar, t)
        )
    return {"trigger": t, "cmd": t + 2, "wait": w, "readback": rd, "ar": ar, "br": br}


def expected(shape: str, cmd: int, nops: int, binary: bool) -> bytes:
    s = SHAPES[shape]
    pre = bytes.fromhex(s["pre_binary"] if binary else s["pre_unary"])
    suf = bytes.fromhex(s["suf_binary"] if binary else s["suf"])
    return pre + bytes((TRIG_OP, TRIG_SFR, cmd)) + b"\x00" * nops + suf


def _check_shape(blob: bytes, shape: str, cmd: int, nops: int, binary: bool) -> bytes:
    exp = expected(shape, cmd, nops, binary)
    assert len(blob) == len(exp), (
        "sequence length %d != golden %d (surplus/missing step or wait byte)"
        % (len(blob), len(exp))
    )
    assert blob == exp, "sequence bytes differ from the %s golden:\n  got %s\n  exp %s" % (
        shape,
        blob.hex(),
        exp.hex(),
    )
    return exp


def self_test(blob: bytes, shape: str, cmd: int, nops: int, binary: bool) -> None:
    """Mutate the REAL artifact and require the very same comparison to fail.

    Every target is located by scanning for the field it claims to mutate, and
    the byte found there is asserted against that field's architectural
    encoding first -- so an injection cannot silently hit a neighbouring byte
    (the round-3 self-test bug: "wrong AR load" hit the BR load, "wrong BR
    load" hit a reload displacement, and the "missing wait byte" deletion
    removed the command code).
    """
    good = _check_shape(blob, shape, cmd, nops, binary)
    f = fields(good, nops, cmd, binary)

    # --- assertions that the located offsets really are those fields --------
    assert good[f["trigger"]] == TRIG_OP and good[f["trigger"] + 1] == TRIG_SFR
    assert good[f["cmd"]] == cmd, (hex(good[f["cmd"]]), hex(cmd))
    for i in range(nops):
        assert good[f["wait"] + i] == 0x00
    # AR load: destination dr4, source the parking register (never dr4).
    assert good[f["ar"]] == MOV32RR
    assert good[f["ar"] + 1] >> 4 == DR4, hex(good[f["ar"] + 1])
    assert good[f["ar"] + 1] & 0x0F == DR12, hex(good[f["ar"] + 1])
    # Readback: source dr4, destination outside the window.
    assert good[f["readback"]] == MOV32RR
    assert good[f["readback"] + 1] & 0x0F == DR4, hex(good[f["readback"] + 1])
    assert good[f["readback"] + 1] >> 4 != DR4, hex(good[f["readback"] + 1])
    if binary:
        # BR load: destination dr0, source the parking register.
        assert good[f["br"]] == MOV32RR
        assert good[f["br"] + 1] >> 4 == DR0, hex(good[f["br"] + 1])
        assert good[f["br"] + 1] & 0x0F == DR12, hex(good[f["br"] + 1])

    # --- the six injection classes -----------------------------------------
    mutations = [
        # 1. The AR window load: flip its SOURCE nibble, i.e. load the AR half
        #    from a different register.
        ("wrong-ar-load-reg", f["ar"] + 1, good[f["ar"] + 1] ^ 0x02),
        # 2. The pinned trigger immediate itself.
        ("wrong-command", f["cmd"], good[f["cmd"]] ^ 0x01),
        # 3. The readback: flip its SOURCE lane, i.e. read some other window.
        ("wrong-readback-src", f["readback"] + 1, good[f["readback"] + 1] ^ 0x02),
    ]
    if binary:
        # 4. The BR window load source nibble (only the binary four have one).
        mutations.append(
            ("wrong-br-load-reg", f["br"] + 1, good[f["br"] + 1] ^ 0x02)
        )

    for name, off, val in mutations:
        assert good[off] != val, (name, off)
        bad = bytearray(good)
        bad[off] = val
        try:
            _check_shape(bytes(bad), shape, cmd, nops, binary)
        except AssertionError:
            continue
        raise AssertionError("self-test: %s injection was NOT rejected" % name)

    # 5. A deleted wait byte. This is the round-3 bug -- the old deletion
    #    removed `pre_len + 2`, which is the command code, so it never proved
    #    the wait chain was anchored. Delete a REAL zero byte out of the chain
    #    and require rejection.
    missing = good[: f["wait"]] + good[f["wait"] + 1 :]
    assert missing != good
    assert len(missing) == len(good) - 1
    try:
        _check_shape(missing, shape, cmd, nops, binary)
    except AssertionError:
        pass
    else:
        raise AssertionError("self-test: missing-wait-byte was NOT rejected")

    # 6. A surplus byte at the end (sequence length anchor).
    try:
        _check_shape(good + b"\xaa", shape, cmd, nops, binary)
    except AssertionError:
        pass
    else:
        raise AssertionError("self-test: surplus-byte was NOT rejected")

    print(
        "self-test[%s]: ar/br/cmd/wait/readback/length injections all rejected"
        % shape
    )


def elf_symbol_slice(blob: bytes, symbol: str) -> bytes:
    """Return the .text bytes of one function symbol (ELF32, big-endian)."""
    assert blob[:7] == b"\x7fELF\x01\x02\x01", "expected ELF32/BE"
    e_shoff, = struct.unpack_from(">I", blob, 32)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", blob, 46)
    secs = [
        struct.unpack_from(">IIIIIIIIII", blob, e_shoff + i * e_shentsize)
        for i in range(e_shnum)
    ]
    shstr = secs[e_shstrndx]

    def name_of(sec):
        p = shstr[4] + sec[0]
        return blob[p : blob.index(b"\x00", p)].decode()

    text = next(s for s in secs if name_of(s) == ".text")
    symtab = next(s for s in secs if s[1] == 2)  # SHT_SYMTAB
    strtab = secs[symtab[6]]
    for pos in range(symtab[4], symtab[4] + symtab[5], 16):
        st_name, st_value, st_size, _info, _other, shndx = struct.unpack_from(
            ">IIIBBH", blob, pos
        )
        if shndx == 0:
            continue
        p = strtab[4] + st_name
        if blob[p : blob.index(b"\x00", p)].decode() != symbol:
            continue
        base = text[4] + st_value
        return blob[base : base + st_size]
    raise AssertionError("symbol %s not found" % symbol)


def main() -> int:
    ap = argparse.ArgumentParser()
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--text")
    src.add_argument("--obj")
    ap.add_argument("--symbol")
    ap.add_argument("--shape", required=True, choices=sorted(SHAPES))
    ap.add_argument("--cmd", required=True, help="hex command code, e.g. 2d")
    ap.add_argument("--nops", required=True, type=int)
    ap.add_argument("--binary", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    cmd = int(args.cmd, 16)
    if args.obj:
        assert args.symbol, "--obj needs --symbol"
        blob = elf_symbol_slice(pathlib.Path(args.obj).read_bytes(), args.symbol)
        where = "%s:%s" % (args.obj, args.symbol)
    else:
        blob = pathlib.Path(args.text).read_bytes()
        where = args.text

    _check_shape(blob, args.shape, cmd, args.nops, args.binary)
    fields(blob, args.nops, cmd, args.binary)
    print("golden ok[%s]: %s (%d bytes, cmd 0x%02x, %d nops)" %
          (args.shape, where, len(blob), cmd, args.nops))
    if args.self_test:
        self_test(blob, args.shape, cmd, args.nops, args.binary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
