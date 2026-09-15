/*===-- compiler_float_firmware.c -----------------------------------------===*/
/*
 * Compiler-produced f32 QEMU acceptance firmware.
 *
 * This intentionally uses real C float expressions, volatile float operands,
 * and noinline emitters.  It must exercise the compiler's soft-float ABI,
 * rather than calling the uint32_t runtime helpers directly.
 */

#include <stdint.h>

#define SBUF (*(volatile uint8_t *)0x99)

union FloatBits {
  float F;
  uint32_t U;
};

__attribute__((noinline)) static float bits_to_float(uint32_t Bits) {
  union FloatBits Value;
  Value.U = Bits;
  return Value.F;
}

__attribute__((noinline)) static uint32_t float_to_bits(float Value) {
  union FloatBits Bits;
  Bits.F = Value;
  return Bits.U;
}

__attribute__((noinline)) static void uart_putc(char C) {
  SBUF = (uint8_t)C;
}

__attribute__((noinline)) static void uart_puts(const char *S) {
  while (*S)
    uart_putc(*S++);
}

__attribute__((noinline)) static void uart_hex8(uint8_t V) {
  uint8_t Hi = (uint8_t)(V >> 4) & 0x0fU;
  uint8_t Lo = V & 0x0fU;
  uart_putc((char)(Hi < 10U ? '0' + Hi : 'A' + Hi - 10U));
  uart_putc((char)(Lo < 10U ? '0' + Lo : 'A' + Lo - 10U));
}

__attribute__((noinline)) static void uart_hex32(uint32_t V) {
  uart_hex8((uint8_t)(V >> 24));
  uart_hex8((uint8_t)(V >> 16));
  uart_hex8((uint8_t)(V >> 8));
  uart_hex8((uint8_t)V);
}

/* Four finite pairs cover signs, cancellation-adjacent values and division. */
static const uint32_t InputA[] = {
    0x3fc00000U, 0xc0600000U, 0x40400000U, 0x3e800000U,
};
static const uint32_t InputB[] = {
    0x40100000U, 0x3f000000U, 0xbf800000U, 0x40800000U,
};
static const int32_t InputI[] = {0, 1, -12345, 0x01000001};
static const uint32_t InputF[] = {
    0x00000000U, 0x3f800000U, 0xc1200000U, 0x41480000U,
};

__attribute__((noinline)) static void emit_float_ops(uint32_t Index) {
  volatile float A = bits_to_float(InputA[Index]);
  volatile float B = bits_to_float(InputB[Index]);
  volatile float NaN = bits_to_float(0x7fc00000U);
  float Add = A + B;
  float Sub = A - B;
  float Mul = A * B;
  float Div = A / B;
  float Neg = -A;
  uint32_t EQ = (uint32_t)(A == B);
  uint32_t NE = (uint32_t)(A != B);
  uint32_t LT = (uint32_t)(A < B);
  uint32_t LE = (uint32_t)(A <= B);
  uint32_t GT = (uint32_t)(A > B);
  uint32_t GE = (uint32_t)(A >= B);
  uint32_t UNO = (uint32_t)__builtin_isunordered(NaN, A);

  uart_puts("FOP i="); uart_hex32(Index);
  uart_puts(" a="); uart_hex32(InputA[Index]);
  uart_puts(" b="); uart_hex32(InputB[Index]);
  uart_puts(" add="); uart_hex32(float_to_bits(Add));
  uart_puts(" sub="); uart_hex32(float_to_bits(Sub));
  uart_puts(" mul="); uart_hex32(float_to_bits(Mul));
  uart_puts(" div="); uart_hex32(float_to_bits(Div));
  uart_puts(" neg="); uart_hex32(float_to_bits(Neg));
  uart_puts(" eq="); uart_hex32(EQ);
  uart_puts(" ne="); uart_hex32(NE);
  uart_puts(" lt="); uart_hex32(LT);
  uart_puts(" le="); uart_hex32(LE);
  uart_puts(" gt="); uart_hex32(GT);
  uart_puts(" ge="); uart_hex32(GE);
  uart_puts(" uno="); uart_hex32(UNO);
  uart_putc('\n');
}

__attribute__((noinline)) static void emit_conversions(uint32_t Index) {
  volatile int32_t I = InputI[Index];
  volatile float F = bits_to_float(InputF[Index]);
  float FromI = (float)I;
  int32_t ToI = (int32_t)F;

  uart_puts("CNV i="); uart_hex32((uint32_t)I);
  uart_puts(" f="); uart_hex32(InputF[Index]);
  uart_puts(" i2f="); uart_hex32(float_to_bits(FromI));
  uart_puts(" f2i="); uart_hex32((uint32_t)ToI);
  uart_putc('\n');
}

/* G7 S1' (PM ruling 2026-09-15, D1): the unsigned i32 <-> f32 pair is
 * connected, so real C unsigned casts must compile and run.  The unsigned
 * domain spans the boundary values D1 names (0/1/0x7FFFFFFF/0x80000000/
 * 0xFFFFFFFF); the float side stays strictly inside [0, 2^32) because a
 * negative or >= 2^32 float -> unsigned cast is UB in C, which would make
 * the host oracle meaningless. */
#define NUNS 5
static const uint32_t UInputI[NUNS] = {
    0U, 1U, 0x7FFFFFFFU, 0x80000000U, 0xFFFFFFFFU,
};
static const uint32_t UInputF[NUNS] = {
    0x00000000U,  /* 0.0 */
    0x3FC00000U,  /* 1.5 */
    0x4F7FFFFFU,  /* 4294967040.0 = largest float below 2^32 */
    0x4F000000U,  /* 2147483648.0 */
    0x3F800000U,  /* 1.0 */
};

__attribute__((noinline)) static void emit_unsigned_conversions(uint32_t Index) {
  volatile uint32_t U = UInputI[Index];
  volatile float G = bits_to_float(UInputF[Index]);
  float FromU = (float)U;
  uint32_t ToU = (uint32_t)G;

  uart_puts("UCNV u="); uart_hex32((uint32_t)U);
  uart_puts(" g="); uart_hex32(UInputF[Index]);
  uart_puts(" u2f="); uart_hex32(float_to_bits(FromU));
  uart_puts(" f2u="); uart_hex32(ToU);
  uart_putc('\n');
}

int main(void) {
  uint32_t I;
  for (I = 0; I != 4U; ++I) {
    emit_float_ops(I);
    emit_conversions(I);
  }
  for (I = 0; I != NUNS; ++I) {
    emit_unsigned_conversions(I);
  }
  uart_puts("COMPILER-FLOAT-PASS\n");
  for (;;) {
  }
}
