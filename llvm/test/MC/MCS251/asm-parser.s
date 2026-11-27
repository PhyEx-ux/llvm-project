; RUN: llvm-mc -triple=mcs251 -mcs251-object-format=elf -filetype=obj %s -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=DIS
; RUN: llvm-readobj -h -r %t.o | FileCheck %s --check-prefix=OBJ

; OBJ: Machine: EM_MCS251
; OBJ: R_MCS251_24 _ext_fn
; OBJ: R_MCS251_16 _ext_sym
; OBJ: R_MCS251_J16 _jt_tbl

; DIS: <_asm_demo>:
; DIS:      nop
; DIS-NEXT: mov r3, r12
; DIS-NEXT: mov wr4, wr6
; DIS-NEXT: mov dr16, dr60
; DIS-NEXT: mov r5, #0x5a
; DIS-NEXT: mov wr2, #0x1234
; DIS-NEXT: mov dr0, #0x5678
; DIS-NEXT: movh dr0, #0x00ff
; DIS-NEXT: mov a, r3
; DIS-NEXT: mov r7, a
; DIS-NEXT: mov a, r9
; DIS-NEXT: mov r12, a
; DIS-NEXT: mov dpl, r2
; DIS-NEXT: mov r4, dph
; DIS-NEXT: mov b, r1
; DIS-NEXT: mov r6, b
; DIS-NEXT: mov 0x84, r2
; DIS-NEXT: mov 0xed, #0x1c
; DIS-NEXT: mov a, #0x42
; DIS-NEXT: mov r3, @wr6
; DIS-NEXT: mov @wr6, r3
; DIS-NEXT: mov r10, @wr6+0x1234
; DIS-NEXT: mov @wr6+0x1234, r10
; DIS-NEXT: mov r2, @dr60
; DIS-NEXT: mov @dr60-0x0004, r2
; DIS-NEXT: mov wr4, @dr0
; DIS-NEXT: mov @dr60-0x0002, wr4
; DIS-NEXT: movx a, @dptr
; DIS-NEXT: movx @dptr, a
; DIS-NEXT: add r1, r2
; DIS-NEXT: sub wr4, wr2
; DIS-NEXT: add dr4, dr0
; DIS-NEXT: anl r3, #0x0f
; DIS-NEXT: orl wr2, #0x00ff
; DIS-NEXT: xrl r5, r6
; DIS-NEXT: orl r4, a
; DIS-NEXT: sll r3
; DIS-NEXT: srl wr6
; DIS-NEXT: sra r1
; DIS-NEXT: mul ab
; DIS-NEXT: mul wr12, wr8
; DIS-NEXT: cmp r5, #0x10
; DIS-NEXT: cmp wr6, #0x0100
; DIS-NEXT: cmp dr4, dr0
; DIS-NEXT: setb 0x20
; DIS-NEXT: clr 0x21
; DIS-NEXT: cpl 0x22
; DIS-NEXT: mov c, 0x23
; DIS-NEXT: mov 0x24, c
; DIS-NEXT: setb c
; DIS-NEXT: clr c
; DIS-NEXT: cpl c
; DIS-NEXT: rrc a
; DIS-NEXT: rlc a
; DIS-NEXT: jb 0x10,
; DIS-NEXT: je
; DIS-NEXT: sjmp
; DIS-NEXT: ecall 0x000000
; DIS-NEXT: mov wr0, #0x0000
; DIS-NEXT: mov dptr, #0x0000
; DIS-NEXT: jmp @a+dptr
; DIS-NEXT: push psw
; DIS-NEXT: pop psw
; DIS-NEXT: push dr16
; DIS-NEXT: pop dr16
; DIS-NEXT: inc spx, #0x4
; DIS-NEXT: dec spx, #0x2
; DIS-NEXT: inc dr0, #0x01
; DIS-NEXT: dec wr6, #0x01
; DIS-NEXT: inc dptr
; DIS-NEXT: clr a
; DIS-NEXT: ecall @dr12
; DIS-NEXT: eret
; DIS-NEXT: reti
; DIS-NEXT: ret

.text
.globl _asm_demo
.type _asm_demo, @function
_asm_demo:
  nop
  mov r3, r12
  mov wr4, wr6
  mov dr16, dr60
  mov r5, #0x5a
  mov wr2, #0x1234
  mov dr0, #0x5678
  movh dr0, #0x00ff
  mov a, r3
  mov r7, a
  mov a, r9
  mov r12, a
  mov dpl, r2
  mov r4, dph
  mov b, r1
  mov r6, b
  mov dpxl, r2
  mov 0xed, #0x1c
  mov a, #0x42
  mov r3, @wr6
  mov @wr6, r3
  mov r10, @wr6+0x1234
  mov @wr6+0x1234, r10
  mov r2, @spx
  mov @spx-0x0004, r2
  mov wr4, @dr0
  mov @dr60-0x0002, wr4
  movx a, @dptr
  movx @dptr, a
  add r1, r2
  sub wr4, wr2
  add dr4, dr0
  anl r3, #0x0f
  orl wr2, #0x00ff
  xrl r5, r6
  orl r4, a
  sll r3
  srl wr6
  sra r1
  mul ab
  mul wr12, wr8
  cmp r5, #0x10
  cmp wr6, #0x0100
  cmp dr4, dr0
  setb 0x20
  clr 0x21
  cpl 0x22
  mov c, 0x23
  mov 0x24, c
  setb c
  clr c
  cpl c
  rrc a
  rlc a
  jb 0x10, .Ldone
  je .Ldone
  sjmp .
.Ldone:
  ecall _ext_fn
  mov wr0, #(_ext_sym+4)
  mov dptr, #_jt_tbl
  jmp @a+dptr
  push psw
  pop psw
  push dr16
  pop dr16
  inc spx, #4
  dec spx, #2
  inc dr0, #1
  dec wr6, #1
  inc dptr
  clr a
  ecall @dr12
  eret
  reti
  ret

.section .rodata, "a", @progbits
.globl _const_table
_const_table:
  .db 0x11, 0x22
  .dw 0x3344
  .ds 2
