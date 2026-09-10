#!/usr/bin/env bash
# run_addend_checks.sh - blocker-1 / round-2 / round-3 acceptance:
# sdld vs sdrel2elf+lld.
#
# Alice review blocker 1: the converter used to read every T-line field as
# an unsigned addend, so a borrowing expression like `ecall #(_target-1)`
# (T field FF FF FF) produced RELA addend 0xFFFFFF and mcs251-lld rejected
# with "relocation overflow" where sdld resolves the same link (exit 0).
# The fix interprets SYMBOL-reference fields (R3_SYM bit set: external and
# global symbols) as two's-complement values at the adb_* domain width of
# their relocation type (see ADDEND_DOMAIN_BYTES in sdrel2elf.py).
#
# Alice review round 2, issue 1: the same unconditional sext16 misread
# AREA references (no R3_SYM bit: labels defined inside the module, with
# the area-relative offset folded into the T field by asout.c; sdld's
# relr3() adds a[rindex]->a_addr, lkrloc3.c:377-384).  sdld's arithmetic
# there is purely modular (base + unsigned field), and sdas rejects
# negative same-area ljmp/lcall targets already at assembly time
# (mcs251mch.c out_control16), so the 16-bit control fields are read
# UNSIGNED at the domain width.  The intra-object `ljmp #_target` at area
# offset +0x8000 below is the exact repro that used to become addend
# -32768 and die with "J16 bank overflow" in mcs251-lld while sdld exits 0.
#
# Alice review round 3 (C24 area borrow): R3_SYM only distinguishes where
# the reference BASE comes from; it does not make area fields non-negative.
# A same-area label participates in negative expressions
# (`ecall #(_entry-1)` emits a bare area reference with T=FF FF FF), and
# the full-24-bit-field types (R_MCS251_24, R_MCS251_HI8 -- adb_24_hi is
# adb_3b; the >>16 of an area expression stays in the R_HIB mode bit, so
# the borrow form is reachable) are therefore read as two's complement
# for BOTH reference kinds: legal area offsets are < 0x10000 < 2**23, so
# sext24 leaves them untouched and recovers borrows mod 2**24.
#
# Alice review round 4 (slice-family area borrows at high bases): lld
# bounds _16/LO8/MID8/HI8 (its "Is24Slice" family, LinkerCore.cpp
# applyRelocations) by -0x800000 <= S+A <= 0xFFFFFF while sdld applies NO
# check, and the round-2 UNSIGNED domain representative for area refs is
# not bounded inside that window once the section base is high: a -1
# borrow folds to A_u = 2**(8d)-1 and base + A_u leaves 0xFFFFFF for every
# base above 0x1000000-2**(8d).  Repro A: --area-start=CSEG=0xFF0400,
# `mov dptr,#(_entry-1)` + `mov a,#((_entry-1)>>8)` (true target 0xFF03FF
# in space) died with "relocation overflow"; repro B: a 2-byte object in
# front of the referencing module moves its section base to 0xFF0002 at
# the default start (0xFF0002+0xFFFF = 0x1000001), same rejection.  The
# same class reaches LO8 at bases above 0xFFFF00.  Round 4's first fix
# (sext at domain width) cleared those but was REJECTED in round 5: sext
# only recovers borrows within HALF a domain (sext goes negative only for
# u >= M/2), so a deeper borrow -- a -b borrow wraps to u = M-b < M/2 --
# is read as a positive offset and S+u still overflows at high bases.
#
# Alice review round 5 (final): the three pure slices take the unique
# NON-POSITIVE domain representative instead: u = T mod M (M = 2**16 for
# _16/MID8, 2**8 for LO8); A = 0 when u = 0, else A = u - M.  Interval
# derivation: A <= 0 with S <= 0xFFFFFF gives S+A <= 0xFFFFFF; A >=
# -(M-1) = -65535 with S >= 0 gives S+A >= -65535 > -0x800000 -- both
# bounds hold for EVERY field and EVERY base, so lld's slice check can
# never fire; the written bytes depend only on (S+T) mod M and A stays
# congruent, so output is byte-identical to sdld unconditionally, with no
# positive/negative intent disambiguation (that is what sext could not
# do).  Round-5 repros (all true targets in space, sdld exit 0, sext lld
# rejected all): LO8 `(_entry-129)` @0xFFFFF0 -> 0xFFFF6F; _16/MID8
# `(_entry-0x8001)` @0xFF9000 -> 0xFF0FFF.  The choice does NOT transfer
# to J16/J11/PC8/_24 (bank/page/PC-window/full-target semantics; they
# keep their round-2..3 representatives).
#
# For every byte-compare case below the SAME two-module link is performed
#   (a) natively with sdld  (sdcc-upstream linker, .rel -> .ihx), and
#   (b) through the standard path (sdrel2elf.py --source-mode source ->
#       mcs251-lld, CSEG at 0xff0000),
# and the linked CSEG bytes must be IDENTICAL.  The cases marked
# expect_lld_reject document the intentional divergences (pattern in the
# file, default "relocation overflow"):
#   - div_ecall_wide / ecall24_area_wide: the true value leaves the 24-bit
#     field; sdld silently truncates (wrong landing), mcs251-lld rejects.
#   - ljmp16_sym_const_high: cross-object J16 with T >= 0x8000 is
#     genuinely ambiguous (a -N borrow or a +0x8000-class constant; same
#     bytes).  The chosen sext16 representative recovers the borrow class
#     (ljmp16_borrow below) but mcs251-lld's bank check then rejects the
#     constant class that sdld silently accepts; the unsigned
#     representative would invert which class diverges.  Declared, not
#     papered over (README "addend semantics").
set -uo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
A2ELF=$(cd -- "$HERE/.." && pwd)
OUT=${1:?usage: run_addend_checks.sh <output-dir>}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

