; crt-xdata-init-walker.asm - X4 xdata_init record walker (encoding evidence).
;
; This file is the sdas251 ASSEMBLY EVIDENCE SOURCE for the XDATA_INIT walker
; bytes transcribed into crt-selfstart.yaml and crt-irq.yaml (X4 slice of
; XDATA-CODE-SLICE-TASK.md; record format frozen in
; validation/mcs251-models/proposals/XDATA-CODE-DESIGN-SUPPLEMENT.md section 7).
; It is NOT linked into any image and NOT part of the frozen ASxxxx asset
; crt-selfstart.asm (validation/mcs251-firmware stays untouched); assemble it
; only to (re)produce the gold listing:
;
;   /home/liu/build-sdcc/bin/sdas251 -l -o /tmp/x.rel crt-xdata-init-walker.asm
;
; and compare the emitted bytes against the BOOT hex blocks in both YAML
; fixtures (the two .db lines carry the s_XDATA_INIT relocation placeholders
; the same way the frozen XINIT walker prologue does).
;
; Record format v1 (frozen): u8 bank + u16 window (BE) + u16 object_size (BE)
; + u16 payload_size (BE) + payload; payload_size == 0 means "clear
; object_size bytes".  Records never straddle a 64K window and never overlap
; (lld validateXDATAInit() rejects both, belt-and-braces on top of the XSEG
; allocator's no-straddle rule); the walker therefore performs NO bank-carry
; handling and NO format validation - the format is guaranteed by the lld gate
; and malformed input behavior is documented as out of contract (README.md).
;
; Walker structure (offsets walker-relative, 98 = 0x62 bytes total):
;   +0x00  dr0 24-bit prologue (s_XDATA_INIT window then bank, .db hand split)
;   +0x08  wr4 = l_XDATA_INIT (remaining table bytes)
;   +0x0C  __mcs251_xdata_record: while (wr4 != 0) read 7-byte header,
;          DPXL <- bank, DPH:DPL <- window, wr4 -= 7
;   +0x3F  __mcs251_xdata_copy: payload_size bytes via movx @dptr,a / inc dptr
;   +0x54  __mcs251_xdata_zero: object_size zero bytes via clr a / movx
;   +0x61  __mcs251_xdata_done: eret (ECALL return)
;
; Register plan mirrors the frozen XINIT walker (crt-selfstart.asm 88-125):
; dr0 walks the XDATA_INIT area in code space, wr4 tracks unread table bytes,
; wr8/wr12/wr16 hold window/object_size/payload_size, r14 stages payload
; bytes, ACC carries the movx data.  The walker is ECALLed from the BOOT flow
; strictly between __mcs251_globals_init and _main, so no live register state
; is destroyed that anything depends on.
;
; DPXL after the walker: NOT restored to the reset value 01h.  DESIGN
; SUPPLEMENT section 3 (DPXL keeping protocol) rules "CRT startup: no
; obligation - generated code does not depend on the reset value 01h, no
; initial-value action (self-healing)": every AS3 access sequence re-loads
; DPXL one step before its movx, so the last record's bank left in DPXL is
; harmless.  The DPS SFR selects DPTR bank 0 at reset (and crt-irq sets
; DPS = 0 explicitly before any walker runs); AU0/ID0/TSL are 0 there, so
; movx @dptr never auto-increments or toggles the selection - the explicit
; "inc dptr" below is the only pointer advance.
;
; QEMU evidence: the linked image of validation/mcs251-xdata-e2e (firmware
; with XDATA_INIT records) runs on qemu-system-mcs251 -M stc32g144k246 and
; the initialized XDATA contents read back match (build.sh + serial
; transcript assertions; see that package's README).

        .area XDATA_INIT (CODE)

        .globl  s_XDATA_INIT
        .globl  l_XDATA_INIT

__mcs251_xdata_init::
        ; dr0[15:0] = s_XDATA_INIT window (MID8/LO8 relocations land here).
        .db     0x7e, 0x08, (s_XDATA_INIT) >> 8, (s_XDATA_INIT)
        ; dr0[31:16] = 0x00 bank (HI8 relocation; R0 keeps the zero top byte).
        .db     0x7a, 0x0c, 0x00, (s_XDATA_INIT) >> 16
        mov     wr4,#l_XDATA_INIT
__mcs251_xdata_record:
        cmp     wr4,#0x0000
        je      __mcs251_xdata_done
        mov     r14,@dr0                ; bank (DPXL value, canonical [23:16])
        inc     dr0
        mov     wr8,@dr0                ; window, big-endian u16 (R8 hi, R9 lo)
        inc     dr0
        inc     dr0
        mov     wr12,@dr0               ; object_size
        inc     dr0
        inc     dr0
        mov     wr16,@dr0               ; payload_size (0 = clear only)
        inc     dr0
        inc     dr0
        sub     wr4,#0x0007
        mov     dpxl,r14                ; DPXL <- bank   (SFR 0x84)
        mov     dph,r8                  ; DPH <- window hi (SFR 0x83)
        mov     dpl,r9                  ; DPL <- window lo (SFR 0x82)
        cmp     wr16,#0x0000            ; payload_size == 0 -> zero fill
        je      __mcs251_xdata_zero
__mcs251_xdata_copy:
        mov     r14,@dr0                ; payload byte from code space
        inc     dr0
        mov     a,r14
        movx    @dptr,a                 ; XDATA write through the 24-bit window
        inc     dptr
        dec     wr4
        dec     wr16
        cmp     wr16,#0x0000
        je      __mcs251_xdata_record
        sjmp    __mcs251_xdata_copy
__mcs251_xdata_zero:
        cmp     wr12,#0x0000
        je      __mcs251_xdata_record
        clr     a
        movx    @dptr,a
        inc     dptr
        dec     wr12
        sjmp    __mcs251_xdata_zero
__mcs251_xdata_done:
        eret
