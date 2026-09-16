// RUN: %clang_cc1 -triple mcs251 -std=c11 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251 -std=c23 -fsyntax-only -verify %s

#define PLACE(A) __attribute__((mcu_place_at(A)))
#define BIND(A) __attribute__((mcu_bind_at(A)))
#define RETAIN __attribute__((mcu_retain))

int fixed PLACE(0x100) = 1;
int fixed_array[4] PLACE(0x110);
void fixed_function(void) PLACE(0x200);
void fixed_function(void) {}
extern int device BIND(0x300);
extern void entry(void) BIND(0x400);
int kept PLACE(0x500) RETAIN;
int kept_reverse RETAIN PLACE(0x510);
int warm PLACE(0x520) __attribute__((noinit));
// Separate storage ledgers: equal numeric addresses in different spaces.
__xdata int far_object PLACE(0x100);
__code int code_object PLACE(0x100) = 7;

void automatic(void) {
  int local PLACE(0); // expected-error {{`mcu::place_at` requires a static object or function definition}}
}
extern int only_decl PLACE(0x600); // expected-error {{`mcu::place_at` requires a static object or function definition}}
extern int kept_decl RETAIN; // expected-error {{`mcu::retain` requires a definition}}
extern int kept_placed_decl PLACE(0x610) RETAIN; // expected-error {{`mcu::retain` requires a definition}}
int kept_plain RETAIN; // expected-error {{`mcu::retain` requires a fixed placement (`place_at`) in this profile}}
extern int kept_bound BIND(0x620) RETAIN; // expected-error {{`mcu::retain` requires a definition}}
extern int kept_bound_reverse RETAIN BIND(0x630); // expected-error {{`mcu::retain` requires a definition}}
extern int kept_chain BIND(0x640);
extern int kept_chain RETAIN; // expected-error {{`mcu::retain` requires a definition}}
static int private_binding BIND(0x700); // expected-error {{`mcu::bind_at` requires an entity with external linkage}}
int bind_storage BIND(0x710); // expected-error {{`mcu::bind_at` cannot have an initializer or function body}}
int bind_init BIND(0x720) = 1; // expected-error {{`mcu::bind_at` cannot have an initializer or function body}}
BIND(0x730) void bind_body(void) {} // expected-error {{`mcu::bind_at` cannot have an initializer or function body}}
extern int mixed PLACE(0x740) BIND(0x740); // expected-error {{mcu::bind_at and mcu::place_at attributes are not compatible}}
int mixed_reverse BIND(0x750) PLACE(0x750); // expected-error {{mcu::bind_at and mcu::place_at attributes are not compatible}}
extern int mixed_chain PLACE(0x760);
extern int mixed_chain BIND(0x760); // expected-error {{mcu::bind_at and mcu::place_at attributes are not compatible}}
int twice PLACE(0x800) PLACE(0x810); // expected-error {{conflicting `mcu::place_at` addresses 0x800 and 0x810}}
extern int twice_bind BIND(0x820) BIND(0x830); // expected-error-re {{conflicting placement for twice_bind: 0x820 ({{[^()]*mcs251-g11-placement\.c}}) vs 0x830 ({{[^()]*mcs251-g11-placement\.c}})}}
extern int chain PLACE(0x840);
int chain PLACE(0x850); // expected-error {{conflicting `mcu::place_at` addresses 0x840 and 0x850}}
extern int chain_bind BIND(0x860);
extern int chain_bind BIND(0x870); // expected-error-re {{conflicting placement for chain_bind: 0x860 ({{[^()]*mcs251-g11-placement\.c}}) vs 0x870 ({{[^()]*mcs251-g11-placement\.c}})}}
int zero[0] PLACE(0x900); // expected-error {{fixed placement entity 'zero' must have a non-zero size}}
extern int zero_bind[0] BIND(0x910); // expected-error {{fixed placement entity 'zero_bind' must have a non-zero size}}
struct incomplete;
extern struct incomplete incomplete_bind BIND(0x920); // expected-error {{fixed placement requires a complete type}}
int align PLACE(0x931) __attribute__((aligned(4))); // expected-error {{placement address 0x931 does not satisfy alignment 4}}
extern int align_bind BIND(0x941) __attribute__((aligned(4))); // expected-error {{placement address 0x941 does not satisfy alignment 4}}
extern void align_fn(void) BIND(0x951) __attribute__((aligned(4))); // expected-error {{placement address 0x951 does not satisfy alignment 4}}
char occupied[8] PLACE(0x960);
char overlaps PLACE(0x964); // expected-error {{fixed placement range [0x964, 0x965) overlaps another entity}}
int noinit_init PLACE(0x980) __attribute__((noinit)) = 0; // expected-error {{`noinit` cannot be combined with an initializer}}
extern int noinit_chain __attribute__((noinit)); // expected-error {{`noinit` cannot be combined with an initializer}}
int noinit_chain PLACE(0x990) = 1;
int plain_noinit __attribute__((noinit)); // expected-error {{`noinit` requires a fixed placement (`place_at`) in this profile}}
int negative PLACE(-1); // expected-error {{placement address -1 is not representable in the MCS-251 address model}}
int huge PLACE(0x1000000); // expected-error {{placement address 16'777'216 is not representable in the MCS-251 address model}}
int missing __attribute__((mcu_place_at)); // expected-error {{'mcu_place_at' attribute takes one argument}}
int extra __attribute__((mcu_bind_at(1, 2))); // expected-error {{'mcu_bind_at' attribute takes one argument}}
int runtime;
int nonconstant PLACE(runtime); // expected-error {{expression is not an integer constant expression}}
int string PLACE("bad"); // expected-error {{expression is not an integer constant expression}}
int retained_arg __attribute__((mcu_retain(1))); // expected-error {{'mcu_retain' attribute takes no arguments}}
int as5 __attribute__((address_space(5))); // expected-error {{address space 5 is not defined by the MCS-251 memory contract}}
int as_negative __attribute__((address_space(-1))); // expected-error {{address space is negative}}