SDAS=${SDAS:-/home/liu/build-sdcc/bin/sdas251}
SDLD=${SDLD:-/home/liu/build-sdcc/bin/sdld}
LLD=${LLD:-/home/liu/build-mcs251-lld/bin/lld}
CONVERT="$A2ELF/sdrel2elf.py"
BASE=0xff0000
DSEG=${DSEG:-0}   # set to 0x30 by cases that define DSEG symbols
FAILURES=0
CASES=0

run_case() { # name lld_expect(regions-args) -- files...
  local name="$1"; shift
  CASES=$((CASES+1))
  local dir="$OUT/case_$name"
  mkdir -p "$dir"
  (
    cd "$dir"
    local sdld_rc lld_rc
    "$SDAS" -los caller.rel caller.asm || return 1
    "$SDAS" -los ref.rel ref.asm || return 1
    {
      echo "-i $dir/sd.ihx"
      echo "-b CSEG=$BASE"
      [ "$DSEG" != 0 ] && echo "-b DSEG=$DSEG"
      echo "$dir/caller.rel"
      echo "$dir/ref.rel"
      echo "-e"
    } > link.lk
    "$SDLD" -f link.lk > sdld.log 2>&1
    sdld_rc=$?
    python3 "$CONVERT" caller.rel -o caller.o --source-mode source \
      || return 1
    python3 "$CONVERT" ref.rel -o ref.o --source-mode source || return 1
    "$LLD" -flavor mcs251 caller.o ref.o --area-start=CSEG=$BASE \
      ${DSEG:+--area-start=DSEG=$DSEG} -o lld.elf > lld.log 2>&1
    lld_rc=$?
    if [ "$sdld_rc" -ne 0 ]; then
      # sdld flags some links it completes anyway: its J16/PCR checkers
      # inspect the *untruncated* 24-bit intermediate sum, so e.g. every
      # cross-object byte PCR draws a "Byte PCR relocation error" warning
      # even when the final displacement byte is correct.  Cases marked
      # with expect_sdld_warning assert that specific warning, require the
      # .ihx to exist, and compare the bytes; anything else is a failure.
      if [ -f expect_sdld_warning ] && [ -f sd.ihx ]; then
        want=$(cat expect_sdld_warning)
        if ! grep -q "$want" sdld.log; then
          echo "addend: FAIL: $name: expected sdld warning '$want'"
          cat sdld.log
          return 1
        fi
      else
        echo "addend: FAIL: $name: sdld exited $sdld_rc"
        cat sdld.log
        return 1
      fi
    fi
    if [ -f expect_lld_reject ]; then
      # Documented divergence: sdld links (exit 0 above) but the chosen
      # addend representative makes mcs251-lld reject the object; the
      # expected diagnostic pattern comes from the marker file (default
      # "relocation overflow" for the out-of-space truncation class).
      want=$(cat expect_lld_reject)
      want=${want:-relocation overflow}
      if [ "$lld_rc" -eq 0 ]; then
        echo "addend: FAIL: $name: expected mcs251-lld to reject, exit 0"
        return 1
      fi
      if ! grep -q "$want" lld.log; then
        echo "addend: FAIL: $name: lld rejected without '$want'"
        cat lld.log
        return 1
      fi
      echo "addend: ok: $name: sdld links silently, mcs251-lld rejects ($want)"
      return 0
    fi
    python3 - "$name" "$lld_rc" "$BASE" <<'PYEOF'
import struct, sys

name, lld_rc, base = sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 16)

def ihx_bytes(path):
    mem, upper = {}, 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        raw = bytes.fromhex(line[1:])
        count, addr, typ = raw[0], (raw[1] << 8) | raw[2], raw[3]
        payload = raw[4:4 + count]
        if typ == 0:
            for i, b in enumerate(payload):
                mem[upper | (addr + i)] = b
        elif typ == 4:
            upper = (payload[0] << 8 | payload[1]) << 16
        elif typ == 1:
            break
    return mem

def elf_bytes(path):
    data = open(path, "rb").read()
    phoff = struct.unpack_from(">I", data, 28)[0]
    phnum = struct.unpack_from(">H", data, 44)[0]
    mem = {}
    for i in range(phnum):
        (_t, off, _va, pa, filesz, _ms, _fl, _al) = \
            struct.unpack_from(">8I", data, phoff + i * 32)
        for j in range(filesz):
            mem[pa + j] = data[off + j]
    return mem

sd = ihx_bytes("sd.ihx")
ll = elf_bytes("lld.elf")
def run(sd_mem, ll_mem):
    # Compare over the union span with unwritten bytes defaulted to 0:
    # sdld's .ihx omits contiguous zero runs (e.g. the .blkb gap in
    # ljmp16_area_high) while the lld image materializes them explicitly.
    out_sd, out_ll = bytearray(), bytearray()
    a = base
    while a in sd_mem or a in ll_mem:
        out_sd.append(sd_mem.get(a, 0))
        out_ll.append(ll_mem.get(a, 0))
        a += 1
    return bytes(out_sd), bytes(out_ll)
sd_run, ll_run = run(sd, ll)
def show(b):
    return b.hex() if len(b) <= 64 else b[:64].hex() + "...(%d bytes)" % len(b)
print("addend: info: %s: sdld  CSEG: %s" % (name, show(sd_run)))
print("addend: info: %s: lld   CSEG: %s" % (name, show(ll_run)))
sys.exit(0 if sd_run and sd_run == ll_run else 1)
PYEOF
    cmp_rc=$?
    if [ "$cmp_rc" -ne 0 ]; then
      echo "addend: FAIL: $name: linked images differ"
      return 1
    fi
    if [ "$lld_rc" -ne 0 ]; then
      echo "addend: FAIL: $name: mcs251-lld exited $lld_rc"
      cat lld.log
      return 1
    fi
    echo "addend: ok: $name: sdld and sdrel2elf+lld images byte-identical"
  )
  if [ $? -ne 0 ]; then
    FAILURES=$((FAILURES+1))
  fi
  DSEG=0
}

