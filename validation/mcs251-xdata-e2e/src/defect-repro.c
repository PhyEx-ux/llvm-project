/*
 * defect-repro.c - reproducer for the X2-scope miscompile found by the
 * X4 e2e (reported to the coordinator; FIXED by the X2 glue change in
 * MCS251ISelLowering.cpp buildMOVXByteLoad/Store - this image now pins
 * the GREEN -O2 shape).
 *
 * Compiled at -O2, store3() used to emit, in ONE basic block, three DPXL
 * writes, then three DPL writes, then three DPH writes, then the three A
 * values, then "F0 F0 F0" - three bare MOVX @DPTR,A with no pointer lane
 * writes between them.  On the machine (and in the QEMU model, which
 * matches the Intel MCS251 manual: no DPTR write queue is documented
 * anywhere in intel-um.txt / stc32g.txt; QEMU target/mcs51 implements
 * immediate dptr[selected] updates) all three stores then hit the LAST
 * written address (a3[0] in the observed schedule).
 *
 * Root cause (X2 fix): at -O1+ the DAG combiner parallelises disjoint
 * non-volatile memory ops (parallelizeChainedStores / FindBetterChain), so
 * the three store chains became TokenFactor-parallel; the DPL/DPH/DPXL/A
 * pins live only in the MCInstrDesc implicit operand lists, which the
 * SelectionDAG schedulers do not see, so the pre-RA list scheduler
 * streamed the lane writes by register across the parallel accesses.
 *
 * Fix: each MOVX byte sequence is welded with Glue into one scheduling
 * unit (llvm/test/CodeGen/MCS251/xdata-o2-order.ll pins both paths).
 *
 * Post-ISel MIR evidence BEFORE the fix (-stop-after=finalize-isel):
 *   MOV8dpxl %7,  implicit-def dead $dpxl
 *   MOV8dpxl %10, implicit-def dead $dpxl
 *   MOV8dpxl %13, implicit-def dead $dpxl
 *   MOV8dpl  %15, implicit-def dead $dpl
 *   MOV8dpl  %17, implicit-def dead $dpl
 *   MOV8dpl  %19, implicit-def dead $dpl
 *   MOV8dph  %20, implicit-def dead $dph
 *   MOV8dph  %21, implicit-def dead $dph
 *   MOV8dph  %22, implicit-def dead $dph
 *   MOV8a %4 / MOV8a %2 / MOV8a %0
 *   MOVXAST implicit $a, implicit $dpl, implicit $dph, implicit $dpxl  (a3+1)
 *   MOVXAST ...                                                        (a3+2)
 *   MOVXAST ...                                                        (a3)
 *
 * The physical-register dependencies (DPL/DPH/DPXL/A) that the instruction
 * descriptors pin were invisible to the DAG scheduler, which streamed
 * writes by register instead of per access. The later "dead" markers are
 * another consequence of the missing glue, not its cause. After the fix the implicit defs
 * are emitted live and every MOVX follows its own lane writes.
 *
 * Runtime evidence (QEMU, -O2): BEFORE the fix the write/readback check
 * failed for every index except 0 - exactly the signature of all stores
 * landing on the last-written pointer; AFTER the fix an out-of-band -O2
 * probe firmware read back R=5AC381 O2-STORE3-PASS (all three indices
 * correct).
 *
 * check-bytes.py now asserts the GREEN -O2 shape over this image: each
 * MOVX is preceded by its own DPXL/DPL/DPH lane writes since the previous
 * MOVX, and there is no bare-MOVX run.
 */
typedef unsigned char BYTE;

BYTE __xdata a3[3];

void store3(BYTE v0, BYTE v1, BYTE v2)
{
    a3[0] = v0;
    a3[1] = v1;
    a3[2] = v2;
}
