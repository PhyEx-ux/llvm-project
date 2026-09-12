// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

// X1 official-source shapes under the Keil dialect: the bare spellings
// `xdata`/`code` must produce exactly the same IR address spaces as
// `__xdata`/`__code` (XDATA = addrspace(3), CODE = addrspace(4)).

typedef unsigned char BYTE;
typedef unsigned short WORD;

// Official shape 1: `extern BYTE xdata UsbBuffer[256];` (object in XDATA).
extern BYTE xdata UsbBuffer[256];
// CHECK-DAG: @UsbBuffer = external addrspace(3) global [256 x i8], align 1

// Official shape 2: `BYTE xdata *pdat;` (pointer to XDATA).
BYTE xdata *pdat;
// CHECK-DAG: @pdat = global ptr addrspace(3) null, align 1

// Official shape 3: `char code DEVICEDESC[18]` (ROM constant table).
char code DEVICEDESC[18] = {0x12, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
                            0x34, 0x12, 0xEF, 0xCD, 0x01, 0x01, 0x00, 0x00,
                            0x00, 0x00};
// A2a: the bare `code` word builds the same implicitly-const AS4 type as
// `__code`, so the object is emitted as a read-only LLVM global.
// CHECK-DAG: @DEVICEDESC = addrspace(4) constant [18 x i8] c"\12\01\00\00\00\00\00@4\12\EF\CD\01\01\00\00\00\00", align 1

// Qualifier position and declspec position must agree.
WORD xdata xword;
// CHECK-DAG: @xword = addrspace(3) global i16 0, align 1
char code cscalar = 'a';
// A2a: implicit const applies to the scalar spelling too.
// CHECK-DAG: @cscalar = addrspace(4) constant i8 97, align 1

BYTE xdata *xdata_pick(BYTE xdata *p) { return p; }
// CHECK-LABEL: define dso_local ptr addrspace(3) @xdata_pick(
// CHECK-SAME: ptr addrspace(3) noundef %p

char code *code_pick(void) { return &DEVICEDESC[1]; }
// CHECK-LABEL: define dso_local ptr addrspace(4) @code_pick(
// CHECK: ret ptr addrspace(4) getelementptr inbounds nuw (i8, ptr addrspace(4) @DEVICEDESC

BYTE xdata_read(unsigned i) { return UsbBuffer[i]; }
// CHECK-LABEL: define dso_local zeroext i8 @xdata_read(
// CHECK: load i8, ptr addrspace(3) %{{.*}}
