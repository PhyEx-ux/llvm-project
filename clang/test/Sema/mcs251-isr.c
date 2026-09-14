// G1-3: full Sema matrix for the 127-slot profile. main.c keeps the G1-1
// boundary set and gains the complete negative matrix (every non-Legal slot,
// the rejected upper-bound alternatives) plus the C-constant and identity
// probes; legal-sweep.c registers one function on each of the 109 Legal
// slots in a single TU, which -verify accepts only if none of them produces
// a diagnostic. The two sections are separate TUs so the sweep's slot usage
// cannot collide with the teaching cases in main.c.
// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %t/main.c
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %t/legal-sweep.c

//--- main.c
#define VEC (24)
void good(void) __attribute__((interrupt(VEC)));
void good(void) {}

void neg(void) __attribute__((interrupt(-1))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void gap(void) __attribute__((interrupt(7))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void transfer(void) __attribute__((interrupt(13))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void system14(void) __attribute__((interrupt(14))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void system15(void) __attribute__((interrupt(15))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void high52(void) __attribute__((interrupt(52))); // accepted: 52 became Legal
void high52(void) {}
void high126(void) __attribute__((interrupt(126))); // accepted: the new upper bound
void high126(void) {}
void reserved100(void) __attribute__((interrupt(100))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void high81(void) __attribute__((interrupt(81))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void over127(void) __attribute__((interrupt(127))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void wide(void) __attribute__((interrupt(0x100000001ULL))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}

// G1-3: the remaining in-profile Reserved gaps (NoSource evidence) and the
// four HeaderOnly slots above 90 all share the one slot diagnostic.
void gap22(void) __attribute__((interrupt(22))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void gap23(void) __attribute__((interrupt(23))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void gap32(void) __attribute__((interrupt(32))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void gap33(void) __attribute__((interrupt(33))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void gap34(void) __attribute__((interrupt(34))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void gap35(void) __attribute__((interrupt(35))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void reserved92(void) __attribute__((interrupt(92))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void reserved93(void) __attribute__((interrupt(93))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void reserved94(void) __attribute__((interrupt(94))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void reserved95(void) __attribute__((interrupt(95))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void reserved101(void) __attribute__((interrupt(101))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void reserved113(void) __attribute__((interrupt(113))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}

// G1-3: out-of-profile upper-bound alternatives. 132 is the rejected
// padded-profile bound, 133 the first number past it; 255 and 65535 check
// the slot-field width does not truncate either value into a low slot.
void over132(void) __attribute__((interrupt(132))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void over133(void) __attribute__((interrupt(133))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void over255(void) __attribute__((interrupt(255))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}
void over65535(void) __attribute__((interrupt(65535))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}

// G1-3: the C side rules on integer VALUES, not spellings -- hexadecimal and
// enum constants are fine here (only IR metadata text must be canonical
// decimal, which the Verifier tests cover). 0x60 is the Legal high slot 96;
// the enum constant 0x7F is 127, one past the bound, and is rejected by value.
enum { MID = 45, PAST_TOP = 0x7F };
void hexlegal(void) __attribute__((interrupt(0x60))); // accepted: 96 in hex
void hexlegal(void) {}
void enumlegal(void) __attribute__((interrupt(MID))); // accepted: enum constant 45
void enumlegal(void) {}
void hexbad(void) __attribute__((interrupt(PAST_TOP))); // expected-error {{MCS251 interrupt vector must be a legal slot in 0-126}}

int runtime_slot;
void nonice(void) __attribute__((interrupt(runtime_slot))); // expected-error {{integer constant expression}}
void zero(void) __attribute__((interrupt)); // expected-error {{takes one argument}}
void two(void) __attribute__((interrupt(1, 2))); // expected-error {{takes one argument}}

int badret(void) __attribute__((interrupt(1))); // expected-error {{MCS251 interrupt function must have type void(void)}}
void arg(int x) __attribute__((interrupt(2))); // expected-error {{MCS251 interrupt function must have type void(void)}}
void variadic(int x, ...) __attribute__((interrupt(3))); // expected-error {{MCS251 interrupt function must have type void(void)}}

void late(void);
void late(void) __attribute__((interrupt(4))); // expected-error {{MCS251 interrupt identity must be established on the first declaration}}

void mismatch(void) __attribute__((interrupt(5)));
void mismatch(void) __attribute__((interrupt(6))); // expected-error {{conflicting MCS251 interrupt vector}}

void duplicate_a(void) __attribute__((interrupt(8)));
void duplicate_a(void) {}
void duplicate_b(void) __attribute__((interrupt(8)));
void duplicate_b(void) {} // expected-error {{duplicate MCS251 interrupt vector}}

// G1-3: identity rules at high slots behave exactly as at low slots -- a
// repeated declaration of the same function keeps its slot, a second
// function on the same high slot is a duplicate registration, and two
// different numbers on one function conflict.
void high_redecl(void) __attribute__((interrupt(59)));
void high_redecl(void) __attribute__((interrupt(59))); // same slot, same function: accepted
void high_redecl(void) {}
void high_dup_a(void) __attribute__((interrupt(102)));
void high_dup_a(void) {}
void high_dup_b(void) __attribute__((interrupt(102)));
void high_dup_b(void) {} // expected-error {{duplicate MCS251 interrupt vector}}
void high_conflict(void) __attribute__((interrupt(114)));
void high_conflict(void) __attribute__((interrupt(115))); // expected-error {{conflicting MCS251 interrupt vector}}

void naked_isr(void) __attribute__((interrupt(9), naked)); // expected-error {{incompatible with MCS251 interrupt}}
void inline_isr(void) __attribute__((interrupt(10), always_inline)); // expected-error {{incompatible with MCS251 interrupt}}

void use(void) {
  good(); // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
  void (*p)(void) = good; // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
  (void)&good; // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
  (void)(unsigned long)&good; // expected-error {{MCS251 interrupt entry cannot be used as an ordinary function}}
}

//--- legal-sweep.c
// One function per Legal slot, all 109 of them (0..126 minus the 16 Reserved
// and 2 System). -verify with no expected-* directives means any diagnostic
// on any slot fails the test. Low legacy Legals, the three reclassified
// slots (31/45/46) and the high dual-source block (52..126) are all here,
// each exactly once.
void s0(void) __attribute__((interrupt(0)));
void s0(void) {}
void s1(void) __attribute__((interrupt(1)));
void s1(void) {}
void s2(void) __attribute__((interrupt(2)));
void s2(void) {}
void s3(void) __attribute__((interrupt(3)));
void s3(void) {}
void s4(void) __attribute__((interrupt(4)));
void s4(void) {}
void s5(void) __attribute__((interrupt(5)));
void s5(void) {}
void s6(void) __attribute__((interrupt(6)));
void s6(void) {}
void s8(void) __attribute__((interrupt(8)));
void s8(void) {}
void s9(void) __attribute__((interrupt(9)));
void s9(void) {}
void s10(void) __attribute__((interrupt(10)));
void s10(void) {}
void s11(void) __attribute__((interrupt(11)));
void s11(void) {}
void s12(void) __attribute__((interrupt(12)));
void s12(void) {}
void s16(void) __attribute__((interrupt(16)));
void s16(void) {}
void s17(void) __attribute__((interrupt(17)));
void s17(void) {}
void s18(void) __attribute__((interrupt(18)));
void s18(void) {}
void s19(void) __attribute__((interrupt(19)));
void s19(void) {}
void s20(void) __attribute__((interrupt(20)));
void s20(void) {}
void s21(void) __attribute__((interrupt(21)));
void s21(void) {}
void s24(void) __attribute__((interrupt(24)));
void s24(void) {}
void s25(void) __attribute__((interrupt(25)));
void s25(void) {}
void s26(void) __attribute__((interrupt(26)));
void s26(void) {}
void s27(void) __attribute__((interrupt(27)));
void s27(void) {}
void s28(void) __attribute__((interrupt(28)));
void s28(void) {}
void s29(void) __attribute__((interrupt(29)));
void s29(void) {}
void s30(void) __attribute__((interrupt(30)));
void s30(void) {}
void s31(void) __attribute__((interrupt(31)));
void s31(void) {}
void s36(void) __attribute__((interrupt(36)));
void s36(void) {}
void s37(void) __attribute__((interrupt(37)));
void s37(void) {}
void s38(void) __attribute__((interrupt(38)));
void s38(void) {}
void s39(void) __attribute__((interrupt(39)));
void s39(void) {}
void s40(void) __attribute__((interrupt(40)));
void s40(void) {}
void s41(void) __attribute__((interrupt(41)));
void s41(void) {}
void s42(void) __attribute__((interrupt(42)));
void s42(void) {}
void s43(void) __attribute__((interrupt(43)));
void s43(void) {}
void s44(void) __attribute__((interrupt(44)));
void s44(void) {}
void s45(void) __attribute__((interrupt(45)));
void s45(void) {}
void s46(void) __attribute__((interrupt(46)));
void s46(void) {}
void s47(void) __attribute__((interrupt(47)));
void s47(void) {}
void s48(void) __attribute__((interrupt(48)));
void s48(void) {}
void s49(void) __attribute__((interrupt(49)));
void s49(void) {}
void s50(void) __attribute__((interrupt(50)));
void s50(void) {}
void s51(void) __attribute__((interrupt(51)));
void s51(void) {}
void s52(void) __attribute__((interrupt(52)));
void s52(void) {}
void s53(void) __attribute__((interrupt(53)));
void s53(void) {}
void s54(void) __attribute__((interrupt(54)));
void s54(void) {}
void s55(void) __attribute__((interrupt(55)));
void s55(void) {}
void s56(void) __attribute__((interrupt(56)));
void s56(void) {}
void s57(void) __attribute__((interrupt(57)));
void s57(void) {}
void s58(void) __attribute__((interrupt(58)));
void s58(void) {}
void s59(void) __attribute__((interrupt(59)));
void s59(void) {}
void s60(void) __attribute__((interrupt(60)));
void s60(void) {}
void s61(void) __attribute__((interrupt(61)));
void s61(void) {}
void s62(void) __attribute__((interrupt(62)));
void s62(void) {}
void s63(void) __attribute__((interrupt(63)));
void s63(void) {}
void s64(void) __attribute__((interrupt(64)));
void s64(void) {}
void s65(void) __attribute__((interrupt(65)));
void s65(void) {}
void s66(void) __attribute__((interrupt(66)));
void s66(void) {}
void s67(void) __attribute__((interrupt(67)));
void s67(void) {}
void s68(void) __attribute__((interrupt(68)));
void s68(void) {}
void s69(void) __attribute__((interrupt(69)));
void s69(void) {}
void s70(void) __attribute__((interrupt(70)));
void s70(void) {}
void s71(void) __attribute__((interrupt(71)));
void s71(void) {}
void s72(void) __attribute__((interrupt(72)));
void s72(void) {}
void s73(void) __attribute__((interrupt(73)));
void s73(void) {}
void s74(void) __attribute__((interrupt(74)));
void s74(void) {}
void s75(void) __attribute__((interrupt(75)));
void s75(void) {}
void s76(void) __attribute__((interrupt(76)));
void s76(void) {}
void s77(void) __attribute__((interrupt(77)));
void s77(void) {}
void s78(void) __attribute__((interrupt(78)));
void s78(void) {}
void s79(void) __attribute__((interrupt(79)));
void s79(void) {}
void s80(void) __attribute__((interrupt(80)));
void s80(void) {}
void s82(void) __attribute__((interrupt(82)));
void s82(void) {}
void s83(void) __attribute__((interrupt(83)));
void s83(void) {}
void s84(void) __attribute__((interrupt(84)));
void s84(void) {}
void s85(void) __attribute__((interrupt(85)));
void s85(void) {}
void s86(void) __attribute__((interrupt(86)));
void s86(void) {}
void s87(void) __attribute__((interrupt(87)));
void s87(void) {}
void s88(void) __attribute__((interrupt(88)));
void s88(void) {}
void s89(void) __attribute__((interrupt(89)));
void s89(void) {}
void s90(void) __attribute__((interrupt(90)));
void s90(void) {}
void s91(void) __attribute__((interrupt(91)));
void s91(void) {}
void s96(void) __attribute__((interrupt(96)));
void s96(void) {}
void s97(void) __attribute__((interrupt(97)));
void s97(void) {}
void s98(void) __attribute__((interrupt(98)));
void s98(void) {}
void s99(void) __attribute__((interrupt(99)));
void s99(void) {}
void s102(void) __attribute__((interrupt(102)));
void s102(void) {}
void s103(void) __attribute__((interrupt(103)));
void s103(void) {}
void s104(void) __attribute__((interrupt(104)));
void s104(void) {}
void s105(void) __attribute__((interrupt(105)));
void s105(void) {}
void s106(void) __attribute__((interrupt(106)));
void s106(void) {}
void s107(void) __attribute__((interrupt(107)));
void s107(void) {}
void s108(void) __attribute__((interrupt(108)));
void s108(void) {}
void s109(void) __attribute__((interrupt(109)));
void s109(void) {}
void s110(void) __attribute__((interrupt(110)));
void s110(void) {}
void s111(void) __attribute__((interrupt(111)));
void s111(void) {}
void s112(void) __attribute__((interrupt(112)));
void s112(void) {}
void s114(void) __attribute__((interrupt(114)));
void s114(void) {}
void s115(void) __attribute__((interrupt(115)));
void s115(void) {}
void s116(void) __attribute__((interrupt(116)));
void s116(void) {}
void s117(void) __attribute__((interrupt(117)));
void s117(void) {}
void s118(void) __attribute__((interrupt(118)));
void s118(void) {}
void s119(void) __attribute__((interrupt(119)));
void s119(void) {}
void s120(void) __attribute__((interrupt(120)));
void s120(void) {}
void s121(void) __attribute__((interrupt(121)));
void s121(void) {}
void s122(void) __attribute__((interrupt(122)));
void s122(void) {}
void s123(void) __attribute__((interrupt(123)));
void s123(void) {}
void s124(void) __attribute__((interrupt(124)));
void s124(void) {}
void s125(void) __attribute__((interrupt(125)));
void s125(void) {}
void s126(void) __attribute__((interrupt(126)));
void s126(void) {}

// expected-no-diagnostics
