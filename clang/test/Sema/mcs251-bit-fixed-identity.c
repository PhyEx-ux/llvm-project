// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %t/redecl.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o /dev/null -verify=codegen %t/codegen-sbit.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=codegen %t/codegen-global.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=cl %t/codegen-compound.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=clw %t/codegen-compound-write.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=ci %t/codegen-constinit.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=cis %t/codegen-constinit-static.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=cia %t/codegen-constinit-array.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=alias %t/codegen-alias.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=cistmt %t/codegen-constinit-stmt.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=cok %t/codegen-const-ok.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fblocks -emit-llvm -o /dev/null -verify=blk %t/codegen-block.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -Wno-unused-value -emit-llvm -o /dev/null -verify=cs2 %t/codegen-constinit-stmt2.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -Wno-unused-value -emit-llvm -o /dev/null -verify=cs3 %t/codegen-constinit-stmt3.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -Wno-unused-value -emit-llvm -o /dev/null -verify=cs4 %t/codegen-constinit-stmt4.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=cwok %t/codegen-const-wrapped-ok.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -Wno-unused-value -emit-llvm -o /dev/null -verify=cf %t/codegen-const-fold.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -Wno-unused-value -emit-llvm -o /dev/null -verify=cfl1 %t/codegen-const-fold-live-gnu.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -Wno-unused-value -emit-llvm -o /dev/null -verify=cfl2 %t/codegen-const-fold-live-ifelse.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -Wno-unused-value -emit-llvm -o /dev/null -verify=cfl3 %t/codegen-const-fold-live-case.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -emit-llvm -o /dev/null -verify=os %t/codegen-objectsize-ok.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fopenmp -Wno-unused-value -Wno-empty-body -fsyntax-only -verify=omp %t/sema-omp.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fopenacc -Wno-unused-value -Wno-empty-body -fsyntax-only -verify=acc %t/sema-acc.c

// Old-style `sbit` establishes a controlled fixed bit reference identity.
// Redeclaring the name as an ordinary `__bit` object (in either direction) must
// be rejected, and any bit object or fixed reference in CodeGen before the
// lowering capability exists (M2) must fail closed rather than degrade to an
// ordinary byte global/store/alloca.

//--- redecl.c
sbit X = 0x24; // expected-note {{previous definition is here}}
// An ordinary declaration cannot preserve the fixed-reference identity.
volatile __bit X = 1; // expected-error {{'X' was declared as a controlled MCS251 fixed bit reference; it cannot be redeclared as an ordinary object}}

// The reverse direction is rejected by the sbit path.
volatile __bit Y = 1;
sbit Y = 0x24; // expected-error {{conflicting redeclaration of 'sbit' 'Y' at a different bit address}}

//--- codegen-sbit.c
// codegen-no-diagnostics
// A controlled fixed bit reference now lowers to the target bit intrinsics
// (BIT M2): uses no longer fail closed and no ordinary byte global/store is
// emitted. The instruction-level assertions live in
// clang/test/CodeGen/mcs251-bit-fixed-ref.c.
sbit Z = 0x88 ^ 3;
void use_z(void) { Z = 1; }

//--- codegen-global.c
// An ordinary bit global is a persistent P-1b bit object: it compiles to its
// i8 bit-object handle global (the structural IR assertions live in
// CodeGen/mcs251-bit-objects.c); no byte global and no fail-closed error.
// codegen-no-diagnostics
__bit G;

// (codegen-local removed: auto bit locals are SUPPORTED since P-2; positive
// coverage in CodeGen/mcs251-bit-local.c.)

//--- codegen-compound.c
// A bit compound literal is a real bit object and must fail closed.
int compound_literal(void) {
  // cl-error@+1 {{cannot compile this MCS251 bit compound literal yet}}
  return (__bit){1};
}

//--- codegen-compound-write.c
void compound_write(void) {
  // clw-error@+1 {{cannot compile this MCS251 bit compound literal yet}}
  (__bit){0} = 1;
}

// (codegen-param removed: bit formal parameters are SUPPORTED since P-3.)

//--- codegen-constinit.c
// A bit compound literal inside a constant initializer must not be folded away
// into an ordinary integer constant. This path bypasses
// EmitCompoundLiteralLValue, so the constant emitter itself must fail closed.
// ci-error@+1 {{cannot compile this static initializer yet}} ci-error@+1 {{cannot compile this MCS251 bit compound literal yet}}
int glob = (__bit){1};

//--- codegen-constinit-static.c
int f(void) {
  // cis-error@+1 {{cannot compile this constant l-value expression yet}} cis-error@+1 {{cannot compile this MCS251 bit compound literal yet}}
  static int x = (__bit){1};
  return x;
}

