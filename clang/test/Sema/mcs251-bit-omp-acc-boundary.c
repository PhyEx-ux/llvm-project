// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fopenmp -fblocks -Wno-unused-value -Wno-varargs -Wno-empty-body -verify=pch %t/pchgen.c -emit-pch -o %t/bit.pch
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fopenmp -fblocks -Wno-unused-value -Wno-varargs -Wno-empty-body -Wno-openmp-target -fsyntax-only -verify=omp -include-pch %t/bit.pch %t/omp.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fopenacc -fblocks -Wno-unused-value -Wno-varargs -Wno-empty-body -fsyntax-only -verify=acc %t/acc.c
//
// P08 revision (Alice ruling, plan B): the OpenMP/OpenACC construct boundary
// for MCS251 bit capabilities. Representative matrix (not a cartesian
// product):
//   * three bit carriers: old-style `sbit`, an ordinary `__bit` object
//     (through typedef and cv-qualifiers, global/parameter storage), and the
//     L1 fixed-bit builtin (by builtin ID);
//   * unnamed bit constructions: bit compound literal, cast to `__bit`,
//     reference-free local bit declaration, bit-returning function call;
//   * placement: clause input, associated statement body, explicit capture /
//     private copies, standalone (body-less) directives, declarative
//     directives (arguments, associated declarations, persistent
//     declare-target regions, owned function definitions), nested constructs;
//   * spelling: macro expansion, _Pragma, PCH-imported declarations;
//   * unevaluated operands and dead branches are still rejected (range
//     restriction, not access analysis);
//   * recovery: a construct error does not leak into the next function; bit
//     outside constructs and ordinary objects/Booleans inside constructs are
//     unaffected; casting to int or comma-wrapping does not launder the bit.
//
// Exactly one error (first violation) plus one note (pragma) per construct.

//--- pchgen.c
// pch-no-diagnostics
// Declarations imported through a PCH carry the bit capability by semantic
// identity; a construct in the importing TU is checked against them.
__bit PG;
sbit PS = 0x30;
typedef __bit PT;
typedef __bit (*PFP)(void);
extern PFP PFP_v;

//--- omp.c
sbit X = 0x24;   // carrier: sbit fixed-address identity
__bit G;         // carrier: ordinary bit object
typedef __bit BT;
volatile BT V;   // typedef + cv over the bit type
extern __bit PF(__bit p); // bit-returning function with a bit parameter

// --- clause input, per carrier -------------------------------------------

void clause_sbit(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel if (X)
  {}
}
void clause_bit_object(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel if (G)
  {}
}
void clause_typedef_cv(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel if (V)
  {}
}
void clause_builtin(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel if (__builtin_mcs251_bit_lvalue(0x25))
  {}
}
void clause_parameter(int *p) {
  // A bit-typed formal parameter of the enclosing function, referenced in a
  // clause: same semantic identity.
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp task depend(in : p[PF(0)])
  {}
}

// --- body input -----------------------------------------------------------