# --- 24-bit absolute (R_C24 -> R_MCS251_24), adb_3b domain -----------------
# Alice's original repro: the -1 borrows through the whole field.
cat > "$OUT/.tmp1" <<'EOF'
	.module caller
	.area CSEG (CODE)
	.globl _target
_main:
	ecall	#(_target-1)
	ret
EOF
cat > "$OUT/.tmp2" <<'EOF'
	.module ref
	.area CSEG (CODE)
	.globl _target
_target:
	nop
	ret
EOF
mkdir -p "$OUT/case_ecall24_borrow"
cp "$OUT/.tmp1" "$OUT/case_ecall24_borrow/caller.asm"
cp "$OUT/.tmp2" "$OUT/case_ecall24_borrow/ref.asm"
run_case ecall24_borrow

make_case() { # name caller-body ref-body
  local name="$1"
  mkdir -p "$OUT/case_$name"
  {	echo "	.module caller"
	echo "	.area CSEG (CODE)"
	echo "	.globl _target"
	echo "_main:"
	echo "$2"
	echo "	ret"
  } > "$OUT/case_$name/caller.asm"
  {	echo "	.module ref"
	echo "	.area CSEG (CODE)"
	echo "	.globl _target"
	echo "_target:"
	echo "$3"
	echo "	ret"
  } > "$OUT/case_$name/ref.asm"
}