//--- codegen-constinit-array.c
// cia-error@+1 {{cannot compile this static initializer yet}} cia-error@+1 {{cannot compile this MCS251 bit compound literal yet}}
int arr[1] = {(__bit){1}};

//--- codegen-alias.c
// A bit alias must fail closed rather than degrade to an ordinary byte alias.
unsigned char backing;
// alias-error@+1 {{cannot compile this MCS251 bit alias yet}}
extern __bit y __attribute__((alias("backing")));

//--- codegen-constinit-stmt.c
// A bit compound literal reached through a statement expression in a constant
// initializer must still fail closed: the guard has to cross the StmtExpr's
// CompoundStmt.
int f(void) {
  // cistmt-error@+1 {{cannot compile this constant l-value expression yet}} cistmt-error@+1 {{cannot compile this MCS251 bit compound literal yet}}
  static int x = ({ (__bit){1}; });
  return x;
}

//--- codegen-const-ok.c
// cok-no-diagnostics
// A bit compound literal in an *unevaluated* subexpression creates no bit
// storage and must not be rejected.
int ok_sizeof = sizeof(+(__bit){1});
int ok_generic_unsel = _Generic(0, int : 1, default : (__bit){1});
int ok_generic_control = _Generic(+(__bit){1}, int : 1);
int ok_choose_unsel = __builtin_choose_expr(1, 1, (__bit){1});
// Boundary (documented, accepted): a bit compound literal folded to a plain
// integer value in an enum initializer, a case label, a static assertion, or a
// bit-field width creates no bit storage.
enum { ok_enum = (__bit){1} };
int ok_case(int a) { switch (a) { case (__bit){1}: return 1; } return 0; }
_Static_assert((__bit){1}, "ok");
struct ok_bf { unsigned a : (__bit){1}; };

//--- codegen-block.c
// A block capturing a bit object must fail closed rather than crash on the
// missing byref/LocalDeclMap entry.
int use_block(void) {
  // blk-error@+1 {{cannot compile this MCS251 bit object yet}}
  __block __bit x = 1;
  // blk-error@+1 {{cannot compile this MCS251 bit block capture yet}}
  int (^b)(void) = ^{ return +x; };
  return b();
}

//--- codegen-constinit-stmt2.c
// A bit object folded away inside a statement expression in a constant
// initializer must still fail closed: the scan must recurse through nested
// blocks.
int f_nested(void) {
  // cs2-error@+1 {{cannot compile this MCS251 bit compound literal yet}} cs2-error@+1 {{cannot compile this constant l-value expression yet}}
  static int x = ({{{(__bit){1};}} 1;});
  return x;
}

//--- codegen-constinit-stmt3.c
// ... through control flow ...
int f_if(void) {
  // cs3-error@+1 {{cannot compile this MCS251 bit compound literal yet}} cs3-error@+1 {{cannot compile this constant l-value expression yet}}
  static int y = ({if (1) { (__bit){1}; } 1;});
  return y;
}

//--- codegen-constinit-stmt4.c
// ... and through a folded bit declaration (which bypasses EmitVarDecl).
int f_decl(void) {
  // cs4-error@+1 {{cannot compile this MCS251 bit compound literal yet}} cs4-error@+1 {{cannot compile this constant l-value expression yet}}
  static int z = ({const __bit a = 1; 1;});
  return z;
}

//--- codegen-const-wrapped-ok.c
// cwok-no-diagnostics
// A bit compound literal wrapped in a statement expression but in an
// unevaluated operand (or a short-circuited / unselected branch) creates no bit
// storage and must not be rejected.
int f_sizeof(void) {
  static int x = ({ sizeof(+(__bit){1}); });
  return x;
}
int f_generic(void) {
  static int x = ({ _Generic(0, int : 1, default : (__bit){1}); });
  return x;
}
int f_control(void) {
  static int x = ({ _Generic(+(__bit){1}, int : 1); });
  return x;
}
int f_choose(void) {
  static int x = ({ __builtin_choose_expr(1, 1, (__bit){1}); });
  return x;
}
// A builtin with UnevaluatedArguments does not evaluate its operand.
int f_constant_p = __builtin_constant_p((__bit){1});
int f_constant_p_stmt(void) {
  static int x = ({ __builtin_constant_p((__bit){1}); });
  return x;
}
// A short-circuited / unselected conditional branch is dead.
int f_cond_false(void) {
  static int x = 0 ? (__bit){1} : 1;
  return x;
}
int f_short_false(void) {
  static int x = 0 && (__bit){1};
  return x;
}

