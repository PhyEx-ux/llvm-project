/*
 * Oracle-A host driver for pilot-judge-type.
 * Checkpoint stream: 'B' + <tag><HEX8>* + "PASS\n".
 * Exercises both calculator modes; mode switches via the kernel's global.
 */
#include <stdio.h>

#include "kernel.c"

u8 g_chCalcStatus;     /* kernel state lives in the driver (see kernel.c) */

static void ck(char tag, u8 v)
{
    putchar(tag);
    printf("%02X", v);
}

int main(void)
{
    putchar('B');
    g_chCalcStatus = CALC_NORMAL;    /* normal mode */
    ck('a', alg_judge_type('+'));    /* OPERATOR */
    ck('b', alg_judge_type('d'));    /* OPERATOR via DEGREE_FONT */
    ck('c', alg_judge_type('5'));    /* NUMBER */
    ck('d', alg_judge_type('.'));    /* NUMBER */
    ck('e', alg_judge_type('e'));    /* CONST_NUM */
    ck('f', alg_judge_type('p'));    /* CONST_NUM via PI_FONT */
    ck('g', alg_judge_type('s'));    /* FUNCTION */
    ck('h', alg_judge_type('i'));    /* INVALID in normal mode */
    ck('i', alg_judge_type('q'));    /* INVALID_TYPE */

    g_chCalcStatus = CALC_COMPLEX;   /* complex mode */
    ck('j', alg_judge_type('i'));    /* OPERATOR in complex mode */
    ck('k', alg_judge_type('d'));    /* not an operator here -> ? */
    ck('l', alg_judge_type('x'));    /* CONST_NUM */
    ck('m', alg_judge_type('s'));    /* no FUNCTION branch in complex mode */
    ck('n', alg_judge_type('('));    /* OPERATOR */

    g_chCalcStatus = 7;              /* undefined mode: falls through */
    ck('o', alg_judge_type('+'));    /* final INVALID_TYPE */
    puts("PASS");
    return 0;
}
