// RUN: %clang_cc1 -triple mcs251 -fsyntax-only -verify %s
// expected-no-diagnostics
// G11 design §3.2/§3.3: Sema has no section span for a function. It must
// neither invent a one-byte range nor compare CODE against the DATA ledger.
// These entries are deliberately close; G11-B/C must reject a real overlap
// using NOTE.size == sh_size (including jump tables) in reserveCode.
__attribute__((mcu_place_at(0x100))) void first(void) {}
__attribute__((mcu_place_at(0x104))) void second(void) {}
int data __attribute__((mcu_place_at(0x100)));
// The discriminating inputs for the removed one-byte sentinel: with a sentinel
// at the function address, `same_address` would collide with `first` as
// [0x100, 0x101) vs [0x100, 0x101) and `same_as_second` with `second`.
// Sema knows no function span at all, so equal function addresses (and a
// function next to the DATA entity of the other ledger) are accepted here.
__attribute__((mcu_place_at(0x100))) void same_address(void) {}
__attribute__((mcu_place_at(0x104))) void same_as_second(void) {}
// An object of the DATA ledger shares the numeric address without any
// cross-ledger diagnostic (independent storage classes).
__xdata int far_data __attribute__((mcu_place_at(0x108)));