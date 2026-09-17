// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251 -fsyntax-only -verify %s
//
// G11 second-batch A fix, N5: the aligned attribute is cached as a 32-bit
// count of *bits* (setCachedAlignmentValue(AlignVal * getCharWidth()), see
// Attrs.inc), but MaximumAlignment was 1 << 32 (Sema::MaxAlignmentExponent),
// so an alignment whose bit count does not fit in `unsigned` wrapped to zero
// and the constraint was silently dropped: the object simply kept its natural
// alignment. This is a general Sema defect, not an MCS251/G11 one -- it
// reproduces identically on x86_64, which is why this test runs on both
// triples (the fix is in Sema::AddAlignedAttr and the check is target
// independent; only the numeric bound depends on the target character width,
// which is 8 for both triples here).
//
// Measured on the pre-fix compiler: aligned(0x20000000) (2^29) and
// aligned(1ull << 32) (2^32) both compiled with exit status 0 on mcs251 *and*
// x86_64, and `__alignof__` reported the natural alignment, i.e. the request
// was dropped rather than rejected. The bound is now the largest power of two
// whose bit count is representable: 2^28 bytes at character width 8.
//
// This test also pins the pre-existing upper bound, which must not regress:
// aligned(0x200000000ULL) (2^33) was already rejected before the fix, with the
// same diagnostic text.

// Accepted: exactly the bound (2^28 bytes = 2^31 bits, representable).
int at_bound __attribute__((aligned(0x10000000)));

// Accepted: ordinary alignments far below the bound.
int ordinary __attribute__((aligned(16)));
int natural __attribute__((aligned(1)));

// Rejected: the first value whose bit count overflows the 32-bit cache.
int byte_aligned __attribute__((aligned(0x20000000))); // expected-error {{requested alignment must be 268435456 bytes or smaller}}

// Rejected: the same overflow reached through a shift expression.
int shifted __attribute__((aligned(1ull << 32))); // expected-error {{requested alignment must be 268435456 bytes or smaller}}

// Rejected: one past the shift case.
int shifted_plus_one __attribute__((aligned((1ull << 32) + 1))); // expected-error {{requested alignment must be 268435456 bytes or smaller}}

// Rejected (pre-existing behaviour, unchanged by the fix): above the old bound.
int far_above __attribute__((aligned(0x200000000ULL))); // expected-error {{requested alignment must be 268435456 bytes or smaller}}

// The same code path serves _Alignas (both go through Sema::AddAlignedAttr
// with the same MaximumAlignment), so the overflowing value is rejected there
// too instead of being dropped.
_Alignas(0x20000000) int alignas_overflow; // expected-error {{requested alignment must be 268435456 bytes or smaller}}
_Alignas(16) int alignas_ok;