void body_bit_object(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { G = 1; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void body_local_bit_no_ref(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { __bit b; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void body_bit_compound_literal(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { (__bit){1}; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void body_bit_cast(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { if ((__bit)1) ; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void body_typedef_decl(void) {
  // Introducing a typedef of bit inside the construct.
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { typedef __bit T2; (void)sizeof(int); } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void body_bit_return_call(void) {
  // Calling (even just naming) a bit-returning function.
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { (void)PF(1); } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}

// --- capture / private copies ----------------------------------------------

void capture_shared(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel shared(G)
  {}
}
void capture_private_firstprivate(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel private(G) firstprivate(X)
  {}
}
void capture_implicit(void) {
  // No clause at all: the body reference registers the implicit capture, and
  // the reference itself is already construct input.
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { int t = G; (void)t; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}

// --- standalone (body-less) directives --------------------------------------

void standalone_target_enter_data(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp target enter data map(to : G)
}

// --- declarative directives --------------------------------------------------

void dt_arg_bit_object(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp declare target to(G)
}
void dt_arg_bit_function(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp declare target to(PF)
}

#pragma omp begin declare target // omp-note {{entered OpenMP construct here}}
__bit RG; // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
sbit RS = 0x31; // no-error: the latch reports the first violation of this region only
int plain_ok_in_region; // ordinary declarations stay unaffected
#pragma omp end declare target

// A function marked declare target owns its definition region: the
// restriction follows the definition even when parsed far from the pragma.
void dt_owned_decl(void);
#pragma omp declare target to(dt_owned_decl) // omp-note {{entered OpenMP construct here}}
void dt_owned_decl(void) {
  X = 1; // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
// The same function without bit use stays fine.
void dt_clean(void);
#pragma omp declare target to(dt_clean)
void dt_clean(void) {
  int t = 1;
  (void)t;
}

// --- nested constructs --------------------------------------------------------

void nested_inner_owns_violation(void) {
#pragma omp parallel
  {
    // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp for private(G)
    for (int i = 0; i < 2; i++)
      {}
  }
}
void outer_reported_once_across_nested(void) {
  // The outer construct is rejected at its clause (first violation) and is
  // not diagnosed again for bit input that follows a nested construct which
  // reported its own violation; the nested construct still gets exactly its
  // own one error + note.
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel if (G)
  {
    // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp for private(G)
    for (int i = 0; i < 2; i++)
      {}
    G = 1; // no-error: this construct already reported its first violation
  }
}

// --- spelling: macro, _Pragma, PCH import --------------------------------------

#define CLAUSE_IF_BIT(V) if (V)
void macro_expanded_bit(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel CLAUSE_IF_BIT(G)
  {}
}
#define EXPANDS_TO_NOTHING()
void macro_leaves_nothing(void) {
  // A macro that expands to nothing leaves no construct input: accepted.
#pragma omp parallel EXPANDS_TO_NOTHING()
  {}
}
void pragma_operator_form(void) {
  void h(void);
  _Pragma("omp parallel") // omp-note {{entered OpenMP construct here}}
  {
    X = 1; // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
  }
}
// PCH-imported declarations (PG/PS/PT from bit.pch).
void pch_imported_object(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel shared(PG)
  {}
}
void pch_imported_sbit(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel if (PS)
  {}
}

// --- final rework: capability established through signatures (F1) --------------

typedef __bit (*bit_fn)(void);
extern bit_fn fp;
typedef void (*bit_arg_fn)(__bit);
extern bit_arg_fn fp2;
struct sig_member { __bit (*fp)(void); };
extern struct sig_member sm;

void sig_indirect_return(void) {
  // A function pointer whose signature returns bit: the pointer object itself
  // carries the capability; the call needs no bit-typed decl at all.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)fp(); }
}
void sig_indirect_parameter(void) {
  // A bit parameter in the signature: an ordinary int argument converts to
  // bit at the call, but the referenced pointer already carries bit.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { fp2(1); }
}
void sig_member_field(void) {
  // A struct field of bit-signature pointer type: the member reference is
  // construct input (member expressions bypass BuildDeclRefExpr).
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)sm.fp(); }
}
void sig_local_fn_typedef(void) {
  // A typedef of a function type returning bit, declared inside the region.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { typedef __bit F(void); }
}
void sig_pch_imported_fnptr(void) {
  // PCH-imported fn-ptr-to-bit-signature object (PFP_v from bit.pch).
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)PFP_v(); }
}
// Positive controls: bit-free signatures and siblings of bit-signature fields.
typedef int (*int_fn)(void);
extern int_fn ifp;
struct sig_plain_member { int (*fp)(void); int n; };
extern struct sig_plain_member spm;
struct sig_bit_sibling { __bit (*fp)(void); int n; };
extern struct sig_bit_sibling sbs;
int idfn(void) { return 1; }
void sig_positives(void) {
#pragma omp parallel
  {
    (void)ifp();
    (void)spm.fp();
    spm.n = 1;
    sbs.n = 1; // the struct itself is not the capability; only fp is
    (void)idfn();
  }
}

// --- final rework: block signatures and va_arg (F2/F3) --------------------------

void block_bit_return(void) {
  // A block literal whose signature returns bit (needs -fblocks).
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)(^__bit(void){ return 1; })(); }
}
void block_bit_parameter(void) {
  // A bit parameter of the block literal's signature.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)(^void(__bit p){ })(1); }
}
void block_positive(void) {
#pragma omp parallel
  { (void)(^int(void){ return 1; })(); }
}
void va_bit_in_construct(__builtin_va_list ap) {
  // va_arg establishing a bit value inside the construct.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)__builtin_va_arg(ap, __bit); }
}
void va_int_positive(__builtin_va_list ap) {
#pragma omp parallel
  {
    int v = __builtin_va_arg(ap, int);
    (void)v;
  }
}

// --- final rework: declare simd / declare variant owned definitions (F4) --------

void simd_remote(void);
#pragma omp declare simd
void simd_remote(void);
void simd_remote(void) {
  X = 1; // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
  // omp-note@-4 {{entered OpenMP construct here}}
}
void simd_remote_clean(void);
#pragma omp declare simd
void simd_remote_clean(void);
void simd_remote_clean(void) {
  int t = 1;
  (void)t;
}
void dv_variant(void);
void dv_base(void);
#pragma omp declare variant(dv_variant) match(user = {condition(1)})
void dv_base(void);
void dv_base(void) {
  X = 1; // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
  // omp-note@-4 {{entered OpenMP construct here}}
}
void unmarked_remote(void);
void unmarked_remote(void) {
  X = 1; // no-error: no construct owns this definition
}

// --- final rework 2b: cast/literal types, deep chains, pure type input ------

typedef __bit (*sig_F)(void);
typedef void (*sig_A)(__bit);
typedef int (*sig_I)(void);
typedef sig_I *******sig_IP; // deep clean chain (7 pointer levels)
typedef __bit (********sig_PP)(void); // deep bit chain (7 pointer levels)
extern sig_PP sig_pp;
extern sig_IP sig_ipp;

void cast_bit_signature(void *raw) {
  // Casting to a bit-signature pointer type constructs the capability.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)((sig_F)raw)(); }
}
void cast_bit_param_signature(void) {
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { ((sig_A)0)(1); }
}
void compound_literal_bit_signature(void) {
  // A compound literal of bit-signature pointer type.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)(sig_F){0}; }
}
void block_inferred_bit_return(void *raw) {
  // A block with a deduced bit return type: the deduction is driven by a
  // cast to a bit-signature pointer, which is itself construct input.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)(^{ return ((sig_F)raw)(); })(); }
}
void deep_pointer_chain(void) {
  // 7+ pointer levels over a bit-signature: the walk has no depth cutoff
  // (pointer/array/signature chains are acyclic without records).
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)((*******sig_pp))(); }
}
void deep_pointer_chain_clean(void) {
  // The same depth over a bit-free signature stays accepted.
#pragma omp parallel
  { (void)((*******sig_ipp))(); }
}
void type_trait_bit_argument(void) {
  // Pure type input: a trait naming the bit type.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)__builtin_types_compatible_p(__bit, int); }
}
void generic_bit_association(void) {
  // Pure type input: a _Generic association type.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)_Generic(0, __bit : 1, default : 0); }
}
void sizeof_bit_signature(void) {
  // Pure type input: sizeof/_Alignof of a bit-signature pointer type.
  // omp-error@+3 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  {
    (void)sizeof(sig_F);
    (void)_Alignof(sig_F);
  }
}
void pure_type_positives(void) {
#pragma omp parallel
  {
    (void)sizeof(int);
    (void)_Alignof(int);
    (void)_Generic(0, int : 1, default : 0);
    (void)__builtin_types_compatible_p(int, long);
  }
}
// Outside constructs these pure-type and cast/literal forms keep their
// current (accepted) behavior; the boundary is construct-internal only.
void outside_type_inputs(void *raw) {
  (void)sizeof(sig_F);
  (void)_Alignof(sig_F);
  (void)_Generic(0, __bit : 1, default : 0);
  (void)__builtin_types_compatible_p(__bit, int);
  (void)((sig_F)raw)();
  (void)(sig_F){0};
}

// --- final rework 3: __builtin_bit_cast destination type (F8), _Atomic (F9) ---

void bitcast_bit_destination(void) {
  // __builtin_bit_cast is parsed as a cast-like expression and never becomes
  // a CallExpr, so the builtin-by-ID entry does not see it; its destination
  // type is construct input at its own type-establishment entry.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)__builtin_bit_cast(__bit, (unsigned char)1); }
}
void bitcast_bit_signature_call(void *raw) {
  // A bit-signature destination type; the result is called for a bit value.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)__builtin_bit_cast(sig_F, raw)(); }
}
void bitcast_inferred_block(void *raw) {
  // The same bitcast driving the return of a deduced (block) closure.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)(^{ return __builtin_bit_cast(sig_F, raw)(); })(); }
}
void atomic_sizeof_bit_signature(void) {
  // `_Atomic(sig_F)` wraps the bit-signature pointer; the walk recurses the
  // atomic value type (C11 6.7.2.4p1 forbids _Atomic(_Atomic(T)), so the
  // atomic layer adds no cycle).
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)sizeof(_Atomic(sig_F)); }
}
void atomic_local_declaration(void) {
  // A local declaration of the atomic-wrapped bit-signature type; the
  // ordinary declarator entry reports the (now visible) capability. WP4 A4
  // additionally rejects the _Atomic object declaration itself.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { _Atomic(sig_F) af; (void)af; } // omp-error {{atomic types}} // omp-error {{access to an object with an atomic subobject}}
}
void atomic_type_trait_argument(void) {
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)__builtin_types_compatible_p(_Atomic(sig_F), _Atomic(sig_F)); }
}
void atomic_generic_controlling_type(void) {
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)_Generic(_Atomic(sig_F), default : 0); }
}
void atomic_load_bit_signature(void *raw) {
  // The cast to `_Atomic(sig_F) *` (and the bit-typed call result of the
  // atomic load) is construct input.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)__c11_atomic_load((_Atomic(sig_F) *)raw, 0)(); } // omp-error {{atomic operations}}
}
void atomic_inferred_block(void *raw) {
  // The atomic-load form driving the return of a deduced (block) closure.
  // omp-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel
  { (void)(^{ return __c11_atomic_load((_Atomic(sig_F) *)raw, 0)(); })(); } // omp-error {{atomic operations}}
}
void bitcast_atomic_positives(void *raw) {
  // Bit-free destination forms stay positive; WP4 A4 rejects the two atomic
  // rows (the _Atomic object declaration and the atomic load operation).
#pragma omp parallel
  {
    (void)__builtin_bit_cast(_Bool, (unsigned char)1);
    (void)__builtin_bit_cast(sig_I, raw)();
    (void)(^{ return __builtin_bit_cast(sig_I, raw)(); })();
    (void)sizeof(_Atomic(sig_I));
    _Atomic(sig_I) ai; // omp-error {{atomic types}}
    (void)ai; // omp-error {{access to an object with an atomic subobject}}
    (void)__builtin_types_compatible_p(_Atomic(sig_I), _Atomic(sig_I));
    (void)_Generic(_Atomic(sig_I), default : 0);
    (void)__c11_atomic_load((_Atomic(sig_I) *)raw, 0)(); // omp-error {{atomic operations}}
  }
}
// Outside constructs the F8/F9 TYPE-INPUT forms keep their current (accepted)
// behavior. WP4 A4/A5/A6 update: an _Atomic OBJECT DECLARATION and every
// atomic OPERATION are now rejected outright (no atomic model), so the two
// object/operation rows below carry their own expectations while the pure
// type queries (sizeof / __builtin_types_compatible_p / _Generic) and the
// cast spelling stay accepted.
void outside_bitcast_atomic_inputs(void *raw) {
  (void)__builtin_bit_cast(__bit, (unsigned char)1);
  (void)__builtin_bit_cast(sig_F, raw)();
  (void)(^{ return __builtin_bit_cast(sig_F, raw)(); })();
  (void)sizeof(_Atomic(sig_F));
  _Atomic(sig_F) af; // omp-error {{atomic types}}
  (void)af; // omp-error {{access to an object with an atomic subobject}}
  (void)__builtin_types_compatible_p(_Atomic(sig_F), _Atomic(sig_F));
  (void)_Generic(_Atomic(sig_F), default : 0);
  (void)__c11_atomic_load((_Atomic(sig_F) *)raw, 0)(); // omp-error {{atomic operations}}
}

