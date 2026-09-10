; liba.asm - E4 acceptance demo, object A.
;
; Exercises every relocation the asm2elf path maps onto mcs251-lld:
;   R_MCS251_J16  lcall #_asm_helper_b   (cross-object call)
;                 ljmp  #_asm_back       (absolute intra-object jump)
;   R_MCS251_PC8  sjmp  _asm_resume      (cross-object relative branch)
;   R_MCS251_J11  acall #_asm_local      (intra-object, area reference)
;                 ajmp  #_asm_resume     (cross-object 2K-page jump)
;   R_MCS251_24   ecall #_c_add          (asm -> C call)
;                 ecall #_asm_helper_x   (24-bit cross-object call)
;   R_MCS251_16   mov dptr,#_asm_table   (cross-object const table)
;                 mov dptr,#_c_flag      (C global reference)
;                 mov dptr,#_asm_xbuf    (XSEG reservation reference)
;   R_MCS251_LO8  mov a,#_asm_table / mov a,_c_flag / mov _bvar,#0x55
;                 mov dpl/dph/b/acc + PARM_2 argument stores
;   R_MCS251_MID8 mov a,#_asm_table>>8
;   R_MCS251_HI8  mov a,#_asm_table>>16
;
; Call/return frame discipline (every callee is paired with its frame):
;   near (RET) callees _asm_helper_b/_asm_local/_asm_bsub/_asm_resume are
;   reached only by lcall/acall/sjmp/ljmp/ajmp; extended (ERET) callees
;   _c_add and _asm_helper_x are reached only by ecall.
;
; asm -> C argument ABI for `int c_add(int a, int b)` (MCS251CallingConv.td,
; measured on hardware): the two channels are NOT laid out the same way.
;   - The first i32 argument travels in the register channel DPL:DPH:B:A,
;     least significant byte first (DPL = bits [7:0] ... A = bits [31:24]).
;   - The second i32 argument travels in the static overlay slot
;     _c_add_PARM_2, a 4-byte BIG-ENDIAN memory object ("Memory objects use
;     the same measured big-endian layout as SDCC",
;     MCS251ISelLowering.cpp:2529-2537): value 0x33445566 sits as
;     33 44 55 66 at ascending slot addresses.  This object fills the slot
;     with direct stores before the ecall; check.py cross-checks these
;     stores byte for byte against the compiler-produced caller demo/cabi.c,
;     so the two sides cannot drift (in either direction) unnoticed.
	.module liba
	.area XSEG (XDATA,REL,CON)
	.globl _asm_xbuf
_asm_xbuf:
	.blkb	8
	.area CSEG (CODE,REL,CON)
	.globl _asm_entry
	.globl _asm_local
	.globl _asm_helper_b	; external (libb)
	.globl _asm_helper_x	; external (libb)
	.globl _asm_resume	; external (libb)
	.globl _asm_table	; external (libb)
	.globl _bvar		; external (libb, DSEG)
	.globl _c_add		; external (C)
	.globl _c_add_PARM_2	; external (C, static argument slot)
	.globl _c_flag		; external (C)
_asm_entry:
	lcall	#_asm_helper_b	; R_MCS251_J16 cross-object call
	sjmp	_asm_resume	; R_MCS251_PC8 cross-object relative branch
_asm_back:
	acall	#_asm_local	; R_MCS251_J11 intra-object
	mov	a,#0x01
	; ---- asm -> C call: c_add(0x44332211, 0x33445566) ----
	mov	dpl,#0x11	; arg0 byte 0 (LSB)
	mov	dph,#0x22	; arg0 byte 1
	mov	b,#0x33		; arg0 byte 2
	mov	acc,#0x44	; arg0 byte 3 (MSB)
	mov	_c_add_PARM_2+0,#0x33	; arg1 byte at slot+0 (MSB, big-endian slot)
	mov	_c_add_PARM_2+1,#0x44	; arg1 byte at slot+1
	mov	_c_add_PARM_2+2,#0x55	; arg1 byte at slot+2
	mov	_c_add_PARM_2+3,#0x66	; arg1 byte at slot+3 (LSB)
	ecall	#_c_add		; R_MCS251_24 asm -> C cross-object call
	mov	a,_c_flag	; R_MCS251_LO8 direct load of the C global
	mov	_bvar,#0x55	; R_MCS251_LO8 direct store (PAG0)
	mov	dptr,#_asm_table ; R_MCS251_16 cross-object const table
	movc	a,@a+dptr
	mov	dptr,#_c_flag	; R_MCS251_16 C global address
	mov	dptr,#_asm_xbuf	; R_MCS251_16 XSEG address
	mov	a,#_asm_table	; R_MCS251_LO8
	mov	a,#_asm_table>>8	; R_MCS251_MID8
	mov	a,#_asm_table>>16 ; R_MCS251_HI8
	ecall	#_asm_helper_x	; R_MCS251_24 cross-object extended-frame call
	ajmp	#_asm_resume	; R_MCS251_J11 cross-object jump
	ljmp	#_asm_back	; R_MCS251_J16 absolute intra-object jump
_asm_local:
	nop
	ret