# Shared trivial second module for the AREA-reference cases below (the
# reference lives in the caller module; the harness always links two
# objects).
cat > "$OUT/.refspare.asm" <<'EOF'
	.module arearef
	.area CSEG (CODE)
	.globl _spare
_spare:
	ret
EOF

area_case() { # name caller-body -- same-module label reference (no R3_SYM)
  local name="$1"
  mkdir -p "$OUT/case_$name"
  {	echo "	.module areacaller"
	echo "	.area CSEG (CODE)"
	echo "	.globl _entry"
	echo "_entry:"
	echo "$2"
	echo "	ret"
  } > "$OUT/case_$name/caller.asm"
  cp "$OUT/.refspare.asm" "$OUT/case_$name/ref.asm"
}

make_case ecall24_plus '	ecall	#(_target+1)' '	nop'
run_case ecall24_plus

# --- 24-bit absolute AREA references (R_C24 -> R_MCS251_24), adb_3b domain -
# Alice review round 3: a bare area reference can borrow.  R3_SYM only
# distinguishes where the reference base comes from; same-area labels take
# part in negative expressions, and since MCS251 code areas live in a 64K
# region a legal unsigned offset is always T < 0x10000 < 2**23 -- so the
# field is read sext24 for BOTH reference kinds.  sdld computes
# base + field stored mod 2**24 (exit 0); _entry sits at area offset 0.
area_case ecall24_area_borrow '	ecall	#(_entry-1)'
run_case ecall24_area_borrow

# Normal large positive area offset: sext24 is a no-op below 2**23.
area_case ecall24_area_plus '	ecall	#(_entry+0x234)'
run_case ecall24_area_plus

# sdas ACCEPTS a +0x800000-class constant in an area-referenced C24 field
# (verified: T = 80 00 00, exit 0), and its intent is unrecoverable from
# the .rel: `+0x800000` and `-0x800000` produce the same field.  The
# sext24 reading reproduces sdld's modular landing byte-for-byte (both
# sides exit 0 and store 7F 00 00); README declares the ambiguity instead
# of denying it.
area_case ecall24_area_hugeconst '	ecall	#(_entry+0x800000)'
run_case ecall24_area_hugeconst

# --- 16-bit absolute control (R_J16 -> R_MCS251_J16), adb_2b domain --------
# Borrowing constant -4: target _target-4.  sdld's region check runs on the
# *untruncated* sum (0xFF0004 + 0xFFFC = 0x1000000) and only warns; the
# written field (00 00) equals mcs251-lld's (S + sext16(T) = 0xFF0000).
# Constants -1..-3 push the sum further and get the same warning; -2 was
# verified by hand to also produce matching bytes.
make_case ljmp16_borrow '	ljmp	#(_target-4)' '	nop'
echo "64K Region relocation error" > "$OUT/case_ljmp16_borrow/expect_sdld_warning"
run_case ljmp16_borrow
make_case ljmp16_plus '	ljmp	#(_target+4)' '	nop'
run_case ljmp16_plus

