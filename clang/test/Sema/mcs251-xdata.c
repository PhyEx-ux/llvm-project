// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s

// X1: `__xdata` maps the qualified object (or the object a pointer
// designates) onto target address space 3 (external data, 24-bit effective
// address, DESIGN.md B.2). This file pins the accepted declaration forms and
// the placement/conversion negatives; the IR-level assertions live in
// CodeGen/mcs251-xdata-code.c.

typedef unsigned char BYTE;

// Accepted forms: object, pointer, parameter, return, aggregate member,
// typedef. These come straight from the official-source shapes.
extern BYTE __xdata UsbBuffer[256];
BYTE __xdata *pdat;
void uart_send(BYTE __xdata *buf, unsigned len);   // pointer parameter
BYTE __xdata *xdata_pick(void);                    // pointer return
void xdata_arr(BYTE __xdata buf[], unsigned len);  // array parameter decays
struct XDataPtrs {
  BYTE __xdata *pdat;                              // pointer member
  char __code *rom;                                // pointer member
};
typedef char __xdata XBYTE;
XBYTE xtypedefed[4];

// Reads and AS3 stores are fine.
unsigned xdata_read(unsigned i) { return UsbBuffer[i]; }
void xdata_write(unsigned i, BYTE v) { UsbBuffer[i] = v; }
static char __xdata slocal;                        // static local is fine

// Placement negatives: automatic objects must not land in XDATA silently
// (DESIGN.md B.4; ISO/IEC TR 18037 S6.7.3).
// expected-error@+1 {{automatic variable qualified with an address space}}
void xdata_auto(void) { char __xdata la; }

// A struct field qualified with the space itself is rejected (TR 18037): an
// aggregate is placed as a whole, so a member cannot live in another space;
// pointERS into the space (struct XDataPtrs above) remain the supported form.
// expected-error@+1 {{field may not be qualified with an address space}}
struct XDataField { char __xdata f; };

// 'bit' has no address-space representation.
__bit bitobj;
// expected-error@+1 {{MCS251 'bit' cannot be declared in a non-default address space}}
__xdata __bit bitx;

// Two address spaces on one declaration.
// expected-error@+1 {{multiple address spaces specified for type}}
char __xdata __code both;

// A value return type qualified with the space has no ABI; only pointers
// into the space may be returned.
// expected-error@+1 {{function return type may not be qualified with an MCS251 '__xdata' or '__code' address space}}
char __xdata xret(void);
// expected-error@+1 {{function return type may not be qualified with an MCS251 '__xdata' or '__code' address space}}
char __xdata xret2(void) { return 0; }

// Cross-space conversions are rejected in both directions (implicit pointer
// conversions never change the address space).
char *dptr;
void conversions(char __code *cp) {
  // expected-error@+1 {{changes address space of pointer}}
  pdat = (BYTE *)dptr;
  dptr = pdat; // expected-error {{changes address space of pointer}}
  cp = pdat;   // expected-error {{changes address space of pointer}}
  pdat = cp;   // expected-error {{changes address space of pointer}}

  // Mixed-space pointer arithmetic.
  // expected-error@+1 {{non-overlapping address spaces}}
  (void)(pdat - (BYTE __code *)cp);

  // Explicit casts are the supported crossing (lowered as addrspacecast).
  dptr = (char *)pdat;
  pdat = (BYTE __xdata *)dptr;
}
