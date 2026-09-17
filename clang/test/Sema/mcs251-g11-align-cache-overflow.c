// RUN: %clang_cc1 -triple mcs251 -fsyntax-only -verify %s
//
// G11 second-batch A fix, N5, in the G11 context: a placed object whose
// `aligned` request overflows the 32-bit alignment cache must be rejected at
// the attribute, exactly like any other object -- the G11 placement profile
// must not become a way to smuggle a silently dropped alignment constraint
// into a fixed placement (the dropped constraint would let a placement violate
// the alignment the author asked for while the frontend reports no error).
// The underlying defect and its target-independence are covered by
// attr-aligned-overflow-cache.c, which runs on x86_64 as well.
#define PLACE(A) __attribute__((mcu_place_at(A)))

// Rejected: the overflowing alignment is diagnosed by the ordinary aligned
// attribute rule; the placement address itself is legal, so this diagnostic is
// the only one.
int bad_align PLACE(0x100) __attribute__((aligned(0x20000000))); // expected-error {{requested alignment must be 268435456 bytes or smaller}}

// Rejected: same overflow via a shift, in the reverse attribute order.
int bad_align_reverse __attribute__((aligned(1ull << 32))) PLACE(0x110); // expected-error {{requested alignment must be 268435456 bytes or smaller}}

// Accepted controls: a legitimate alignment still combines with placement, and
// it is genuinely enforced by the placement rule -- the address that does not
// satisfy it is diagnosed, which proves the constraint is live rather than
// silently dropped. (The 2^28 bound itself cannot be combined with a placement
// here: satisfying it would require an address outside the MCS-251 AS0 range,
// so the attribute-level bound is covered by the generic test.)
int ok_align PLACE(0x200) __attribute__((aligned(4)));
int bad_address PLACE(0x201) __attribute__((aligned(4))); // expected-error {{placement address 0x201 does not satisfy alignment 4}}