//--- codegen-const-fold.c
// cf-no-diagnostics
// Dead branches of constant conditions (GNU `c ?: x`, `if (0)`, `while (0)`,
// `for (; 0;)`) are folded away before the constant-initializer guard runs: a
// bit object there creates no storage and must not be rejected. (The live
// negatives are in their own sections: only the first offending static
// initializer in a translation unit reaches the guard's diagnostic.)
int fold_gnu_dead(void) {
  static unsigned long x = 1 ?: (__bit){1};
  return x;
}
int fold_if_dead(void) {
  static unsigned long x = ({ if (0) { (__bit){1}; } 1; });
  return x;
}
int fold_while_dead(void) {
  static unsigned long x = ({ while (0) { (__bit){1}; } 1; });
  return x;
}
int fold_for_dead(void) {
  static unsigned long x = ({ for (; 0;) { (__bit){1}; } 1; });
  return x;
}
int fold_if_decl_dead(void) {
  static unsigned long x = ({ if (0) { const __bit b = 1; } 1; });
  return x;
}
// case/default label expressions are integer constant expressions folded to
// plain values: they create no bit storage (registered boundary) whether or
// not the case is selected.
int fold_case_boundary(void) {
  static unsigned long x = ({ switch (1) { case (__bit){1}: ; } 1; });
  return x;
}
int fold_case_range_boundary(void) {
  static unsigned long x = ({ switch (2) { case (__bit){1} ... 2: ; } 1; });
  return x;
}
int fold_default_boundary(void) {
  static unsigned long x = ({ switch (1) { default: ; } 1; });
  return x;
}

//--- codegen-const-fold-live-gnu.c
int fold_gnu_live(void) {
  // cfl1-error@+1 {{cannot compile this constant l-value expression yet}} cfl1-error@+1 {{cannot compile this MCS251 bit compound literal yet}}
  static unsigned long x = 0 ?: (__bit){1};
  return x;
}

//--- codegen-const-fold-live-ifelse.c
int fold_if_else_live(void) {
  // cfl2-error@+1 {{cannot compile this constant l-value expression yet}} cfl2-error@+1 {{cannot compile this MCS251 bit compound literal yet}}
  static unsigned long x = ({ if (0) { (__bit){1}; } else { (__bit){2}; } 1; });
  return x;
}

//--- codegen-const-fold-live-case.c
int fold_case_body_live(void) {
  // cfl3-error@+1 {{cannot compile this constant l-value expression yet}} cfl3-error@+1 {{cannot compile this MCS251 bit compound literal yet}}
  static unsigned long x = ({ switch (1) { case 1: (__bit){5}; } 1; });
  return x;
}

//--- codegen-objectsize-ok.c
// os-no-diagnostics
// The object size builtins are declared with unevaluated arguments: their
// pointer argument is not evaluated whenever it cannot be emitted, so a bit
// compound literal inside it creates no bit storage in a runtime context
// either -- the builtin returns the default result instead of materializing
// the unsupported object.
unsigned long objectsize_bit_literal(void) {
  return __builtin_object_size((void *)(unsigned long)(__bit){1}, 0);
}
unsigned long dynamic_objectsize_bit_literal(void) {
  return __builtin_dynamic_object_size((void *)(unsigned long)(__bit){1}, 0);
}

//--- sema-omp.c
// P08 revision (Alice ruling, plan B): every MCS251 bit capability is
// rejected wholesale inside an enabled OpenMP construct -- pragma expression
// arguments, clause input, the associated statement, captures and generated
// private copies; read, write, storage binding, and actual evaluation are
// not distinguished (a construct-boundary restriction, not an access
// analysis). Exactly one error (located at the first violation) plus one
// note (at the pragma) is emitted per construct, and the old controlled-bit
// clause evaluation classification (R6-R9) no longer runs on directive
// input, so the migrated cases assert the new diagnostic without the old
// "this field constitutes a read" reasoning. Cases that used to be positive
// because the bit was only written/bound/discarded are now negative.
sbit X = 0x24;

