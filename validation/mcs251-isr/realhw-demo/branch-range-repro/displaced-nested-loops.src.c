unsigned f(unsigned s, unsigned n) {
  volatile unsigned char M[16];
for (unsigned i0 = 0; i0 < 3; ++i0) {
  if (s == M[1]) {
    for (unsigned i2 = 0; i2 < 15; ++i2) {
      s += M[4];
    }
    for (unsigned i2 = 0; i2 < 16; ++i2) {
      s += M[14];
      s += M[10];
      s += M[12];
    }
  }
  for (unsigned i1 = 0; i1 < 15; ++i1) {
    if (M[10] != s) {
      s += M[12];
    }
    s += M[4];
  }
}
for (unsigned i0 = 0; i0 < 11; ++i0) {
  if (s == M[14]) {
    for (unsigned i2 = 0; i2 < 8; ++i2) {
    }
    s += M[4];
  }
}
}