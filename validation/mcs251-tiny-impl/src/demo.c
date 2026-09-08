typedef unsigned char u8;
typedef unsigned short u16;

#ifdef HOST_BUILD
#include <stdio.h>
typedef volatile u8 sfr8;
#else
typedef volatile u8 __attribute__((address_space(6))) sfr8;
#define SBUF (*(sfr8 *)0x99u)
_Static_assert(sizeof(void *) == 2, "near data pointers must be two bytes");
#endif

#ifndef MODEL_X
#define MODEL_X 0
#endif

_Static_assert(sizeof(u8) == 1 && sizeof(u16) == 2, "scalar widths");

static volatile u8 arena[16];
static volatile u16 word_slot;

struct node {
  volatile u8 value;
  volatile struct node *next;
};

static void putc_target(u8 c) {
#ifdef HOST_BUILD
  putchar(c);
#else
  SBUF = c;
#endif
}

static void hex4(u8 v) {
  v &= 15u;
  putc_target((u8)(v < 10u ? 48u + v : 65u + v - 10u));
}

static void hex8(u8 v) {
  hex4((u8)(v >> 4));
  hex4(v);
}

static void hex16(u16 v) {
  hex8((u8)(v >> 8));
  hex8((u8)v);
}

u8 read_after_call(u8 delta, volatile u8 *p);
u8 static_pointer_arg(u8 salt, volatile u8 *p);

static u16 fill_and_sum(void) {
  volatile u8 *p = arena;
  volatile u8 *end = arena + 8;
  u8 value = 1u;
  u16 sum = 0u;
  while (p != end) {
    *p++ = value;
    value = (u8)(value + 3u);
  }
  for (p = arena; p != end; ++p)
    sum = (u16)(sum + *p);
  return sum;
}

static u8 walk_chain(void) {
  volatile struct node nodes[3];
  volatile struct node *p;
  u8 sum = 0u;
  nodes[0].value = 0x11u;
  nodes[1].value = 0x22u;
  nodes[2].value = 0x33u;
  nodes[0].next = &nodes[1];
  nodes[1].next = &nodes[2];
  nodes[2].next = (volatile struct node *)0;
  for (p = &nodes[0]; p; p = p->next)
    sum = (u8)(sum + p->value);
  return sum;
}

static u16 mixed_access(void) {
  volatile u16 *word = &word_slot;
  volatile u8 *byte = &arena[9];
  *word = 0x1234u;
  *byte = 0xABu;
  return (u16)(*word + *byte);
}

int main(void) {
  u16 array_sum = fill_and_sum();
  u8 slot_result = static_pointer_arg(0x5Au, arena);
  u8 chain_sum = walk_chain();
  u16 mixed_result = mixed_access();
  u8 spill_result = read_after_call(3u, &arena[3]);

  putc_target((u8)(MODEL_X ? 88u : 84u));
  putc_target(32u);
  putc_target(80u); putc_target(61u);
#ifdef HOST_BUILD
  hex8(2u);
#else
  hex8((u8)sizeof(void *));
#endif
  putc_target(32u);
  putc_target(65u); putc_target(61u); hex16(array_sum);
  putc_target(32u);
  putc_target(83u); putc_target(61u); hex8(slot_result);
  putc_target(32u);
  putc_target(67u); putc_target(61u); hex8(chain_sum);
  putc_target(32u);
  putc_target(87u); putc_target(61u); hex16(mixed_result);
  putc_target(32u);
  putc_target(82u); putc_target(61u); hex8(spill_result);
  putc_target(10u);

  if (MODEL_X) {
    putc_target(88u); putc_target(84u); putc_target(73u); putc_target(78u);
    putc_target(89u); putc_target(45u);
  } else {
    putc_target(84u); putc_target(73u); putc_target(78u); putc_target(89u);
    putc_target(45u);
  }
  putc_target(80u); putc_target(65u); putc_target(83u); putc_target(83u);
  putc_target(10u);
#ifdef HOST_BUILD
  return 0;
#else
  for (;;) { }
#endif
}
