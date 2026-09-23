// RUN: %clang_cc1 -triple mcs251-unknown-none -std=gnu++17 -fsyntax-only -verify %s
//
// A trivial C++ copy constructor receives its source by reference, so the
// source expression has no lvalue-to-rvalue conversion for the atomic-use
// scanner to find. The generated memberwise copy still reads every subobject.

struct AtomicMember {
  _Atomic(int) value;
  int ordinary;
};

void use(AtomicMember *);

void copy_atomic_member(AtomicMember *source) {
  // expected-error@+1 {{access to an object with an atomic subobject}}
  AtomicMember copy = *source;
  use(&copy);
}

void read_only_ordinary_member(AtomicMember *source) {
  int value = source->ordinary;
  (void)value;
}

void inspect_by_reference(const AtomicMember &);
void passing_reference_is_not_a_copy(AtomicMember *source) {
  inspect_by_reference(*source);
}

void move_atomic_member(AtomicMember *source) {
  // expected-error@+1 {{access to an object with an atomic subobject}}
  AtomicMember moved = static_cast<AtomicMember &&>(*source);
  use(&moved);
}

void unevaluated_copy(AtomicMember *source) {
  (void)sizeof(AtomicMember(*source));
  (void)noexcept(AtomicMember(*source));
}

void dead_arm_copy(AtomicMember *source) {
  AtomicMember copy = false ? AtomicMember(*source) : AtomicMember{};
  use(&copy);
}

void selected_arm_copy(AtomicMember *source) {
  // expected-error@+1 {{access to an object with an atomic subobject}}
  AtomicMember copy = true ? AtomicMember(*source) : AtomicMember{};
  use(&copy);
}

struct AtomicBase {
  _Atomic(int) value;
};
struct DerivedAtomic : AtomicBase {
  int ordinary;
};
void copy_atomic_base(DerivedAtomic *source) {
  // expected-error@+1 {{access to an object with an atomic subobject}}
  DerivedAtomic copy = *source;
  (void)copy.ordinary;
}

// A user-provided copy constructor that reads only the ordinary member is not
// an implicit bitwise copy of the atomic subobject.
struct UserCopy {
  _Atomic(int) value;
  int ordinary;
  UserCopy(const UserCopy &other) : ordinary(other.ordinary) {}
};
void user_copy_reads_only_ordinary(UserCopy *source) {
  UserCopy copy = *source;
  (void)copy.ordinary;
}

// An outer implicit copy calls this user-provided subobject copy. Its
// constructor intentionally skips the atomic member, so the enclosing nontrivial copy is
// accepted rather than rejected just from its recursive object type.
struct SafeSubobjectCopy {
  _Atomic(int) value;
  int ordinary;
  SafeSubobjectCopy(const SafeSubobjectCopy &other)
      : ordinary(other.ordinary) {}
};
struct OuterWithSafeSubobjectCopy {
  SafeSubobjectCopy member;
  int ordinary;
};
void outer_copy_uses_safe_subobject_copy(OuterWithSafeSubobjectCopy *source) {
  OuterWithSafeSubobjectCopy copy = *source;
  (void)copy.ordinary;
}

struct OrdinaryRecord {
  int value;
};
void ordinary_copy(OrdinaryRecord *source) {
  OrdinaryRecord copy = *source;
  (void)copy.value;
}

// Operator and conversion function names are not identifier-backed; visiting
// these direct callees must not call FunctionDecl::getName() on them.
struct Callable {
  int operator()(int value) const { return value; }
};
struct Convertible {
  operator int() const { return 7; }
};
void operator_and_conversion_calls() {
  Callable call;
  Convertible convert;
  int value = call(1) + convert;
  (void)value;
}
