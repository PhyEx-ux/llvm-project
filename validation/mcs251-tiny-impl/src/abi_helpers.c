typedef unsigned char u8;

__attribute__((noinline)) static u8 bump(u8 value) {
  return (u8)(value + 1u);
}

__attribute__((noinline)) u8 read_after_call(u8 delta, volatile u8 *p) {
  u8 before = *p;
  u8 adjusted = bump(delta);
  return (u8)(before + *p + adjusted);
}

__attribute__((noinline)) u8 static_pointer_arg(u8 salt, volatile u8 *p) {
  p[2] = (u8)(p[0] ^ p[1] ^ salt);
  return p[2];
}
