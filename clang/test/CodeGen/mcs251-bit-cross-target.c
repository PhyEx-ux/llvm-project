// M2-5 regression: every CodeGen dispatch on the ID of
// __builtin_mcs251_bit_lvalue (the MCS251 target-builtin enum) is
// double-guarded by the target triple. Target builtin IDs are per-target
// enumerations that all start at clang::Builtin::FirstTSBuiltin, so the same
// numeric ID names a different builtin on each target: on
// x86_64-pc-windows-msvc it belongs to _AddressOfReturnAddress, and without
// the triple guard a legal Windows x86_64 unit using it (Alice M2 r2 probe)
// was misrouted into EmitLoadOfLValue and crashed the compiler (SIGSEGV in
// codegen).
//
// The x86 and aarch64 runs below are driver-layer replays of that probe.
// This build's clang has the x86_64 and aarch64 frontends compiled in, so
// the runs execute directly (no REQUIRES/UNSUPPORTED and no system-clang
// fallback needed).

// RUN: %clang --target=x86_64-pc-windows-msvc -fms-extensions -S -emit-llvm -o - %s | FileCheck %s --check-prefix=X86
// RUN: %clang --target=aarch64-linux-gnu -S -emit-llvm -o - %s | FileCheck %s --check-prefix=AARCH64
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o - %s | FileCheck %s --check-prefix=MCS251

#ifdef __mcs251__
// The MCS-251 run: the guarded dispatch still fires on its own target and
// the controlled fixed bit builtin lowers through the bit intrinsics.
sbit A = 0x80;
int read_builtin(void) { return __builtin_mcs251_bit_lvalue(0x80); }
void write_sbit(void) { A = 1; }
// MCS251: define {{.*}}@read_builtin(
// MCS251: call {{.*}}i1 @llvm.mcs251.bit.read(i32 128)
// MCS251: define {{.*}}@write_sbit(
// MCS251: call {{.*}}void @llvm.mcs251.bit.set(i32 128)
#elif defined(__x86_64__)
// The x86_64-pc-windows-msvc run: the numerically colliding builtin
// _AddressOfReturnAddress must compile rc=0 with its normal lowering.
void *_AddressOfReturnAddress(void);
#pragma intrinsic(_AddressOfReturnAddress)
void *f(void) { return _AddressOfReturnAddress(); }
// X86: define {{.*}}ptr @f()
// X86: call ptr @llvm.addressofreturnaddress
#else
// Any other target: ordinary code compiles and the MCS251 dispatch never
// fires (no llvm.mcs251.bit.* anywhere).
int f(void) { return 42; }
// AARCH64: define {{.*}}i32 @f()
// AARCH64: ret i32 42
// AARCH64-NOT: mcs251
#endif
