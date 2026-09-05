# Phase 11 preflight: i32 canonical pointer hardware semantics

Date: 2026-09-05  
Target: QEMU `stc32g144k246`, `/home/liu/build-qemu/qemu-system-mcs251`  
Toolchain: `/home/liu/build-sdcc/bin/sdas251`, `/home/liu/build-sdcc/bin/sdcc`  

This is a QEMU-asserted fact report. The probes were assembled and linked into STC32 HEX images and then executed. No repository files were modified. All generated artifacts are under `/tmp/mcs251-p11-pre/`.

## Executive conclusions

| Area | QEMU-asserted fact | Implementation consequence |
|---|---|---|
| A | `CMP DR,DR` is 32-bit subtraction. CY is 32-bit borrow, Z is 32-bit equality, N is result bit 31, and OV is 32-bit signed overflow. | All ten requested icmp conditions map directly to JE/JNE/JC/JNC/JG/JLE/JSL/JSGE/JSG/JSLE. |
| B | DR-indexed MOV uses `(DR & 0x00ffffff) + sign_extend(int16(dis))`, reduced modulo 24 bits. The four forms execute. | Use low 24 DR bits and sign-extend the 16-bit displacement. |
| C | ECALL @DR and EJMP @DR use the full 24-bit DR target including the high region byte. | Preserve all 24 target bits for indirect code pointers. |
| D | In this SDCC MCS251 ABI generic `char *`, `__xdata char *`, and function pointers are all 3 bytes. Memory representation is big-endian `[23:16,15:8,7:0]`; first pointer argument is `B:DPH:DPL`. | Keep the canonical pointer in B/DPH/DPL, copy it to DR28 for generic dereference or indirect call, and use 24-bit carry propagation for p+1. |
| E | DPX/DR56 is `{R56=0,R57=DPXL,R58=DPH,R59=DPL}`. SPX/DR60 is `{R60=0,R61=0,R62=SPX[15:8],R63=SPX[7:0]}`. | DPX is not laid out like SPX's high-16 pair; its useful 24-bit lane starts at R57. |

The QEMU programs intentionally spin after output, so the timeout termination line is expected. The assertion is the `PASS` line and the evidence bytes before it.

## Probe/build protocol

The fixed startup asset was copied from `/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/crt0.asm` to `/tmp/mcs251-p11-pre/crt0.asm`. The temporary startup uses HOME/reset at `0xff0000` and GSINIT0 at `0xfc2800`, as required by the STC32 QEMU image layout. Harnesses use SBUF `0x99` for serial output and independent data bytes for evidence.

Representative invocation:

```text
/home/liu/build-sdcc/bin/sdas251 -plosgffw -o a/probe.rel probe-a.asm
/home/liu/build-sdcc/bin/sdcc -mmcs251 --nostdlib --no-xinit-opt \
  --code-loc 0xff0000 "-Wl-b GSINIT0=0xfc2800" \
  -o a/cmp32.hex a/crt0.rel a/harness.rel a/probe.rel
timeout 3 /home/liu/build-qemu/qemu-system-mcs251 \
  -M stc32g144k246 -bios a/cmp32.hex -accel tcg \
  -display none -monitor none -serial stdio
```

## A. CMP DR,DR: 32-bit PSW and JCC semantics

### Conclusion table

| Condition | Relation tested | JCC | Result |
|---|---|---|---|
| eq | `0x12345678 == 0x12345678` | JE | direct, PASS |
| ne | `0x12345678 != 0x12345679` | JNE | direct, PASS |
| ult | `0x7fffffff < 0x80000000` | JC | direct, PASS |
| uge | `0x80000000 >= 0x7fffffff` | JNC | direct, PASS |
| ugt | `0x80000000 > 0x7fffffff` | JG | direct, PASS |
| ule | `0x7fffffff <= 0x80000000` | JLE | direct, PASS |
| slt | `INT32_MIN < 0` | JSL | direct, PASS |
| sge | `INT32_MAX >= 0` | JSGE | direct, PASS |
| sgt | `INT32_MAX > -1` | JSG | direct, PASS |
| sle | `INT32_MIN <= 0` | JSLE | direct, PASS |