// --- F10: newly written C record members bypass HandleDeclarator --------------

void field_raw(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { struct L { __bit (*f)(void); }; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void field_typedef(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { struct L { sig_F f; }; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void field_atomic(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { struct L { _Atomic(sig_F) f; }; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void field_block(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { struct L { __bit (^f)(void); }; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void field_parameter(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { struct L { void (*f)(__bit); }; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void field_array(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { union L { sig_F f[2]; }; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void field_anonymous(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { struct L { struct { sig_F f; }; }; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void field_anonymous_union(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { struct L { union { struct { sig_F f; }; int n; }; }; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void field_nested_record(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { struct Outer { struct Inner { __bit (*f)(void); } in; }; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void field_multiple_once(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  {
    struct Outer {
      sig_F first, second; // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
      struct Inner { _Atomic(sig_F) f; } in;
      union { sig_F f[2]; __bit (^block)(void); };
    };
  }
}
void field_nested_constructs(void) {
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  {
    struct Before { sig_F f; }; // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
#pragma omp task // omp-note {{entered OpenMP construct here}}
    { struct Inner { sig_F f, g; }; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
    struct After { sig_F f; }; // outer latch survives the nested construct
  }
}
// All seven forms remain legal outside constructs, even after an error.
void field_outside(void) {
  struct Raw { __bit (*f)(void); };
  struct Alias { sig_F f; };
  struct Atomic { _Atomic(sig_F) f; };
  struct Block { __bit (^f)(void); };
  struct Parameter { void (*f)(__bit); };
  union Array { sig_F f[2]; };
  struct Anonymous { struct { sig_F f; }; };
}
struct field_existing { sig_F f; int n; };
void field_record_boundary(struct field_existing r) {
#pragma omp parallel
  {
    struct Clean { int (*f)(void); };
    struct Holder { struct field_existing r; };
    struct field_existing copy = r; // no traversal of existing records
    (void)sizeof(struct field_existing);
    (void)__builtin_offsetof(struct field_existing, f);
    copy.n = 1;
  }
}

// --- laundering attempts and evaluation-independence ---------------------------

void launder_cast_to_int(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel num_threads((int)X + 1)
  {}
}
void launder_comma(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel num_threads((X, 2))
  {}
}
void unevaluated_operand(void) {
  // The operand of an unevaluated builtin still names the bit inside the
  // construct.
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { (void)__builtin_constant_p(G); } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}
void dead_branch(void) {
  // A dead branch is still inside the construct's range.
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
  { if (0) G = 1; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
}

// --- recovery: no context leakage ----------------------------------------------

void helper_writes_bit(void) {
  // Ordinary C bit semantics outside any construct.
  X = 1;
  G ^= 1;
}
void clean_construct_then_bit(void) {
  // A construct that never reported a violation must leave no restriction
  // active: bit use *after* it (outside any construct) stays ordinary.
#pragma omp parallel num_threads(2)
  {
    int t = 1;
    (void)t;
  }
  X = 1;
  G ^= 1;
}
void after_error_clean_function(void) {
  // The next function after rejected constructs is not falsely rejected:
  // bit use outside the construct, a clean construct, an ordinary call.
  X = 1;
  int sampled = X; // recommended sampling outside constructs
  (void)sampled;
#pragma omp parallel num_threads(2)
  {
    int t = 1;
    (void)t;
    _Bool c = 0;
    (void)c;
    helper_writes_bit(); // no cross-function taint: the helper's bit use is
                         // outside this construct
  }
}
void after_error_ordinary_clauses(void) {
  int n = 1;
  int m = 2;
  int k = 3;
#pragma omp parallel shared(n) private(m) firstprivate(k) num_threads(2)
  {
    n = n + 1;
    m = m + 1;
    k = k + 1;
  }
}

//--- acc.c
sbit AX = 0x24;
__bit AG;
typedef __bit ABT;
volatile ABT AV;

void acc_clause_sbit(void) {
  // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel if (AX)
  {}
}
void acc_clause_typedef_cv(void) {
  // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel if (AV)
  {}
}
void acc_clause_builtin(void) {
  // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel if (__builtin_mcs251_bit_lvalue(0x25))
  {}
}
void acc_body_bit_object(void) {
#pragma acc parallel // acc-note {{entered OpenACC construct here}}
  { AG = 1; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_body_local_bit_no_ref(void) {
#pragma acc parallel // acc-note {{entered OpenACC construct here}}
  { __bit b; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_body_bit_compound_literal(void) {
#pragma acc parallel // acc-note {{entered OpenACC construct here}}
  { (__bit){1}; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_body_bit_cast(void) {
#pragma acc parallel // acc-note {{entered OpenACC construct here}}
  { if ((__bit)1) ; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_capture_mapping(void) {
  // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel create(AG) present(AX)
  {}
}
void acc_standalone_update(void) {
  // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc update device(AG)
}
void acc_nested_inner_owns(void) {
#pragma acc parallel
  {
    // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc loop private(AG)
    for (int i = 0; i < 2; i++)
      {}
  }
}
void acc_dead_branch(void) {
#pragma acc parallel // acc-note {{entered OpenACC construct here}}
  { if (0) AG = 1; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}

// Declarative: an acc routine owns the associated definition region.
void acc_owned_decl(void);
#pragma acc routine(acc_owned_decl) seq // acc-note {{entered OpenACC construct here}}
void acc_owned_decl(void) {
  AX = 1; // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_owned_clean(void);
#pragma acc routine(acc_owned_clean) seq
void acc_owned_clean(void) {
  int t = 1;
  (void)t;
}
// The adjacent (unnamed) routine form covers the declaration and body in the
// directive parse itself.
#pragma acc routine seq // acc-note {{entered OpenACC construct here}}
void acc_adjacent(void) { AX = 1; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}

// --- final rework 2b mirrors: cast/literal type, depth, pure type input -----
typedef __bit (*asig_F)(void);
typedef void (*asig_A)(__bit);
void acc_cast_bit_signature(void *raw) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)((asig_F)raw)(); }
}
void acc_compound_literal_bit_signature(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)(asig_F){0}; }
}
void acc_type_trait_bit_argument(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)__builtin_types_compatible_p(__bit, int); }
}
void acc_generic_bit_association(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)_Generic(0, __bit : 1, default : 0); }
}
void acc_sizeof_bit_signature(void) {
  // acc-error@+3 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  {
    (void)sizeof(asig_F);
    (void)_Alignof(asig_F);
  }
}
void acc_pure_type_positives(void) {
#pragma acc parallel
  {
    (void)sizeof(int);
    (void)_Alignof(int);
    (void)_Generic(0, int : 1, default : 0);
    (void)__builtin_types_compatible_p(int, long);
  }
}

// --- final rework 3 mirrors: __builtin_bit_cast (F8), _Atomic (F9) ----------

typedef int (*asig_I)(void);

void acc_bitcast_bit_destination(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)__builtin_bit_cast(__bit, (unsigned char)1); }
}
void acc_bitcast_bit_signature_call(void *raw) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)__builtin_bit_cast(asig_F, raw)(); }
}
void acc_bitcast_inferred_block(void *raw) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)(^{ return __builtin_bit_cast(asig_F, raw)(); })(); }
}
void acc_atomic_sizeof_bit_signature(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)sizeof(_Atomic(asig_F)); }
}
void acc_atomic_local_declaration(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { _Atomic(asig_F) af; (void)af; } // acc-error {{atomic types}} // acc-error {{access to an object with an atomic subobject}}
}
void acc_atomic_type_trait_argument(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)__builtin_types_compatible_p(_Atomic(asig_F), _Atomic(asig_F)); }
}
void acc_atomic_generic_controlling_type(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)_Generic(_Atomic(asig_F), default : 0); }
}
void acc_atomic_load_bit_signature(void *raw) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)__c11_atomic_load((_Atomic(asig_F) *)raw, 0)(); } // acc-error {{atomic operations}}
}
void acc_atomic_inferred_block(void *raw) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)(^{ return __c11_atomic_load((_Atomic(asig_F) *)raw, 0)(); })(); } // acc-error {{atomic operations}}
}
void acc_bitcast_atomic_positives(void *raw) {
  // Bit-free destination and atomic-wrapped types inside constructs.
#pragma acc parallel
  {
    (void)__builtin_bit_cast(_Bool, (unsigned char)1);
    (void)__builtin_bit_cast(asig_I, raw)();
    (void)(^{ return __builtin_bit_cast(asig_I, raw)(); })();
    (void)sizeof(_Atomic(asig_I));
    _Atomic(asig_I) ai; // acc-error {{atomic types}}
    (void)ai; // acc-error {{access to an object with an atomic subobject}}
    (void)__builtin_types_compatible_p(_Atomic(asig_I), _Atomic(asig_I));
    (void)_Generic(_Atomic(asig_I), default : 0);
    (void)__c11_atomic_load((_Atomic(asig_I) *)raw, 0)(); // acc-error {{atomic operations}}
  }
}
// Outside constructs the F8/F9 forms keep their current (accepted) behavior.
void acc_outside_bitcast_atomic_inputs(void *raw) {
  (void)__builtin_bit_cast(__bit, (unsigned char)1);
  (void)__builtin_bit_cast(asig_F, raw)();
  (void)(^{ return __builtin_bit_cast(asig_F, raw)(); })();
  (void)sizeof(_Atomic(asig_F));
  _Atomic(asig_F) af; // acc-error {{atomic types}}
  (void)af; // acc-error {{access to an object with an atomic subobject}}
  (void)__builtin_types_compatible_p(_Atomic(asig_F), _Atomic(asig_F));
  (void)_Generic(_Atomic(asig_F), default : 0);
  (void)__c11_atomic_load((_Atomic(asig_F) *)raw, 0)(); // acc-error {{atomic operations}}
}

