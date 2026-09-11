// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fopenmp -fopenmp-version=52 -Wno-unused-value -Wno-empty-body -fsyntax-only -verify %s

// R9 (migrated, P08 revision / Alice ruling plan B): independently stored
// clause fields as a construct-boundary regression asset.
//
// Originally this file verified that the controlled-bit scanners reached
// clause operand fields stored outside children() (depend/map/to/from iterator
// modifiers, linear step/calc-step, affinity modifier, array-shaping
// dimensions), with read/RMW/toggle classification. The construct gate
// replaces that classification: every bit capability inside a construct is
// rejected wholesale (err_mcs251_bit_omp_construct), one error + one note per
// construct. The same inputs are kept as boundary negatives so the gate still
// has to see through every independently stored clause field; the error facts
// are preserved while the "this field constitutes a read" reasoning is not.
// The complete clause/field enumeration table below is retained verbatim as
// the R9 review asset.
//
// Negative matrix per target field: outer stmt-expr X=({ ... }) with a plain
// bit reference (X), a complex RMW (X|=1), and a used toggle ((X^=1)+1); the
// direct-directive forms of the same; and the formerly-positive plain reads /
// discarded toggles (construct input regardless of use). Positive controls:
// constant iterator/step/dimension forms, whose clauses carry no bit
// capability.
//
//===----------------------------------------------------------------------===//
// Complete enumeration: OpenMPClause.h clauses -> independently stored
// operand fields -> is the field in children()? -> how this round handles it.
// (Accessors cited, not line numbers; OpenMPClause.h @ r9.)
//
//  clause            field(s)                        children()?  handling
//  ----------------  ------------------------------  ----------  --------
//  allocate          Allocator, Alignment            no          explicitly visited as evaluations in BOTH clause scanners (since R8)
//  aligned           alignment (*varlist_end())     no          NOT visited: Sema forces a positive integer constant (VerifyPositiveIntegerConstantInClause), so no runtime bit operation can hide there
//  linear            Privates/Inits/Updates/        no          NOT separately visited: Sema-built references to the generated private copies; the original-item references are the varlist (in children(), scanned as reads per the "linear list = read" ruling)
//                    Finals/UsedExprs
//  linear            Step, CalcStep                  no          explicitly visited as evaluations in BOTH clause scanners (R9-A)
//  depend            Modifier (iterator)             no          explicitly visited as evaluation in BOTH clause scanners (R9-A)
//  depend            LoopData (sink/source)          no          NOT separately visited: Sema-built from the associated loops' induction variables (buildOrderedLoopData); the user-written sink locators are the varlist (in children())
//  map               IteratorModifier                no          explicitly visited as evaluation in BOTH clause scanners (R9-A)
//  map/to/from       UDMapperRefs                    no          NOT visited: references to declare-mapper declarations, no bit content
//  to                IteratorModifier                no          explicitly visited as evaluation in BOTH clause scanners (R9-A)
//  from              IteratorModifier                no          explicitly visited as evaluation in BOTH clause scanners (R9-A)
//  affinity          Modifier (iterator)             YES (the    re-routed in R9-B: the read scanner scans it as a real read and skips it in the address-only children() loop (the usage scanner already reached it through children())
//                                                    only clause
//                                                    whose modi-
//                                                    fier is in
//                                                    children())
//  private           PrivateCopies                   no          NOT separately visited: refs to generated private copies; originals are the varlist (in children(), binding)
//  firstprivate      Inits, PrivateCopies            no          same: Inits duplicate the varlist refs; the initialization read is covered by scanning the varlist (evaluated, per ruling)
//  lastprivate       PrivateCopies/SourceExprs/      no          NOT separately visited: copy-back is a write of the original (binding ruling); the PostUpdate hook covers the post-region expression
//                    DestinationExprs/AssignmentOps
//  copyin            SourceExprs/DestinationExprs/   no          same pattern; the value read is the varlist (evaluated, per ruling)
//                    AssignmentOps
//  copyprivate       SourceExprs/DestinationExprs/   no          same pattern (broadcast read = varlist, evaluated, per ruling)
//                    AssignmentOps
//  reduction/        LHSExprs/RHSExprs/Privates/     no          NOT separately visited: originals are the varlist (evaluated read, per ruling); ops reference generated privates; PostUpdate hook covers the combination
//  task_reduction/   ReductionOps/InscanCopy*/
//  in_reduction      (+TaskgroupDescriptors)
//  nontemporal       PrivateRefs                     no          NOT visited: refs to generated private copies
//  ordered           LoopNumIterations               no          NOT visited: Sema/AST-reader-derived loop-bound references (NumForLoops itself IS in children())
//  doacross          LoopData                        no          same as depend LoopData (Sema-built from loop induction variables)
//  num_teams/        ModifierExpr (*varlist_end())   YES         reached through children() (children = varlist + one slot)
//  thread_limit
//  schedule          ChunkSize                       YES         reached through children(); runtime chunks additionally captured by the PreInit hook
//  dist_schedule     ChunkSize                       YES         reached through children(); PreInit hook as above
//  if/final/device/  the single operand Stmt         YES         reached through children() (OneStmtClause / dedicated field)
//  priority/
//  grainsize/
//  num_tasks/hint/
//  filter/novariants/
//  nocontext/detach/
//  message/safelen/
//  simdlen/collapse/
//  holds/allocator/
//  align/transparent/
//  nowait/use/depobj/
//  destroy/ompx_...
//  sizes/counts/     all operand refs                YES         reached through children()
//  permutation
//  init              varlist + attr exprs            YES         children() spans the interop var, fr exprs, and attr() expressions
//  uses_allocators   allocator + traits exprs        YES         children() spans NumOfAllocators * ExprOffsets::Total
//  absent/contains   directive lists                 n/a         no expressions
//  default/defaultmap/proc_bind/order/atomic flags/
//  untied/mergeable/nogroup/simd/bind/at/severity/
//  self_maps/unified_*/reverse_offload/dynamic_
//  allocators/atomic_default_mem_order/...          n/a         no operand expressions
//
//  Related OpenMP *expressions* (ExprOpenMP.h), not clauses:
//  OMPIteratorExpr   range begin/end/step            YES         the user-written iterator ranges are in children(); the helper data (Upper/Update/CounterUpdate) is outside but Sema-built arithmetic over the same range subexpressions, so no user content is lost
//  OMPArrayShaping   Dimensions + Base               YES         both are in children(); the R9-C bug was the dedicated findReadAddressComputation branch that visited only getBase(); it now scans getDimensions() as well
//===----------------------------------------------------------------------===//