The signed cases cross the sign boundary. Additional overflow cases distinguish 32-bit flags from an 8-bit implementation.

### QEMU evidence, verbatim

`/tmp/mcs251-p11-pre/a/qemu.out`:

```text
BA:0101010101010101010120010401a4 PASS
qemu-system-mcs251: terminating on signal 15 from pid 2155 (timeout)
```

Evidence bytes 0..9 are the ten JCC assertions and are all `01`. Immediate PSW1 captures are:

| CMP operation | Result | PSW1 |
|---|---:|---:|
| `CMP 0x80000000,0` | `0x80000000` | `0x20` (`N=1, OV=0, CY=0, Z=0`) |
| `CMP 0x80000000,0x7fffffff` | `0x00000001` | `0x04` (`N=0, OV=1, CY=0, Z=0`) |
| `CMP 0x7fffffff,0x80000000` | `0xffffffff` | `0xa4` (`N=1, OV=1, CY=1, Z=0`) |

The probe source is `/tmp/mcs251-p11-pre/probe-a.asm`; image and raw output are `/tmp/mcs251-p11-pre/a/cmp32.hex` and `/tmp/mcs251-p11-pre/a/qemu.out`; listing is `/tmp/mcs251-p11-pre/a/probe.lst`.

`CMP DR,DR` computes a 32-bit `lhs-rhs`: CY=1 iff unsigned subtraction borrows (`lhs < rhs`); Z=1 iff all result bits are zero; N is result bit 31; OV is signed overflow for 32-bit subtraction.

Direct lowering is:

```text
icmp eq  -> je       icmp ne  -> jne
icmp ult -> jc       icmp uge -> jnc
icmp ugt -> jg       icmp ule -> jle
icmp slt -> jsl      icmp sge -> jsge
icmp sgt -> jsg      icmp sle -> jsle
```

If a path lacks dedicated signed JCCs, use `N xor OV` for signed less-than, `!(N xor OV)` for signed greater-or-equal, `!(N xor OV) && !Z` for signed greater-than, and `(N xor OV) || Z` for signed less-or-equal. Current hardware makes replacements unnecessary.

## B. @DRk+dis indexed addressing

### Conclusion table

| Form | QEMU assertion |
|---|---|
| `mov Rm,@DRk+dis` | byte load `0x5a`, PASS |
| `mov WRj,@DRk+dis` | big-endian word load `0xabcd`, PASS |
| `mov @DRk+dis,Rm` | byte store/readback `0x3c`, PASS |
| `mov @DRk+dis,WRj` | big-endian word store/readback `0x7788`, PASS |

DR16 was set to low 24-bit address `0x010000`, then reloaded with high byte `0x12`; accesses still addressed the same location. Bits 31:24 are ignored and bits 23:0 form the base.

The QEMU rule is:

```text
EA = ((DR & 0x00ffffff) + sign_extend(int16(dis))) & 0x00ffffff
```

Displacement bytes are big-endian. Although TSV/YAML spells the operand `@DRk+dis24`, sdas251 emits a 16-bit displacement: source bytes `opcode, subopcode, disp_hi, disp_lo` (five bytes including source-mode A5 prefix). No separately accepted/encoded 24-bit displacement form was found. `/tmp/mcs251-p11-pre/b/probe.lst` contains:

```text
29 A4 00 10    mov r10,@dr16+0x0010
29 A4 12 34    mov r10,@dr16+0x1234
29 A5 FF 00    mov r10,@dr20-0x0100
```

Explicit `@dr20-0x0100` and positive spelling `+0xff00` both encode `FF 00`. With DR20=`0x00010100`, the probe preloaded `0x33` at `0x010000` and `0x22` at `0x020000`; QEMU read `0x33`, proving sign extension rather than zero extension.

