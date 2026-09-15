// AS3 (`__xdata`) pointer-parameter ABI: IR preservation (R2) and the
// v2 compatibility-ABI lowering shapes (R3). Promoted from the G5 probe
// evidence GAP-G5-PROBES/rev1/r3ctu/ (see
// validation/mcs251-models/proposals/G5-ADDRSPACE-DESIGN-draft.md 6.2).
//
// R2 (IR): an `__xdata` parameter keeps `ptr addrspace(3)` end to end --
// definition signature, call argument, GEP and store -- with no silent
// addrspacecast down to AS0 anywhere in the module.
//
// R3 (ABI shapes): parameter 1 arrives in the B/DPH/DPL registers (24-bit
// address + DPXL bank); parameters 2+ live in `_f_PARM_n` static slots in
// the OSEG overlay area, an AS3 pointer slot being 4 bytes (24-bit address
// + bank byte). Stores through an AS3 parameter must go the full 24-bit
// MOVX @DPTR channel after restoring the DPXL bank byte (SFR 0x84), and an
// AS3 object definition lands in XSEG. The linker-side placement of these
// shapes (cross-TU ok.map) stays with the rev1 probe; this file pins the
// per-TU contract that the linker consumes.

// REQUIRES: mcs251-registered-target

// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR --implicit-check-not=addrspacecast
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o %t.ll %s
// RUN: llc -mtriple=mcs251-unknown-none -mcs251-object-format=elf -filetype=obj -o %t.o %t.ll
// RUN: llvm-readelf -s %t.o | FileCheck %s --check-prefix=SYM
// RUN: llvm-readelf -S %t.o | FileCheck %s --check-prefix=SEC
// The textual-asm run needs a TU without AS3 global storage (the ASxxxx
// streamer rejects __xdata globals; slots and instruction selection do not
// depend on the definition).
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -DXDATA_GLOBAL_EXTERN -emit-llvm -o %t-asm.ll %s
// RUN: llc -mtriple=mcs251-unknown-none -filetype=asm -o - %t-asm.ll | FileCheck %s --check-prefix=ASM

#ifdef XDATA_GLOBAL_EXTERN
extern unsigned char __xdata DmaTxBuffer[16];
#else
unsigned char __xdata DmaTxBuffer[16];
#endif

void fill(unsigned char __xdata *p, unsigned char v, unsigned char __xdata *q);

// IR: @DmaTxBuffer = addrspace(3) global [16 x i8] zeroinitializer, align 1
// IR-NOT: addrspacecast
// IR-LABEL: define dso_local void @dispatch(ptr addrspace(3) noundef %src) addrspace(4)
// IR: call addrspace(4) void @fill(ptr addrspace(3) noundef %{{.*}}, i8 noundef zeroext 90, ptr addrspace(3) noundef @DmaTxBuffer)
// IR-NOT: addrspacecast
// IR-LABEL: define dso_local void @fill(ptr addrspace(3) noundef %p, i8 noundef zeroext %v, ptr addrspace(3) noundef %q) addrspace(4)
// IR: getelementptr inbounds i8, ptr addrspace(3)
// IR: store i8 %{{.*}}, ptr addrspace(3)
// IR: getelementptr inbounds i8, ptr addrspace(3)
// IR: store i8 -91, ptr addrspace(3)
// IR-NOT: addrspacecast
void dispatch(unsigned char __xdata *src) {
  fill(src, 0x5A, &DmaTxBuffer[0]);
}

void fill(unsigned char __xdata *p, unsigned char v, unsigned char __xdata *q) {
  p[0] = v;
  q[0] = 0xA5;
}

// Symbol level: `_fill_PARM_2` (byte param) and `_fill_PARM_3` (AS3 pointer
// param) are 1- and 4-byte objects; the first parameter is register-passed
// and never materializes a `_PARM_1` slot.
// SYM: FUNC GLOBAL DEFAULT {{[0-9]+}} _dispatch
// SYM: {{[0-9]+}} 1 OBJECT GLOBAL DEFAULT {{[0-9]+}} _fill_PARM_2
// SYM: {{[0-9]+}} 16 OBJECT GLOBAL DEFAULT {{[0-9]+}} _DmaTxBuffer
// SYM: {{[0-9]+}} 4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _fill_PARM_3
// SYM: FUNC GLOBAL DEFAULT {{[0-9]+}} _fill
// SYM-NOT: _PARM_1

// Section level: the two static slots share the 5-byte OSEG overlay; the
// AS3 object definition lands in XSEG.
// SEC: .mcs251.OSEG{{.*}} NOBITS {{.*}} 000005
// SEC: .mcs251.XSEG._DmaTxBuffer{{.*}} 000010

// asm: caller side -- the incoming AS3 pointer arrives as B/DPH/DPL
// (bank:high:low), the callee's pointer argument is staged through the
// 4-byte `_fill_PARM_3` slot before the call.
// ASM-LABEL: _dispatch:
// ASM: mov r{{[0-9]+}}, b
// ASM: mov r{{[0-9]+}}, dph
// ASM: mov r{{[0-9]+}}, dpl
// ASM: mov b, r{{[0-9]+}}
// ASM: ecall _fill
//
// asm: static slots -- OVR/DATA overlay, AS3 pointer slot is 4 bytes.
// ASM: .area OSEG (OVR,DATA)
// ASM: _fill_PARM_2:
// ASM-NEXT: .ds 1
// ASM: _fill_PARM_3:
// ASM-NEXT: .ds 4
//
// asm: callee side -- both AS3 stores restore the DPXL bank byte (SFR
// 0x84) and write through the 24-bit MOVX @DPTR channel.
// ASM-LABEL: _fill:
// ASM: mov 0x84, r{{[0-9]+}}
// ASM-NEXT: mov dpl, r{{[0-9]+}}
// ASM-NEXT: mov dph, r{{[0-9]+}}
// ASM: movx @dptr, a
// ASM: mov 0x84, r{{[0-9]+}}
// ASM-NEXT: mov dpl, r{{[0-9]+}}
// ASM-NEXT: mov dph, r{{[0-9]+}}
// ASM: movx @dptr, a
