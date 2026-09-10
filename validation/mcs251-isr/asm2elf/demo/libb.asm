; libb.asm - E4 acceptance demo, object B.
;
; Provides the cross-object call/branch targets of liba and the shared
; constant table plus a DSEG variable.  The table lives in CSEG (code
; memory, read with movc) because mcs251-lld maps .rodata/.text to the
; same CSEG region and DSEG/XSEG reservations carry no ROM bytes.
;
; Frame discipline: near callers get a RET helper (_asm_helper_b),
; extended callers get an ERET helper (_asm_helper_x).  One function must
; not serve both kinds of caller (an ECALL to a RET-returning function, or
; an LCALL to an ERET-returning one, corrupts the return frame).
	.module libb
	.area DSEG (DATA,REL,CON)
	.globl _bvar
_bvar:
	.blkb	1
	.area CSEG (CODE,REL,CON)
	.globl _asm_helper_b
	.globl _asm_helper_x
	.globl _asm_resume
	.globl _asm_table
	.globl _asm_entry	; external (liba)
_asm_helper_b:
	inc	r2		; near frame: reached by lcall only
	acall	#_asm_bsub	; R_MCS251_J11 intra-object
	ret
_asm_bsub:
	nop
	ret
_asm_helper_x:
	inc	r2		; extended frame: reached by ecall only
	eret
_asm_resume:
	nop
	nop
	ret
_asm_table:
	.db	0x10,0x20,0x30,0x40