### QEMU evidence, verbatim

`/tmp/mcs251-p11-pre/b/qemu.out`:

```text
BB:5aabcd3c77885a3333333301 PASS
qemu-system-mcs251: terminating on signal 15 from pid 4303 (timeout)
```

Evidence bytes are `5a ab cd 3c 77 88 5a 33 33 33 33 01`. Sources are `/tmp/mcs251-p11-pre/probe-b.asm` and `/tmp/mcs251-p11-pre/harness-b.c`; image/listing/output are `/tmp/mcs251-p11-pre/b/indexed-dr.hex`, `/tmp/mcs251-p11-pre/b/probe.lst`, and `/tmp/mcs251-p11-pre/b/qemu.out`.

The TSV rows at `/mnt/c/Prj/LLVM/MCS251/sdcc-upstream/sdas/as251/tests/instruction-forms.tsv` are `mov_rm_idx_dr`, `mov_wr_idx_dr`, `mov_idx_dr_rm`, and `mov_idx_dr_wr`, with encodings `29 a4 12 34`, `69 24 12 34`, `39 a4 12 34`, and `79 24 12 34`. QEMU supplies execution semantics.

## C. ECALL @DR and EJMP @DR

### Conclusion table

| Instruction | Target | Result |
|---|---|---|
| `ecall @dr16` | target in `0xfe0000` region | callee reached, `eret` returned |
| `ejmp @dr16` | target in `0xfe0000` region | callee reached, jump-back point reached |

### QEMU evidence, verbatim

`/tmp/mcs251-p11-pre/c/qemu.out`:

```text
BC:01010101 PASS
qemu-system-mcs251: terminating on signal 15 from pid 3272 (timeout)
```

The four `01` bytes independently mark ECALL continuation, ECALL callee entry, EJMP callee entry, and post-EJMP jump-back. The probe uses:

```text
mov dr16,#target_ecall
movh dr16,#0x00fe
ecall @dr16
...
mov dr16,#target_ejmp
movh dr16,#0x00fe
ejmp @dr16
```

The link map places the target area at `0x00fe0000`. Source/image/map/output are `/tmp/mcs251-p11-pre/probe-c.asm`, `/tmp/mcs251-p11-pre/c/indirect-call.hex`, `/tmp/mcs251-p11-pre/c/indirect-call.map`, and `/tmp/mcs251-p11-pre/c/qemu.out`.

TSV rows `ecall_at_dr` and `ejmp_at_dr` list `99 48` and `89 48`; QEMU proves target execution, not only assembly acceptance. A target at `0x010900` was rejected by QEMU's firmware loader before execution; `0xfe0000` produced PASS. This is an image-layout constraint, not evidence of truncation.

## D. SDCC generic-pointer ABI

### QEMU evidence, verbatim

The C specimen was compiled with `-S` under default small model and `--model-large`. Both passed:

```text
/tmp/mcs251-p11-pre/d/qemu-small.out
BD:12345612345603035a5aa5012a031256 PASS
qemu-system-mcs251: terminating on signal 15 from pid 4365 (timeout)

/tmp/mcs251-p11-pre/d/qemu.out
BD:12345612345603035a5aa5012a031256 PASS
qemu-system-mcs251: terminating on signal 15 from pid 3610 (timeout)
```

The evidence bytes decode as: `12 34 56` generic constant bytes; `12 34 56` xdata constant bytes; `03` generic size; `03` xdata size; `5a` generic dereference; `5a` xdata dereference; `a5` p+1 dereference; `01` cast round-trip equality; `2a` function-pointer result; `03` function-pointer size; `12` constant high byte; `56` constant low byte.

Assembly artifacts are `/tmp/mcs251-p11-pre/d/pointer-abi.asm` and `/tmp/mcs251-p11-pre/d/pointer-abi-small.asm`; source is `/tmp/mcs251-p11-pre/pointer-abi.c`.