// Body forms (R6 migrated): plain read, write-only, discarded toggle, and
// used toggle are all construct input and all rejected. The note points at
// the pragma; the error at the first violation.
void omp_body_self_read(void) {
  X = ({
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
        { int y = X; (void)y; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
        1;
  });
}
void omp_body_write_only(void) {
  X = ({
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
        { X = 1; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
        1;
  });
}
void omp_body_toggle_discarded(void) {
  ({
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
        { X ^= 1; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
        0;
  });
}
void omp_body_toggle_used(void) {
  ({
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
        { if (X ^= 1) ; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
        0;
  });
}
// A local bit object declared in the region is equally construct input
// (declared-into-the-construct entry), even without any reference to it.
void omp_body_local_bit_decl(void) {
  ({
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
        { __bit b; (void)b; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
        0;
  });
}
// Unevaluated operands and dead branches are still rejected: the boundary is
// a range restriction, not an evaluation analysis.
void omp_body_unevaluated(void) {
  ({
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
        { (void)sizeof(X); } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-error {{sizeof of MCS251 'bit' type is not allowed}}
        0;
  });
}
void omp_body_dead_branch(void) {
  ({
#pragma omp parallel // omp-note {{entered OpenMP construct here}}
        { if (0) X = 1; } // omp-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}}
        0;
  });
}

// Clause forms (R7 migrated): a plain read, a complex RMW, and a used toggle
// in an evaluated clause operand were already negative; they now take the
// construct diagnostic.
void omp_clause_if_self_read(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel if (X)
        {}
        1;
  });
}
void omp_clause_if_complex_rmw(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel if ((X |= 1, 1))
        {}
        1;
  });
}
void omp_clause_if_used_toggle(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel if ((X ^= 1))
        {}
        1;
  });
}
void omp_clause_num_threads_used(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel num_threads((X ^= 1) + 1)
        {}
        1;
  });
}
void omp_clause_num_threads_complex_rmw(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel num_threads((X |= 1, 1))
        {}
        1;
  });
}
void omp_clause_task_if_self_read(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp task if (X)
        {}
        1;
  });
}
void omp_clause_task_final_self_read(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp task final (X)
        {}
        1;
  });
}
void omp_clause_schedule_chunk_self_read(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel for schedule(static, X)
        for (int i = 0; i < 2; i++) {
        }
        1;
  });
}
void omp_clause_firstprivate_self_read(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel firstprivate (X)
        {}
        1;
  });
}
// A discarded toggle inside a clause operand used to be the allowed CPL form;
// inside a construct it is plain construct input and rejected.
void omp_clause_if_discarded_toggle(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel if ((X ^= 1, 1))
        {}
        1;
  });
}
void omp_clause_num_threads_discarded_toggle(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel num_threads((X ^= 1, 1))
        {}
        1;
  });
}

// Data-sharing and locator clauses (R8 migrated to negative): private, shared,
// and lastprivate only *name* storage, and the mapping clauses that never
// read the host value -- but naming the bit object puts it into the construct
// (capture/private-copy identity association), so all of these are rejected
// now, with or without a written private copy.
void omp_clause_shared_binds(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel shared(X)
        {}
        1;
  });
}
void omp_clause_private_binds(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel private(X)
        {}
        1;
  });
}
void omp_clause_shared_write_only(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel shared(X)
        { X = 0; }
        1;
  });
}
void omp_clause_lastprivate_binds(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel for lastprivate(X)
        for (int i = 0; i < 2; i++) {
        }
        1;
  });
}
void omp_clause_lastprivate_written(void) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel for lastprivate(X)
        for (int i = 0; i < 2; i++) {
          X = 0;
        }
        1;
  });
}
// A nested task shared clause: the inner construct owns the violation and the
// note points at the inner pragma.
void omp_clause_nested_task_shared(void) {
  X = ({
#pragma omp parallel
        {
          // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp task shared(X)
          {}
        }
        1;
  });
}
// A depend locator's subscript index names the bit in construct input.
void omp_clause_depend_index_self_read(int *p) {
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp task depend(in : p[X])
        {}
        1;
  });
}

// The allocate clause (R8-B migrated): the allocator expression is construct
// input; a read, a complex RMW, a consumed or a discarded toggle there are
// all rejected identically.
typedef enum omp_allocator_handle_t {
  omp_null_allocator = 0,
  omp_default_mem_alloc = 1,
  omp_large_cap_mem_alloc = 2,
  omp_const_mem_alloc = 3,
  omp_high_bw_mem_alloc = 4,
  omp_low_lat_mem_alloc = 5,
  omp_cgroup_mem_alloc = 6,
  omp_pteam_mem_alloc = 7,
  omp_thread_mem_alloc = 8
} omp_allocator_handle_t;
void omp_clause_allocate_self_read(void) {
  int a;
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel private(a) allocate((omp_allocator_handle_t)X : a)
        {}
        1;
  });
}
void omp_clause_allocate_complex_rmw(void) {
  int a;
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel private(a) allocate((X |= 1, omp_default_mem_alloc) : a)
        {}
        1;
  });
}
void omp_clause_allocate_used_toggle(void) {
  int a;
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel private(a) allocate((omp_allocator_handle_t)(X ^= 1) : a)
        {}
        1;
  });
}
void omp_clause_allocate_discarded_toggle(void) {
  int a;
  X = ({
      // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel private(a) allocate((X ^= 1, omp_default_mem_alloc) : a)
        {}
        1;
  });
}

