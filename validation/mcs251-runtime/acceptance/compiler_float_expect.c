/*===-- compiler_float_expect.c -------------------------------------------===*/
/* Host oracle for compiler_float_firmware.c. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

union FloatBits {
  float F;
  uint32_t U;
};

static float bits_to_float(uint32_t Bits) {
  union FloatBits Value;
  Value.U = Bits;
  return Value.F;
}

static uint32_t float_to_bits(float Value) {
  union FloatBits Bits;
  Bits.F = Value;
  return Bits.U;
}

static void hex32(uint32_t V) { printf("%08X", V); }

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

static void emit_float_ops(uint32_t Index) {
  volatile float A = bits_to_float(InputA[Index]);
  volatile float B = bits_to_float(InputB[Index]);
  volatile float NaN = bits_to_float(0x7fc00000U);
  float Add = A + B;
  float Sub = A - B;
  float Mul = A * B;
  float Div = A / B;
  float Neg = -A;

  printf("FOP i="); hex32(Index);
  printf(" a="); hex32(InputA[Index]);
  printf(" b="); hex32(InputB[Index]);
  printf(" add="); hex32(float_to_bits(Add));
  printf(" sub="); hex32(float_to_bits(Sub));
  printf(" mul="); hex32(float_to_bits(Mul));
  printf(" div="); hex32(float_to_bits(Div));
  printf(" neg="); hex32(float_to_bits(Neg));
  printf(" eq="); hex32((uint32_t)(A == B));
  printf(" ne="); hex32((uint32_t)(A != B));
  printf(" lt="); hex32((uint32_t)(A < B));
  printf(" le="); hex32((uint32_t)(A <= B));
  printf(" gt="); hex32((uint32_t)(A > B));
  printf(" ge="); hex32((uint32_t)(A >= B));
  printf(" uno="); hex32((uint32_t)__builtin_isunordered(NaN, A));
  putchar('\n');
}

static void emit_conversions(uint32_t Index) {
  volatile int32_t I = InputI[Index];
  volatile float F = bits_to_float(InputF[Index]);
  float FromI = (float)I;
  int32_t ToI = (int32_t)F;

  printf("CNV i="); hex32((uint32_t)I);
  printf(" f="); hex32(InputF[Index]);
  printf(" i2f="); hex32(float_to_bits(FromI));
  printf(" f2i="); hex32((uint32_t)ToI);
  putchar('\n');
}

int main(void) {
  uint32_t I;
  for (I = 0; I != 4U; ++I) {
    emit_float_ops(I);
    emit_conversions(I);
  }
  puts("COMPILER-FLOAT-PASS");
  return 0;
}