1. Generic `char *` is 3 bytes. In this target ABI `__xdata char *` is also 3 bytes, not 2. `0x123456` materializes as:

   ```text
   mov dptr,#0x3456
   mov dpxl,#0x12
   mov dr28,dpx
   ```

   Memory representation is `[0x12,0x34,0x56]`; B is high byte and DPH/DPL are middle/low.

2. At pointer-taking function entry SDCC emits:

   ```text
   mov r7,b
   mov r6,dph
   mov a,dpl
   ```

   Thus first pointer argument is `B:DPH:DPL = addr[23:16]:addr[15:8]:addr[7:0]`; A is only a transient DPL copy. SDCC spills offsets +0,+1,+2 high-to-low.

3. Generic dereference copies bytes to DPL/DPH/DPXL, then `mov dr28,dpx` and `ecall __gptrget`. `p+1` uses low-to-high carry:

   ```text
   inc r7
   cjne r7,#0x00,carry_done
   inc r6
   cjne r6,#0x00,carry_done
   inc r5
   carry_done:
   mov dpl,r7
   mov dph,r6
   mov b,r5
   ```

4. Generic/xdata casts round-trip equal in QEMU (`evidence[11]=01`) without changing the three-byte flat value. No 2-byte xdata representation was observed.

5. `sizeof(fn_t)=3`; assembly indirect call is:

   ```text
   push r0
   push r1
   push r2
   pop dr28
   ecall @dr28
   ```

   The function pointer is in DR28 and QEMU function-result evidence is `0x2a`.

The large-model link included `/mnt/c/Prj/LLVM/MCS251/sdcc-upstream/build-smoke/device/lib/mcs251-large/_gptrget.rel` because generic dereference is an actual runtime call.

## E. DPX lane layout versus SPX

### Conclusion table

| Alias | Four slots | Useful value |
|---|---|---|
| DR56 / DPX | `00 33 22 11` after DPXL=33, DPH=22, DPL=11 | `DPXL:DPH:DPL` in R57:R58:R59; R56 zero |
| DR60 / SPX | `00 00 44 55` after SPX=4455 | R62:R63 are SPX high/low; R60:R61 zero |

### QEMU evidence, verbatim

`/tmp/mcs251-p11-pre/e/qemu.out`:

```text
BE:0033221111223300004455 PASS
qemu-system-mcs251: terminating on signal 15 from pid 4261 (timeout)
```

The probe writes DPX SFR bytes directly (`DPL=11`, `DPH=22`, `DPXL=33`), copies `dpx` to DR0, independently reads the SFRs, sets SPX=`4455`, copies `spx` to DR4, and restores live startup SPX before ERET. Source/image/listing/output are `/tmp/mcs251-p11-pre/probe-e.asm`, `/tmp/mcs251-p11-pre/e/dpx-lane.hex`, `/tmp/mcs251-p11-pre/e/probe.lst`, and `/tmp/mcs251-p11-pre/e/qemu.out`.

Direct SFR evidence is `11 22 33`; DPX alias evidence is `00 33 22 11`; SPX alias evidence is `00 00 44 55`. This settles the DPX/SPX lane contradiction for this QEMU target: DPX is not symmetric with the SPX high-16 convention.

## Artifact index

| Area | Source | Image | Raw QEMU evidence |
|---|---|---|---|
| A | `probe-a.asm`, `harness-a.c` | `a/cmp32.hex` | `a/qemu.out` |
| B | `probe-b.asm`, `harness-b.c` | `b/indexed-dr.hex` | `b/qemu.out` |
| C | `probe-c.asm`, `harness-c.c` | `c/indirect-call.hex` | `c/qemu.out` |
| D | `pointer-abi.c`, `d/pointer-abi.asm` | `d/pointer-abi.hex` | `d/qemu.out`, `d/qemu-small.out` |
| E | `probe-e.asm`, `harness-e.c` | `e/dpx-lane.hex` | `e/qemu.out` |

Fixed startup copy: `/tmp/mcs251-p11-pre/crt0.asm`.