// A directive outside any statement expression is equally construct input.
void omp_direct_num_threads_complex_rmw(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel num_threads((X |= 1, 1))
  {}
}
void omp_direct_num_threads_used_toggle(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel num_threads((X ^= 1) + 1)
  {}
}
void omp_direct_num_threads_discarded_toggle(void) {
  // omp-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} omp-note@+1 {{entered OpenMP construct here}}
#pragma omp parallel num_threads((X ^= 1, 1))
  {}
}
// A bit-free directive after a rejected one is unaffected (no context leak),
// and a constant operand stays accepted.
void omp_direct_after_error_ok(void) {
#pragma omp parallel num_threads(2)
  {}
}
// Positive controls: the bit stays outside the construct and the construct
// itself is free of bit capability. The enclosing outer assignment to X is
// ordinary C bit semantics and must not reject the whole statement
// expression.
void omp_positive_bit_outside_only(void) {
  X = ({
#pragma omp parallel
        { int t = 1; (void)t; }
        1;
  });
}
void omp_positive_bit_sampled_before(void) {
  int v = X;
#pragma omp parallel num_threads(v + 1)
  {}
}
void omp_positive_ordinary_objects(void) {
  int n = 1;
  _Bool c = 0;
#pragma omp parallel shared(n) private(c) num_threads(2)
  {
    n = n + c;
    int local = 3;
    (void)local;
  }
}

//--- sema-acc.c
// The same construct boundary for OpenACC: clause input, the associated
// statement, captures -- rejected wholesale, one error + one note per
// construct.
sbit AX = 0x24;
void acc_if_self_read(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel if (AX)
        {}
        1;
  });
}
void acc_if_complex_rmw(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel if ((AX |= 1, 1))
        {}
        1;
  });
}
void acc_if_used_toggle(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel if ((AX ^= 1))
        {}
        1;
  });
}
void acc_if_discarded_toggle(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel if ((AX ^= 1, 1))
        {}
        1;
  });
}
// Body-only forms.
void acc_body_write_only(void) {
  AX = ({
#pragma acc parallel // acc-note {{entered OpenACC construct here}}
        { AX = 0; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
        1;
  });
}
void acc_body_dead_branch(void) {
  AX = ({
#pragma acc parallel // acc-note {{entered OpenACC construct here}}
        { if (0) AX = 1; } // acc-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}}
        1;
  });
}

// The mapping clauses that bind storage without reading it (create, present,
// private, copyout) used to be positive; naming the bit in them is now
// rejected, with or without a written region body. Copy/copyin were already
// negative and migrate to the construct diagnostic.
void acc_create_binds(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel create(AX)
        {}
        1;
  });
}
void acc_present_binds(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel present(AX)
        {}
        1;
  });
}
void acc_private_binds(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel private(AX)
        {}
        1;
  });
}
void acc_copyout_binds(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel copyout(AX)
        {}
        1;
  });
}
void acc_copyout_written(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel copyout(AX)
        { AX = 0; }
        1;
  });
}
void acc_copy_reads(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel copy(AX)
        {}
        1;
  });
}
void acc_copyin_reads(void) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel copyin(AX)
        {}
        1;
  });
}
// A mapping locator's subscript index names the bit in construct input.
void acc_create_index_self_read(int *p) {
  AX = ({
      // acc-error@+1 {{MCS251 bit types, objects and fixed-bit references are not supported in OpenACC constructs}} acc-note@+1 {{entered OpenACC construct here}}
#pragma acc parallel create(p[AX:1])
        {}
        1;
  });
}
// Positive controls: bit outside the construct; ordinary objects and constant
// clause operands accepted; a bit-free directive after rejected ones is
// unaffected.
void acc_positive_bit_outside_only(void) {
  AX = ({
#pragma acc parallel
        { int t = 1; (void)t; }
        1;
  });
}
void acc_positive_ordinary_objects(void) {
  int n = 1;
#pragma acc parallel copy(n) num_gangs(2)
  {
    n = n + 1;
  }
}
void acc_positive_after_error_ok(void) {
#pragma acc parallel num_gangs(1)
  {}
}