# Cross-object J16 with a +0x8000-class CONSTANT: T = 0x8000 is genuinely
# ambiguous (a -0x8000 borrow or a +0x8000 constant; same bytes).  sdld
# adds the raw unsigned field and checks the *untruncated* sum: no carry,
# so it silently accepts and lands at 0xff8004 (the intended target).  The
# converter's sext16 representative reads -0x8000 and mcs251-lld's bank
# check rejects; the unsigned representative would instead break the
# ljmp16_borrow case above.  Neither choice covers both classes, so this
# is a declared divergence (README "addend semantics"): rewrite such
# constants via an equ/label or an absolute `ljmp #constant`.
make_case ljmp16_sym_const_high '	ljmp	#(_target+0x8000)' '	nop'
printf 'J16 bank overflow' > "$OUT/case_ljmp16_sym_const_high/expect_lld_reject"
run_case ljmp16_sym_const_high

# --- 16-bit AREA references (no R3_SYM): unsigned field, round-2 issue 1 ---
# The exact review repro: intra-object `ljmp #_target` with _target at area
# offset +0x8000 (CSEG placed at 0xff0000 -> target 0xff8000, same 64K
# region).  sdld: exit 0, writes 02 80 00.  With the old unconditional
# sext16 the converter produced addend -32768 and mcs251-lld rejected with
# "J16 bank overflow".  Byte-for-byte sdld agreement + lld link success.
mkdir -p "$OUT/case_ljmp16_area_high"
{
  echo "	.module areahigh"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry,_target"
  echo "_entry:"
  echo "	ljmp	#_target"
  echo "	.blkb	0x7ffd"
  echo "_target:"
  echo "	ret"
} > "$OUT/case_ljmp16_area_high/caller.asm"
{
  echo "	.module areahighref"
  echo "	.area CSEG (CODE)"
  echo "	.globl _spare"
  echo "_spare:"
  echo "	ret"
} > "$OUT/case_ljmp16_area_high/ref.asm"
run_case ljmp16_area_high

# Same-area 16-bit data reference whose expression borrows below the area
# base: `mov dptr,#(_entry-4)` folds to T field FF FC as an area reference.
# sdld's plain adb_2b path has no range check: relv = 0xFF0000 + 0xFFFC =
# 0xFFFFFF, stores FF FC.  Round 4: this case only passed under the
# round-2 unsigned reading because the base sat exactly on 0xFF0000, where
# the sum lands on the 0xFFFFFF boundary; the non-positive representative
# (-4) is congruent mod 2^16 (stores the same FF FC) and keeps the case
# green.
mkdir -p "$OUT/case_mov16_area_borrow"
{
  echo "	.module areaborrow"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry"
  echo "_entry:"
  echo "	mov	dptr,#(_entry-4)"
  echo "	ret"
} > "$OUT/case_mov16_area_borrow/caller.asm"
cp "$OUT/case_ljmp16_area_high/ref.asm" \
  "$OUT/case_mov16_area_borrow/ref.asm"
run_case mov16_area_borrow

# --- round 4: slice-family area borrows at high section bases ---------------
# Repro A: CSEG placed OFF the 0xFF0000 bank boundary.  With the round-2
# unsigned area representative the -1 borrow (T fields FFFF / FF FF FF)
# produced addends 0xFFFF and lld computed 0xFF0400+0xFFFF = 0x10003FF >
# 0xFFFFFF -> "relocation overflow", while sdld resolves the true target
# 0xFF03FF (in space) and writes 90 03 FF 74 03.  The non-positive
# representative (-1) makes S+A the exact target on both relocs.
mkdir -p "$OUT/case_mov16_mid8_area_borrow_bankstart"
{
  echo "	.module bankstart"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry"
  echo "_entry:"
  echo "	mov	dptr,#(_entry-1)"
  echo "	mov	a,#((_entry-1)>>8)"
  echo "	ret"
} > "$OUT/case_mov16_mid8_area_borrow_bankstart/caller.asm"
cp "$OUT/.refspare.asm" "$OUT/case_mov16_mid8_area_borrow_bankstart/ref.asm"
BASE=0xff0400
run_case mov16_mid8_area_borrow_bankstart
BASE=0xff0000

