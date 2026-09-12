// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

// X1 IR contract: the `__xdata`/`__code` qualifiers must land as LLVM address
// spaces on the global object / pointee types (AS3 = XDATA, AS4 = CODE;
// DESIGN.md B.1). The function address space itself is AS4 (DataLayout P4),
// so function definitions carry addrspace(4) as well. Backend lowering of
// AS3/AS4 data access is the X2 slice; this slice only pins the IR.

typedef unsigned char BYTE;

BYTE __xdata UsbBuffer[256];
// CHECK-DAG: @UsbBuffer = addrspace(3) global [256 x i8] zeroinitializer, align 1

char __code DEVICEDESC[18] = {0x12, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
                              0x34, 0x12, 0xEF, 0xCD, 0x01, 0x01, 0x00, 0x00,
                              0x00, 0x00};
// A2a: `__code` implies const for object types, so the CODE global is
// emitted as a read-only LLVM global while keeping AS4.
// CHECK-DAG: @DEVICEDESC = addrspace(4) constant [18 x i8] c"\12\01\00\00\00\00\00@4\12\EF\CD\01\01\00\00\00\00", align 1

static char __xdata slocal;
char slocal_read(void) { return slocal; }
// CHECK-DAG: @slocal = internal addrspace(3) global i8 0, align 1

extern char __xdata edecl[4];
char edecl_read(unsigned i) { return edecl[i]; }
// CHECK-DAG: @edecl = external addrspace(3) global [4 x i8]

char __code *romptr;
// CHECK-DAG: @romptr = global ptr addrspace(4) null, align 1

// Reading an __xdata object emits an AS3 load through a ptr addrspace(3).
BYTE xdata_read(unsigned i) { return UsbBuffer[i]; }
// CHECK-LABEL: define dso_local zeroext i8 @xdata_read(
// CHECK: getelementptr inbounds nuw [256 x i8], ptr addrspace(3) @UsbBuffer
// CHECK: load i8, ptr addrspace(3) %{{.*}}

// Writing an __xdata object emits an AS3 store.
void xdata_write(unsigned i, BYTE v) { UsbBuffer[i] = v; }
// CHECK-LABEL: define dso_local void @xdata_write(
// CHECK: store i8 %{{.*}}, ptr addrspace(3) %{{.*}}

// Reading a __code object emits an AS4 load (no store to AS4 exists).
char code_read(unsigned i) { return DEVICEDESC[i]; }
// CHECK-LABEL: define dso_local signext i8 @code_read(
// CHECK: load i8, ptr addrspace(4) %{{.*}}

// Taking the address of a __code object yields ptr addrspace(4).
char __code *take_addr(void) { return &DEVICEDESC[0]; }
// CHECK-LABEL: define dso_local ptr addrspace(4) @take_addr(
// CHECK: ret ptr addrspace(4) @DEVICEDESC

// The only legal crossing in IR is an explicit addrspacecast; no bitcast and
// no silent truncation of the 32-bit AS3/AS4 containers.
char *to_default(void) { return (char *)UsbBuffer; }
// CHECK-LABEL: define dso_local ptr @to_default(
// CHECK: ret ptr addrspacecast (ptr addrspace(3) @UsbBuffer to ptr)
