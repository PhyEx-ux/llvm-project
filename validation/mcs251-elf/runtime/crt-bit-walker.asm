; crt-bit-walker.asm - BT14 bit-init table walker (encoding evidence).
;
; This file is the sdas251 ASSEMBLY EVIDENCE SOURCE for the bit-init walker
; bytes transcribed into crt-bit.yaml (P09 BT14; record format frozen in
; lld/MCS251/LinkerCore.cpp BT14 comments and lld/test/MCS251/bit-profile.test).
; It is NOT linked into any image; assemble it only to (re)produce the gold
; listing:
;
;   /home/liu/build-sdcc/bin/sdas251 -l -o /tmp/b.rel crt-bit-walker.asm
;
; and compare the emitted bytes against the BOOT hex block of crt-bit.yaml
; (the two .db lines carry the s_BITINIT relocation placeholders the same way
; the frozen XINIT walker prologue does).
;
; Record format v1 (frozen, 3 bytes per backing byte that owns at least one
; automatic bit): u8 addr + u8 and_mask + u8 or_value.  lld synthesizes the
; table as .mcs251.bittable in the BITINIT CODE area with and_mask =
; ~owned_mask and or_value = value & owned_mask, ascending by addr; bytes
; with mask 0 (fixed-only or untouched) get NO record, so the walker never
; touches them ("mask=0 no access").  The walker performs NO format
; validation - the lld gate owns that - and applies
;
;   IRAM[addr] = (IRAM[addr] & and_mask) | or_value
;
; which clears/sets only the allocator-owned bits and PRESERVES every
; neighbour bit's power-on value (partial mask) or writes the declared value
; outright (mask 0xff: and_mask 0x00).  Zero-initialized owned bits are
; explicitly cleared through the mask, never through a blanket window clear
; and never through QEMU-default zero RAM.
;
; Walker structure (offsets walker-relative, 0x37 = 55 bytes total):
;   +0x00  dr0 24-bit prologue (s_BITINIT window then bank, .db hand split)
;   +0x08  wr4 = l_BITINIT (remaining table bytes)
;   +0x0C  r6 = 0 (wr6 = {r6,r7} IDATA pointer; high byte stays 0)
;   +0x0F  __mcs251_bit_record: while (wr4 != 0) read 3-byte record, RMW
;   +0x36  __mcs251_bit_done: eret (ECALL return)
;
; Register plan (REWRITTEN 2026-09-16: the first QEMU pollute-RAM e2e
; exposed that the original staging was broken; see the note below).
; dr0 walks the BITINIT table in code space, wr4 tracks unread table bytes,
; wr6 = {r6,r7} is the IDATA indirect pointer, r12/r13/r14 stage the record
; fields, ACC carries the RMW data.  The constraints that shape the plan,
; as implemented by QEMU's MCS251 model (qemu target/mcs51, and matching
; the MCS251 register-file architecture):
;
;   - DR0 IS the register-file bytes r0-r3 (RAM-mapped in the PSW-selected
;     bank): loading dr0 writes IRAM 0x00-0x03, and WR2 = {r2,r3} shares two
;     of those bytes.  So r0-r3 must never be touched after the prologue,
;     and WR2 can never be this walker's pointer.
;   - r10 and r11 are the B and ACC registers; staging the and_mask into
;     r11 loses it to the very next ACC-using instruction (anl a,r11 is a
;     no-op identity).  Record fields must stage in general registers.
;   - wr4 = {r4,r5} doubles as the record counter; loading anything into r4
;     or r5 corrupts it.
;
; Therefore: pointer = wr6 (bank bytes r6/r7, disjoint from dr0's r0-r3 and
; from wr4's r4-r5; r6 is pinned to zero once, r7 is reloaded per record,
; so wr6 = 0x00NN addresses the 256-byte IDATA window through the word-
; register indirect access); staging = r12/r13/r14 (general registers, the
; frozen XINIT walker precedent - its own staging lives in r12/r14/r16 and
; never in r0-r7).  The walker is ECALLed from the BOOT flow strictly
; BEFORE __mcs251_globals_init, so no live register state is destroyed that
; anything depends on.
;
; DEFECT RECORD (2026-09-16, found by validation/mcs251-bit/bt14-pollute
; e2e under polluted RAM): the original transcription staged the record
; address into r5 ("wr2 = {r4,r5}" - a register pair that does not exist)
; and the masks into r10-r12.  Under the real register model that
; clobbered the loop counter with the record address, left @wr2 reading
; dr0's own bytes, and dropped the and_mask into the accumulator, so NO
; record was ever applied to the window (the bit values read back as the
; untouched pollution).  The lit-level tests could not catch this: they
; freeze the crt bytes and the lld diagnostics but never execute the
; walker.  The register plan above is validated end-to-end in QEMU with a
; polluted bit window (A5/5A at 0x20): full-mask bytes receive their
; declared value, partial-mask bytes keep their polluted neighbours, and
; untouched bytes are never accessed.

        .area BITINIT (CODE)

        .globl  s_BITINIT
        .globl  l_BITINIT

__mcs251_bit_init::
        ; dr0[15:0] = s_BITINIT window (MID8/LO8 relocations land here).
        .db     0x7e, 0x08, (s_BITINIT) >> 8, (s_BITINIT)
        ; dr0[31:16] = 0x00 bank (HI8 relocation; R0 keeps the zero top byte).
        .db     0x7a, 0x0c, 0x00, (s_BITINIT) >> 16
        mov     wr4,#l_BITINIT
        mov     r6,#0x00                ; wr6 = {r6,r7}: keep the high byte zero
__mcs251_bit_record:
        cmp     wr4,#0x0000
        je      __mcs251_bit_done
        mov     r12,@dr0                ; record addr (IRAM direct 0x20-0x2F)
        inc     dr0
        mov     r13,@dr0                ; and_mask = ~owned_mask
        inc     dr0
        mov     r14,@dr0                ; or_value = value & owned_mask
        inc     dr0
        sub     wr4,#0x0003
        mov     r7,r12                  ; wr6 = 0x00NN (the record's direct addr)
        mov     a,@wr6                  ; A = IRAM[addr]  (neighbour bits intact)
        anl     a,r13                   ; clear the owned bits only
        orl     a,r14                   ; set owned bits to declared values
        mov     @wr6,a                  ; IRAM[addr] = A
        sjmp    __mcs251_bit_record
__mcs251_bit_done:
        eret