# Repro B: the referencing module is preceded by a 2-byte object at the
# DEFAULT start, so its own .text input section (and sdld's second CSEG
# fragment) begins at 0xFF0002.  The unsigned representative died on
# 0xFF0002+0xFFFF = 0x1000001; the non-positive representative (-1)
# lands on 0xFF0001 (the eret opcode in front of the module), and both
# sides write 90 00 01.
mkdir -p "$OUT/case_mov16_area_borrow_prepended"
{
  echo "	.module preamble"
  echo "	.area CSEG (CODE)"
  echo "	.globl _pre"
  echo "_pre:"
  echo "	nop"
  echo "	eret"
} > "$OUT/case_mov16_area_borrow_prepended/caller.asm"
{
  echo "	.module borrower"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry"
  echo "_entry:"
  echo "	mov	dptr,#(_entry-1)"
  echo "	eret"
} > "$OUT/case_mov16_area_borrow_prepended/ref.asm"
run_case mov16_area_borrow_prepended

# Genuine large area offsets must not regress under the non-positive
# reading: _far sits at area offset 0x9ABC (bit 15 set), the non-positive
# representative (u-0x10000 = -0x6544) stays inside lld's slice window
# and writes the same low 16 bits (9A BC) sdld stores.
mkdir -p "$OUT/case_mov16_area_high"
{
  echo "	.module areahigh16"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry,_far"
  echo "_entry:"
  echo "	mov	dptr,#_far"
  echo "	.blkb	0x9ab9"
  echo "_far:"
  echo "	ret"
} > "$OUT/case_mov16_area_high/caller.asm"
cp "$OUT/.refspare.asm" "$OUT/case_mov16_area_high/ref.asm"
run_case mov16_area_high

# --- round 5: borrows deeper than half a domain (Alice repros) --------------
# sext at domain width only recovers |borrow| <= M/2: -129 wraps to
# u = 0x7F and -0x8001 to u = 0x7FFF (both < M/2), which sext reads as
# POSITIVE offsets; at the high bases below S+u leaves the slice window
# and lld rejected links sdld resolves with in-space true targets.  The
# non-positive representative (A = u-M, see header) fixes all three:
# these are byte-compare cases, NOT divergences -- the field carries no
# ambiguity that needs recovering here.
mkdir -p "$OUT/case_lo8_area_borrow_halfdomain"
{
  echo "	.module lo8half"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry"
  echo "_entry:"
  echo "	mov	a,#(_entry-129)"
  echo "	ret"
} > "$OUT/case_lo8_area_borrow_halfdomain/caller.asm"
cp "$OUT/.refspare.asm" "$OUT/case_lo8_area_borrow_halfdomain/ref.asm"
BASE=0xfffff0
run_case lo8_area_borrow_halfdomain
BASE=0xff0000

mkdir -p "$OUT/case_mov16_area_borrow_halfdomain"
{
  echo "	.module m16half"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry"
  echo "_entry:"
  echo "	mov	dptr,#(_entry-0x8001)"
  echo "	ret"
} > "$OUT/case_mov16_area_borrow_halfdomain/caller.asm"
cp "$OUT/.refspare.asm" "$OUT/case_mov16_area_borrow_halfdomain/ref.asm"
BASE=0xff9000
run_case mov16_area_borrow_halfdomain

mkdir -p "$OUT/case_mid8_area_borrow_halfdomain"
{
  echo "	.module mid8half"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry"
  echo "_entry:"
  echo "	mov	a,#((_entry-0x8001)>>8)"
  echo "	ret"
} > "$OUT/case_mid8_area_borrow_halfdomain/caller.asm"
cp "$OUT/.refspare.asm" "$OUT/case_mid8_area_borrow_halfdomain/ref.asm"
run_case mid8_area_borrow_halfdomain
BASE=0xff0000

