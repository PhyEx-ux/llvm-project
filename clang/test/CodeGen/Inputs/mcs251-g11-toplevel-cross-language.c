/* G11-N4 cross-language probe (driver TU: C side).
 *
 * Two unmangled declarations with C language linkage, so both identities are
 * the bare declaration identifiers:
 *   c_owned    -- owned  (a definition) at 0x100
 *   cpp_owned  -- bind   (a declaration) at 0x200
 *
 * The companion C++ TU (mcs251-g11-toplevel-cross-language.cpp) declares the
 * *same two names with the OPPOSITE ownership*: `c_owned` is bound there and
 * `cpp_owned` is owned. The test therefore covers both directions of the
 * design §6 requirement (C owned versus C++ bind, and C bind versus C++
 * owned), and it asserts the ownership of every entity on both sides as well
 * as the byte equality of the two identity sets.
 */
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define BIND(A) __attribute__((mcu_bind_at(A)))

int c_owned PLACE(0x100) = 1;
extern int cpp_owned BIND(0x200);
