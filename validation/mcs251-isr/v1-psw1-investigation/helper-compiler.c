#include "hardware.h"
__attribute__((noinline)) u8 helper(u8 value)
{
    volatile u8 work[4];
    for (u8 i = 0; i < 4; ++i)
        work[i] = (u8)(value ^ (u8)(0x31 + i));
    return (u8)(work[0] + work[1] + work[2] + work[3]);
}
