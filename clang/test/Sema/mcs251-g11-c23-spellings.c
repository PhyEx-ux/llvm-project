// RUN: %clang_cc1 -triple mcs251 -std=c23 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251 -std=c23 -ast-print %s | FileCheck %s --check-prefixes=PRINT
// RUN: %clang_cc1 -triple mcs251 -std=c23 -ast-dump %s | FileCheck %s --check-prefixes=DUMP
// RUN: %clang_cc1 -triple mcs251 -std=c23 -ast-dump %s | grep -c "RetainAttr .* Implicit" | FileCheck %s --check-prefixes=IMPLICIT
// RUN: %clang_cc1 -triple mcs251 -std=c23 -O2 -emit-llvm -o - %s | FileCheck %s --check-prefixes=IR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -Werror=unknown-attributes -fsyntax-only -verify=nonmcs %s
//
// G11 second-batch A fix, demo review item 2: the placement attributes must be
// usable with every promised spelling. Before this fix the vendor spelling was
// registered as CXX11<"mcu", "..."> only, and a CXX11 spelling is not visible
// in C mode, so `[[mcu::place_at(...)]]` was rejected in C23 while
// `[[gnu::mcu_place_at(...)]]` worked. The C23 spelling is now registered
// separately (Attr.td), which is why the two syntaxes are exercised side by
// side here.
//
// Eight checks in this one file:
//  1. C23 vendor spelling ([[mcu::...]]) in C23 mode;
//  2. GNU scoped spelling ([[gnu::mcu_...]]) in C23 mode;
//  3. GNU unscoped spelling (__attribute__((mcu_...)));
//  4. the C++ vendor spelling -- mcs251-g11-spellings.cpp (unchanged) covers
//     [[mcu::...]] in C++ mode;
//  5. AST print keeps the spelling the source used (three distinct forms);
//  6. the implicit RetainAttr is created for every retain-carrying entity
//     (DUMP count) -- and it is a RetainAttr of the built-in `retain`
//     attribute, created through RetainAttr::CreateImplicit with its own
//     spelling enumeration, never by copying the ParsedAttr spelling index of
//     the MCS251 attribute (the historical out-of-range spelling-index bug);
//     the class distinction is visible in the dump, and -ast-print/-ast-dump
//     exercise the printing path that would read a bogus index;
//  7. both attribute orders on one declaration, and a retain that arrives
//     across a redeclaration chain;
//  8. at -O2 the retained entities are exactly the ones in llvm.used (the
//     keepalive path that the implicit RetainAttr drives), the bind
//     declarations ride llvm.compiler.used, and the placement strings are the
//     expected ones -- asserted on the IR attributes, not just on exit status.
//
// On x86_64 the attributes do not exist at all; each spelling must produce the
// ordinary ignored-unknown-attribute diagnostic there (with
// -Werror=unknown-attributes so a silently accepted attribute would fail).
// expected-no-diagnostics

// 1. C23 vendor spelling, owned + retain, and the bind form.
[[mcu::place_at(0x100), mcu::retain]] int c23_vendor; // nonmcs-error {{unknown attribute 'mcu::place_at' ignored}} // nonmcs-error {{unknown attribute 'mcu::retain' ignored}}
[[mcu::bind_at(0x200)]] extern int c23_vendor_bind; // nonmcs-error {{unknown attribute 'mcu::bind_at' ignored}}

// 2. GNU scoped spelling in C23 mode, both orders.
[[gnu::mcu_place_at(0x300)]] int c23_gnu_scoped; // nonmcs-error {{unknown attribute 'gnu::mcu_place_at' ignored}}
[[gnu::mcu_retain, gnu::mcu_place_at(0x310)]] int c23_gnu_scoped_reversed; // nonmcs-error {{unknown attribute 'gnu::mcu_retain' ignored}} // nonmcs-error {{unknown attribute 'gnu::mcu_place_at' ignored}}

// 3. GNU unscoped spelling.
__attribute__((mcu_place_at(0x400), mcu_retain)) int c23_gnu; // nonmcs-error {{unknown attribute 'mcu_place_at' ignored}} // nonmcs-error {{unknown attribute 'mcu_retain' ignored}}

// 7. Retain arriving across a redeclaration chain: the attribute is on the
// declaration, the definition follows without it.
[[mcu::place_at(0x500), mcu::retain]] extern int c23_redecl; // nonmcs-error {{unknown attribute 'mcu::place_at' ignored}} // nonmcs-error {{unknown attribute 'mcu::retain' ignored}}
int c23_redecl = 1;


// 5. AST print preserves the spelling forms: the two vendor/scoped forms stay
// as written, the GNU unscoped form is printed as __attribute__((...)).
// PRINT: {{\[\[}}mcu::place_at(256)]] {{\[\[}}mcu::retain]] int c23_vendor;
// PRINT: {{\[\[}}mcu::bind_at(512)]] extern int c23_vendor_bind;
// PRINT: {{\[\[}}gnu::mcu_place_at(768)]] int c23_gnu_scoped;
// PRINT: {{\[\[}}gnu::mcu_retain]] {{\[\[}}gnu::mcu_place_at(784)]] int c23_gnu_scoped_reversed;
// PRINT: __attribute__((mcu_place_at(1024))) __attribute__((mcu_retain)) int c23_gnu;
// PRINT: {{\[\[}}mcu::place_at(1280)]] {{\[\[}}mcu::retain]] extern int c23_redecl;

// 6. One implicit built-in RetainAttr per retain-carrying entity (four of
// them: c23_vendor, c23_gnu_scoped_reversed, c23_gnu, c23_redecl). The MCS251
// attribute keeps its own spelling name (`retain` / `mcu_retain`), while the
// implicit one is a plain RetainAttr whose spelling therefore comes from the
// built-in attribute's own enumeration (RetainAttr::CreateImplicit's default),
// not from the ParsedAttr that carried the MCS251 spelling.
// DUMP: MCS251RetainAttr {{.*}} retain
// DUMP: MCS251RetainAttr {{.*}} mcu_retain
// IMPLICIT: 4

// 8. Keepalive and placement at -O2.
// IR: @llvm.used = appending global [4 x ptr] [
// IR-DAG: ptr @c23_gnu
// IR-DAG: ptr @c23_gnu_scoped_reversed
// IR-DAG: ptr @c23_redecl
// IR-DAG: ptr @c23_vendor
// IR: {{.*}}section "llvm.metadata"
// IR: @llvm.compiler.used = appending global [1 x ptr] [ptr @c23_vendor_bind], section "llvm.metadata"
// IR-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x100,data,object,owned,1" "mcs251-stable-symbol"="c23_vendor" }
// IR-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x200,data,object,bind,0" "mcs251-stable-symbol"="c23_vendor_bind" }
// IR-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x300,data,object,owned,0" "mcs251-stable-symbol"="c23_gnu_scoped" }
// IR-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x310,data,object,owned,1" "mcs251-stable-symbol"="c23_gnu_scoped_reversed" }
// IR-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x400,data,object,owned,1" "mcs251-stable-symbol"="c23_gnu" }
// IR-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x500,data,object,owned,1" "mcs251-stable-symbol"="c23_redecl" }