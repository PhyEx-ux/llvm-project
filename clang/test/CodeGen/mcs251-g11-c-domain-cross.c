// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -DNEGATIVE -verify %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O0 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O1 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O3 -emit-llvm -o - %s | FileCheck %s
// G11 identity input boundary (G11-A sixth round): in C mode the host
// component of a local static's stable symbol is the host's plain declaration
// name -- the only component domain that can equal an Itanium mangled
// identity (always "_Z"-prefixed). The first RUN compiles the R5 cross-domain
// probe (a plain C host *declared* with the exact Itanium spelling of the
// overloadable host below, kept distinct at IR level by an asm label) and
// requires the Sema input-boundary rejection: the reserved "_Z"-prefixed host
// name must not be silently encoded into a stable symbol that the overloadable
// host's static also produces (the 12/12 collision of review R5). A "_Z"-
// spelled file-scope static is *not* rejected: it has no host component and
// its identity domain is disjoint by dot-field count.
// The remaining RUNs compile the renamed (legal) TU: the only fix for the
// rejected input is renaming the host. Both statics' identities are then
// distinct by construction, keep their exact placements, stay kept alive,
// and every RUN level asserts the very same identity strings (byte-stable
// across -O0/-O1/-O2/-O3).
#define P(A) __attribute__((mcu_place_at(A), mcu_retain))

#ifdef NEGATIVE
// Accepted (no host component, 3-field identity domain): the boundary must
// not over-reject file-scope entities.
static int _Zfile_scope_sink P(0x400);
// The R5 probe, verbatim. The plain C host's declaration name "_Z4hosti" is
// byte-equal to the Itanium encoding of the overloadable `host(int)` below;
// the asm label only keeps the two *hosts* apart at IR level.
void _Z4hosti(int a) __asm__("plain_host");
void _Z4hosti(int a) { // expected-error {{MCS251 fixed placement: host function '_Z4hosti' has a reserved '_Z'-prefixed name that invades the Itanium mangled identity namespace; rename the host function}}
  static int x P(0x100);
}
#endif

// Renamed host: identity uses the declaration name `plainhost_c` (never the
// asm label), so it cannot intersect the mangled domain of `host`'s static.
void plainhost_c(int a) __asm__("plain_host");
void plainhost_c(int a) {
  static int x P(0x300);
}
void __attribute__((overloadable)) host(int a) {
  static int x P(0x200);
}

// The two IR globals keep their distinct emission names (the asm label routes
// the unmangled host's static to "\01plain_host.x"; the overloadable host's
// static uses its mangled host name). Only the stable-symbol strings are
// pinned exactly; the TU hash nibbles are path-derived and matched loosely.
// CHECK-DAG: @"\01plain_host.x" = internal global i32 0, align 1 #[[PC:[0-9]+]]
// CHECK-DAG: @_Z4hosti.x = internal global i32 0, align 1 #[[HX:[0-9]+]]
// The keepalive set is exactly the two retained objects (the hosts themselves
// are unplaced, so only their statics are kept alive).
// CHECK: @llvm.used = appending global [2 x ptr] [
// CHECK-DAG: ptr @"\01plain_host.x"
// CHECK-DAG: ptr @_Z4hosti.x
// CHECK: ], section "llvm.metadata"
// Identities: distinct host components (`plainhost_c` vs the mangled
// `_Z4hosti`), same variable name -- no collision in any RUN level.
// CHECK: attributes #[[PC]] = { "mcs251-place"="0x300,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_c_domain_cross_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}.plainhost_c.x" }
// CHECK: attributes #[[HX]] = { "mcs251-place"="0x200,data,object,owned,1" "mcs251-stable-symbol"="mcs251_g11_c_domain_cross_c.{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}._Z4hosti.x" }