// --- final rework mirrors: signatures, blocks, va_arg (F1/F2/F3) -----------
typedef __bit (*abit_fn)(void);
extern abit_fn afp;
struct asig_member { __bit (*fp)(void); };
extern struct asig_member asm_;
void acc_sig_indirect_return(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)afp(); }
}
void acc_sig_member_field(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)asm_.fp(); }
}
void acc_block_bit_return(void) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)(^__bit(void){ return 1; })(); }
}
void acc_block_positive(void) {
#pragma acc parallel
  { (void)(^int(void){ return 1; })(); }
}
void acc_va_bit(__builtin_va_list ap) {
  // acc-error@+2 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel
  { (void)__builtin_va_arg(ap, __bit); }
}
void acc_va_int_positive(__builtin_va_list ap) {
#pragma acc parallel
  {
    int v = __builtin_va_arg(ap, int);
    (void)v;
  }
}
typedef int (*aint_fn)(void);
extern aint_fn aifp;
void acc_sig_positive(void) {
#pragma acc parallel
  { (void)aifp(); }
}

// --- F10 mirrors: new members, anonymous/nested records and latch ownership ---

void acc_field_raw(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  { struct L { __bit (*f)(void); }; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_field_typedef(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  { struct L { asig_F f; }; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_field_atomic(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  { struct L { _Atomic(asig_F) f; }; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_field_block(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  { struct L { __bit (^f)(void); }; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_field_parameter(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  { struct L { void (*f)(__bit); }; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_field_array(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  { union L { asig_F f[2]; }; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_field_anonymous(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  { struct L { struct { asig_F f; }; }; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_field_anonymous_union(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  { struct L { union { struct { asig_F f; }; int n; }; }; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_field_nested_record(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  { struct Outer { struct Inner { __bit (*f)(void); } in; }; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
}
void acc_field_multiple_once(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  {
    struct Outer {
      asig_F first, second; // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
      struct Inner { _Atomic(asig_F) f; } in;
      union { asig_F f[2]; __bit (^block)(void); };
    };
  }
}
void acc_field_nested_constructs(void) {
#pragma acc serial // acc-note {{entered OpenACC construct here}}
  {
    struct Before { asig_F f; }; // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
#pragma acc loop // acc-note {{entered OpenACC construct here}}
    for (int i = 0; i < 2; ++i) {
      struct Inner { asig_F f, g; }; // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
    }
    struct After { asig_F f; }; // outer latch survives the nested construct
  }
}
void acc_field_outside(void) {
  struct Raw { __bit (*f)(void); };
  struct Alias { asig_F f; };
  struct Atomic { _Atomic(asig_F) f; };
  struct Block { __bit (^f)(void); };
  struct Parameter { void (*f)(__bit); };
  union Array { asig_F f[2]; };
  struct Anonymous { struct { asig_F f; }; };
}
struct acc_field_existing { asig_F f; int n; };
void acc_field_record_boundary(struct acc_field_existing r) {
#pragma acc serial
  {
    struct Clean { int (*f)(void); };
    struct Holder { struct acc_field_existing r; };
    struct acc_field_existing copy = r;
    (void)sizeof(struct acc_field_existing);
    (void)__builtin_offsetof(struct acc_field_existing, f);
    copy.n = 1;
  }
}

// Recovery and positives.
void acc_clean_construct_then_bit(void) {
  // Same no-leak control for OpenACC: a clean construct, then ordinary bit use.
#pragma acc parallel num_gangs(1)
  {
    int t = 1;
    (void)t;
  }
  AX = 1;
  AG ^= 1;
}
void acc_after_error_ok(void) {
  AX = 1;
  int sampled = AX;
  (void)sampled;
#pragma acc parallel num_gangs(1)
  {
    int t = 1;
    (void)t;
  }
}
void acc_ordinary_objects(void) {
  int n = 1;
  int m = 2;
#pragma acc parallel copy(n) present(m) private(m)
  {
    n = n + 1;
    m = m + 1;
  }
}