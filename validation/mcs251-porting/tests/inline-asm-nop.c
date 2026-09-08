/* Compile the public v1 compatibility spelling, not a hand-written surrogate. */
#include "../include/intrins.h"

void inline_asm_nop_probe(void)
{
    _nop_();
}
