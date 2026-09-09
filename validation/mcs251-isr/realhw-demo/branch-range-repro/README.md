# E1 archive: MCS251 PC-relative branch out of range (branch relaxation)

Archived 2026-09-10 alongside the backend fix for assessment item E1
(COMPILER-ASSESSMENT-20260910.md §4).

## Symptom

During realhw-demo development, `llc` object generation of the (then
fully-inlined) report code failed repeatedly with:

```text
<unknown>:0: error: MCS251 PC-relative branch out of range
```

Adding `__attribute__((noinline))` to the main.c helpers made the error go
away, which was a workaround, not a fix.

## Root cause (located)

* Every MCS251 conditional branch (`je/jne/jc/jnc/jg/jle/jsl/jsge/jsg/jsle`)
  and `sjmp` is a 2-byte instruction with a signed rel8 displacement measured
  from the byte after the instruction: reach is exactly [-128, +127].
  `MCS251AsmBackend.cpp` applies those PC-relative `FK_Data_1` fixups and
  errors out when the value no longer fits.
* The compiler therefore always expanded conditional branches into the
  three-part form `jCCinv skip ; ejmp target` (FinalizeISel custom inserter,
  `MCS251ISelLowering.cpp expandLongConditionalBranch`). The `jCCinv` skip
  displacement is a fixed 4 bytes *only while* the skip block stays
  layout-adjacent to the branch.
* That adjacency was believed to be guaranteed by MachineBlockPlacement
  (uniform successor probabilities). It is not: once a function grows past a
  few hundred blocks, MBP's chain formation displaces the skip block, and the
  rel8 no longer reaches. This was reproduced deterministically (see below)
  and confirmed at MIR level: before `-stop-before=block-placement` every
  `jCC; EJMP` pair is adjacent; after placement, e.g.
  `bb.301: JE %bb.302 ; EJMP %bb.282` is followed in layout by `bb.285`,
  with `bb.302` 11 blocks further on, so the je displacement exceeds +127.

## Why the current main.c no longer shows it

The failing development-time code shape no longer exists (the noinline
workaround changed inlining, and main.c evolved). Compiling today's
`main.c` with all noinline attributes removed through the pre-fix backend
(clang -O0/-O1/-O2/-Os/-Oz x llc -O0/-O1/-O2/-O3, both object formats)
succeeds. The failure class itself is real and deterministic; it is
reproduced by randomly generated control-flow graphs.

## Reproduction (pre-fix backend)

`mbp-displaced-skip.O1.ll` is a captured failing IR module (LLVM IR of a
randomly generated 350-line C function, fuzz seed 4 of the 2026-09-10
campaign: 27 of 150 generated modules failed at llc -O1/-O2, none at -O0).
With the pre-fix toolchain:

```sh
build/bin/llc -O1 -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 \
  -verify-machineinstrs -mcs251-object-format=elf -filetype=obj \
  mbp-displaced-skip.O1.ll -o /dev/null
# => error: MCS251 PC-relative branch out of range
```

`displaced-nested-loops.src.c` is a minimized descendant (nested loops +
diamonds) that still exercises the same MachineBlockPlacement displacement
against the fixed backend; it is the source shape of the lit regression test
`llvm/test/CodeGen/MCS251/branch-relax-displaced.ll`.

## Fix (landed with this archive)

* `MCS251InstrInfo.cpp getInstSizeInBytes` now returns the exact encoded
  size of every instruction (mirroring `MCS251MCCodeEmitter.cpp`), policed
  by `getInstSizeVerifyMode == ExactSize` in the AsmPrinter on object output.
* New pass `MCS251BranchRelaxation` (`MCS251BranchRelaxation.cpp`) runs in
  `addPreEmitPass`, after the layout is final: every rel8 branch that no
  longer reaches is retargeted (same opcode, same condition) to a fresh
  4-byte-away trampoline block carrying `ejmp` to the original destination;
  out-of-range `sjmp` becomes `ejmp`. The scan repeats until stable, and a
  debug-only final check asserts every rel8 branch is in range.
* The generic `analyzeBranch` hooks were deliberately NOT implemented, so
  the branch folder / tail duplication / terminator rewriting stay inactive
  and codegen for in-range branches is unchanged.

## Status after the fix

* 400/400 generated CFG modules plus the 27 originally failing modules
  compile at llc -O0/-O1/-O2 in both ELF and REL object formats.
* `llvm/test/CodeGen/MCS251` lit suite: 112/112 PASS, including
  `branch-relax-boundary.mir` (+127/-128 direct vs. +128/-129 relaxed) and
  `branch-relax-displaced.ll`, and `isr-exits.ll` untouched and passing.
* `main.c` in this directory no longer carries the noinline workaround.