# Zero-offset fields pin the u = 0 -> A = 0 branch of the representative
# rule (a bare same-area label reference folds T = 0 for all three pure
# slice types; positive offsets are already pinned by lo8_area_plus /
# mid8_area_plus / mov16_area_high / mid8_area_high above).
mkdir -p "$OUT/case_slice_area_zero"
{
  echo "	.module slicezero"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry"
  echo "_entry:"
  echo "	mov	dptr,#_entry"
  echo "	mov	a,#_entry"
  echo "	mov	a,#((_entry)>>8)"
  echo "	ret"
} > "$OUT/case_slice_area_zero/caller.asm"
cp "$OUT/.refspare.asm" "$OUT/case_slice_area_zero/ref.asm"
run_case slice_area_zero

# --- 24-bit multi-byte borrow: -256 crosses bytes 0 and 1 ------------------
make_case ecall24_borrow256 '	ecall	#(_target-256)' '	nop'
run_case ecall24_borrow256

# --- 16-bit data word (R3_WORD -> R_MCS251_16), adb_2b domain --------------
# The reference symbol lives in DSEG at 0x30 in every case below.
dmk() { # name body
  local name="$1"
  mkdir -p "$OUT/case_$name"
  {	echo "	.module caller"
	echo "	.area CSEG (CODE)"
	echo "	.globl _dvar"
	echo "_main:"
	echo "$2"
	echo "	ret"
  } > "$OUT/case_$name/caller.asm"
  {	echo "	.module refd"
	echo "	.area DSEG (DATA)"
	echo "	.globl _dvar"
	echo "_dvar:"
	echo "	.blkb	4"
  } > "$OUT/case_$name/ref.asm"
  DSEG=0x30
}

dmk mov16_borrow '	mov	dptr,#(_dvar-1)'
run_case mov16_borrow
# Cross-byte carry: 0x30 + 0xFF = 0x12F carries into the high byte.
dmk mov16_carry '	mov	dptr,#(_dvar+0xFF)'
run_case mov16_carry
# 16-bit boundary: constant with the top bit set but positive intent; the
# R_MCS251_16 slice check accepts both readings and the bytes agree.
dmk mov16_boundary '	mov	dptr,#(_dvar+0x8000)'
run_case mov16_boundary

# --- byte selections (R_BYT3 -> LO8/MID8/HI8), mask-derived domains --------
dmk lo8_carry '	mov	a,#(_dvar+0x1FF)'
run_case lo8_carry
dmk mid8_borrow '	mov	a,#((_dvar-1)>>8)'
run_case mid8_borrow
dmk hi8_borrow '	mov	a,#((_dvar-1)>>16)'
run_case hi8_borrow

# --- byte selections on AREA references (R_BYT3, no R3_SYM) ----------------
# Slice semantics (rounds 3-5): the selected byte depends only on
# (base + field) mod 2**(8*(k+1)), so ANY domain-congruent representative
# is byte-congruent however a borrow is read.  Round 3 assumed the
# mask-width unsigned representative stays bounded inside lld's slice
# window; round 4 showed that premise fails once the base is high (LO8
# u8 addend 0xFF at CSEG 0xFFFF80 overflows -- pinned below by
# lo8_area_borrow_topspace); round 5 replaced both with the unique
# NON-POSITIVE domain representative (see the header).  HI8 was never
# mask-bounded: adb_24_hi adds the FULL 24-bit field (the >>16 of an
# area expression stays in the R_HIB mode bit -- T remains FF FF FF for
# the borrow below), so it takes the sext24 reading like R_MCS251_24.
area_case lo8_area_plus '	mov	a,#(_entry+0x12)'
run_case lo8_area_plus
area_case lo8_area_borrow '	mov	a,#(_entry-1)'
run_case lo8_area_borrow
area_case mid8_area_plus '	mov	a,#((_entry+0x1234)>>8)'
run_case mid8_area_plus
area_case mid8_area_borrow '	mov	a,#((_entry-1)>>8)'
run_case mid8_area_borrow