sbit X = 0x24;

//===----------------------------------------------------------------------===//
// depend: iterator modifier (OMPDependClause::getModifier, outside children()).
//===----------------------------------------------------------------------===//

void dep_read_outer(int *p) {
  X = ({
#pragma omp task depend(iterator(int it = 0:X), in:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void dep_bad_outer(int *p) {
  X = ({
#pragma omp task depend(iterator(int it = 0:(X |= 1, 2)), in:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void dep_used_outer(int *p) {
  X = ({
#pragma omp task depend(iterator(int it = 0:((X ^= 1) + 1)), in:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void dep_bad_direct(int *p) {
#pragma omp task depend(iterator(int it = 0:(X |= 1, 2)), in:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  {}
}

void dep_used_direct(int *p) {
#pragma omp task depend(iterator(int it = 0:((X ^= 1) + 1)), in:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  {}
}

// Positive controls: a direct plain read, a discarded toggle, a constant range.
void dep_ok_read_direct(int *p) {
#pragma omp task depend(iterator(int it = 0:X), in:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  {}
}

void dep_ok_discard_outer(int *p) {
  X = ({
#pragma omp task depend(iterator(int it = 0:(X ^= 1, 2)), in:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void dep_ok_const_outer(int *p) {
  X = ({
#pragma omp task depend(iterator(int it = 0:2), in:p[it])
    {}
    1;
  });
}

//===----------------------------------------------------------------------===//
// map: iterator modifier (OMPMapClause::getIteratorModifier, outside
// children(), stored at trailing slot 2*varlist_size()).
//===----------------------------------------------------------------------===//

void map_read_outer(int *p) {
  X = ({
#pragma omp target map(iterator(int it = 0:X), to:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void map_bad_outer(int *p) {
  X = ({
#pragma omp target map(iterator(int it = 0:(X |= 1, 2)), to:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void map_used_outer(int *p) {
  X = ({
#pragma omp target map(iterator(int it = 0:((X ^= 1) + 1)), to:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void map_bad_direct(int *p) {
#pragma omp target map(iterator(int it = 0:(X |= 1, 2)), to:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  {}
}

void map_used_direct(int *p) {
#pragma omp target map(iterator(int it = 0:((X ^= 1) + 1)), to:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  {}
}

void map_ok_read_direct(int *p) {
#pragma omp target map(iterator(int it = 0:X), to:p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  {}
}

void map_ok_const_outer(int *p) {
  X = ({
#pragma omp target map(iterator(int it = 0:2), to:p[it])
    {}
    1;
  });
}

//===----------------------------------------------------------------------===//
// to: iterator modifier (OMPToClause::getIteratorModifier, outside children()).
//===----------------------------------------------------------------------===//

void to_read_outer(int *p) {
  X = ({
#pragma omp target update to(iterator(int it = 0:X):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    1;
  });
}

void to_bad_outer(int *p) {
  X = ({
#pragma omp target update to(iterator(int it = 0:(X |= 1, 2)):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    1;
  });
}

void to_used_outer(int *p) {
  X = ({
#pragma omp target update to(iterator(int it = 0:((X ^= 1) + 1)):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    1;
  });
}

void to_bad_direct(int *p) {
#pragma omp target update to(iterator(int it = 0:(X |= 1, 2)):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
}

void to_used_direct(int *p) {
#pragma omp target update to(iterator(int it = 0:((X ^= 1) + 1)):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
}

void to_ok_read_direct(int *p) {
#pragma omp target update to(iterator(int it = 0:X):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
}

void to_ok_const_outer(int *p) {
  X = ({
#pragma omp target update to(iterator(int it = 0:2):p[it])
    1;
  });
}

//===----------------------------------------------------------------------===//
// from: iterator modifier (OMPFromClause::getIteratorModifier, outside
// children(); from is address-only for its locator list, but the iterator
// ranges are still evaluations).
//===----------------------------------------------------------------------===//

void from_read_outer(int *p) {
  X = ({
#pragma omp target update from(iterator(int it = 0:X):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    1;
  });
}

void from_bad_outer(int *p) {
  X = ({
#pragma omp target update from(iterator(int it = 0:(X |= 1, 2)):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    1;
  });
}

void from_used_outer(int *p) {
  X = ({
#pragma omp target update from(iterator(int it = 0:((X ^= 1) + 1)):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    1;
  });
}

void from_bad_direct(int *p) {
#pragma omp target update from(iterator(int it = 0:(X |= 1, 2)):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
}

void from_used_direct(int *p) {
#pragma omp target update from(iterator(int it = 0:((X ^= 1) + 1)):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
}

void from_ok_const_outer(int *p) {
  X = ({
#pragma omp target update from(iterator(int it = 0:2):p[it])
    1;
  });
}

//===----------------------------------------------------------------------===//
// linear: step and calc-step (OMPLinearClause::getStep()/getCalcStep(),
// trailing slots after Finals, outside children()).
//===----------------------------------------------------------------------===//

void lin_read_outer(void) {
  int a = 0;
  X = ({
#pragma omp simd linear(a:X) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    for (int i = 0; i < 2; i++)
      {}
    1;
  });
  (void)a;
}

void lin_bad_outer(void) {
  int a = 0;
  X = ({
#pragma omp simd linear(a:(X |= 1, 2)) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    for (int i = 0; i < 2; i++)
      {}
    1;
  });
  (void)a;
}

void lin_used_outer(void) {
  int a = 0;
  X = ({
#pragma omp simd linear(a:((X ^= 1) + 1)) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    for (int i = 0; i < 2; i++)
      {}
    1;
  });
  (void)a;
}

void lin_bad_direct(void) {
  int a = 0;
#pragma omp simd linear(a:(X |= 1, 2)) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  for (int i = 0; i < 2; i++)
    {}
  (void)a;
}

void lin_used_direct(void) {
  int a = 0;
#pragma omp simd linear(a:((X ^= 1) + 1)) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  for (int i = 0; i < 2; i++)
    {}
  (void)a;
}

void lin_ok_read_direct(void) {
  int a = 0;
#pragma omp simd linear(a:X) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  for (int i = 0; i < 2; i++)
    {}
  (void)a;
}

void lin_ok_const_outer(void) {
  int a = 0;
  X = ({
#pragma omp simd linear(a:2)
    for (int i = 0; i < 2; i++)
      {}
    1;
  });
  (void)a;
}

//===----------------------------------------------------------------------===//
// affinity: the modifier (OMPAffinityClause::getModifier) is the one modifier
// stored INSIDE children(); the clause is address-only, which used to route
// the OMPIteratorExpr into the address-computation scan and drop it (R9-B).
//===----------------------------------------------------------------------===//

void aff_read_outer(int *p) {
  X = ({
#pragma omp task affinity(iterator(int it = 0:X):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void aff_bad_outer(int *p) {
  X = ({
#pragma omp task affinity(iterator(int it = 0:(X |= 1, 2)):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void aff_used_outer(int *p) {
  X = ({
#pragma omp task affinity(iterator(int it = 0:((X ^= 1) + 1)):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

// The locator list itself stays a storage binding: a plain read of the listed
// (non-bit) storage is not a bit read, and a constant range is fine.
void aff_ok_read_direct(int *p) {
#pragma omp task affinity(iterator(int it = 0:X):p[it]) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  {}
}

void aff_ok_const_outer(int *p) {
  X = ({
#pragma omp task affinity(iterator(int it = 0:2):p[it])
    {}
    1;
  });
}

//===----------------------------------------------------------------------===//
// array-shaping dimensions (OMPArrayShapingExpr::getDimensions(), R9-C):
// depend and affinity are address-only, so the locator goes through the
// address-computation scan, which used to visit only getBase().
//===----------------------------------------------------------------------===//

void shape_dep_read_outer(int *p) {
  X = ({
#pragma omp task depend(in:([X + 1])p) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void shape_dep_bad_outer(int *p) {
  X = ({
#pragma omp task depend(in:([(X |= 1, 2)])p) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void shape_dep_used_outer(int *p) {
  X = ({
#pragma omp task depend(in:([((X ^= 1) + 1)])p) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void shape_aff_read_outer(int *p) {
  X = ({
#pragma omp task affinity(([X + 1])p) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void shape_aff_bad_outer(int *p) {
  X = ({
#pragma omp task affinity(([(X |= 1, 2)])p) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

void shape_aff_used_outer(int *p) {
  X = ({
#pragma omp task affinity(([((X ^= 1) + 1)])p) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
    {}
    1;
  });
}

// Positive controls: constant dimensions, direct plain reads.
void shape_dep_ok_const_outer(int *p) {
  X = ({
#pragma omp task depend(in:([2])p)
    {}
    1;
  });
}

void shape_aff_ok_const_outer(int *p) {
  X = ({
#pragma omp task affinity(([2])p)
    {}
    1;
  });
}

void shape_dep_ok_read_direct(int *p) {
#pragma omp task depend(in:([X + 1])p) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  {}
}

void shape_aff_ok_read_direct(int *p) {
#pragma omp task affinity(([X + 1])p) // expected-error {{MCS251 bit types, objects and fixed-bit references are not supported in OpenMP constructs}} expected-note {{entered OpenMP construct here}}
  {}
}