# Round 4, LO8 corner: the unsigned u8 representative overflows lld's
# slice window for bases above 0xFFFF00 (0xFFFF80+0xFF = 0x100007F);
# the non-positive representative (-1) lands on the exact in-space
# target 0xFFFF7F, byte 0x7F on both sides (sdld: (0xFFFF80+0xFFFFFF)
# mod 2^24 = 0xFFFF7F).
mkdir -p "$OUT/case_lo8_area_borrow_topspace"
{
  echo "	.module lo8top"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry"
  echo "_entry:"
  echo "	mov	a,#(_entry-1)"
  echo "	ret"
} > "$OUT/case_lo8_area_borrow_topspace/caller.asm"
cp "$OUT/.refspare.asm" "$OUT/case_lo8_area_borrow_topspace/ref.asm"
BASE=0xffff80
run_case lo8_area_borrow_topspace
BASE=0xff0000

# Round 4, genuine large MID8 offset: _far at area offset 0xABCD (bit 15
# set); the non-positive representative (u-0x10000 = -0x5433) lands
# inside the window, writes 0xAB like sdld's middle byte of 0xFFABCD.
mkdir -p "$OUT/case_mid8_area_high"
{
  echo "	.module areahighmid"
  echo "	.area CSEG (CODE)"
  echo "	.globl _entry,_far"
  echo "_entry:"
  echo "	mov	a,#((_far)>>8)"
  echo "	.blkb	0xabcb"
  echo "_far:"
  echo "	ret"
} > "$OUT/case_mid8_area_high/caller.asm"
cp "$OUT/.refspare.asm" "$OUT/case_mid8_area_high/ref.asm"
run_case mid8_area_high

area_case hi8_area_plus '	mov	a,#((_entry+0x2345)>>16)'
run_case hi8_area_plus
area_case hi8_area_borrow '	mov	a,#((_entry-1)>>16)'
run_case hi8_area_borrow

# --- 8-bit PC-relative (R_BYT3|PCR -> R_MCS251_PC8), adb_3b domain ---------
# Pure-symbol cross-object reference (T field 0).  sdld always warns
# ("Byte PCR relocation error") on cross-object byte PCR because its range
# check sees the 24-bit intermediate, but the written displacement byte is
# correct and identical to mcs251-lld's.
make_case sjmp_pcr '	sjmp	_far' '	nop'
sed -i 's/_target/_far/' "$OUT/case_sjmp_pcr/"*.asm
echo "Byte PCR relocation error" > "$OUT/case_sjmp_pcr/expect_sdld_warning"
run_case sjmp_pcr

# --- 11-bit paged control (R_J11 -> R_MCS251_J11), adb_2b domain -----------
# Same sdld behaviour ("2K Page relocation error" for borrow encodings);
# compare the pure-symbol form.
make_case ajmp11_page '	ajmp	#_far' '	nop'
sed -i 's/_target/_far/' "$OUT/case_ajmp11_page/"*.asm
run_case ajmp11_page

# --- documented divergence: true value leaves the 24-bit domain ------------
# `+0x400000` pushes the exact sum past 24 bits; sdld silently truncates
# (writes 3F 00 04, a wrong landing), mcs251-lld must reject the object
# with "relocation overflow".  README ("addend semantics") declares this
# intentional strictness.
make_case div_ecall_wide '	ecall	#(_target+0x400000)' '	nop'
touch "$OUT/case_div_ecall_wide/expect_lld_reject"
run_case div_ecall_wide

# Same out-of-space class through an AREA reference: sdld silently
# truncates (writes 11 34 56), mcs251-lld rejects.  The class applies to
# both reference kinds exactly alike.
area_case ecall24_area_wide '	ecall	#(_entry+0x123456)'
touch "$OUT/case_ecall24_area_wide/expect_lld_reject"
run_case ecall24_area_wide

rm -f "$OUT/.tmp1" "$OUT/.tmp2" "$OUT/.refspare.asm"
if [ $FAILURES -ne 0 ]; then
  echo "addend: $FAILURES case(s) FAILED"
  exit 1
fi
DIVERGED=$(find "$OUT" -name expect_lld_reject | wc -l)
echo "addend: all $CASES cases ok: $((CASES - DIVERGED)) byte-identical with sdld, $DIVERGED documented divergences (mcs251-lld rejects; pattern in each expect_lld_reject)"
