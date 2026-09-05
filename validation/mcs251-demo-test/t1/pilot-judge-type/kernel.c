/*
 * T1 pilot kernel: alg_judge_type -- input character classifier state machine.
 *
 * Source: STC32G144K246 demo 37 (scientific calculator),
 *   37-科学计算器使用CDC虚拟液晶屏显示/Resource/alg_caculate.c:101
 * Its callee alg_const_handle is extracted alongside (alg_caculate.c:176);
 * TYPE_FLAG enum values from Resource/alg_linearlist.h:19, CONST_FLAG from
 * Resource/alg_caculate.h:14, CALC_STATUS from alg_caculate.h:43.
 * g_chCalcStatus is a global in the original (extern uchar, set by the menu
 * layer); it stays a global here so the harness can switch modes.
 *
 * Rewrite per DESIGN.md section 2-T1 (types only, semantics unchanged):
 *   - uchar -> u8; enum -> u8 literal macros, original member order kept
 *   - char parameter -> u8 (both functions): the MCS251 backend lacks 8-to-16
 *     bit sign extension and a signed char parameter arrives signext from
 *     the clang/x86 front end.  Equivalence: every branch compares c against
 *     ASCII literals; bytes >= 0x80 (negative as char) and >= 128 (as u8)
 *     both fall through to the same default branches, ASCII compares are
 *     identical.
 *   - PI_FONT 'p' / DEGREE_FONT 'd' macros folded in (alg_caculate.c:12-13)
 *   - switch -> if/else chains (backend br_jt gap, same as
 *     pilot-const-handle; restore when jump tables select)
 *   - no SFR/interrupt/bit constructs in these two functions
 * The call judge_type -> const_handle is a real demo call chain and is kept.
 */

typedef unsigned char u8;

/* CALC_STATUS */
#define CALC_NORMAL   0u
#define CALC_COMPLEX  1u

/* TYPE_FLAG (alg_linearlist.h) */
#define NUMBER        0u
#define OPERATOR      1u
#define CONST_NUM     2u
#define FUNCTION      3u
#define INVALID_TYPE  4u

/* CONST_FLAG (alg_caculate.h) */
#define E_FLAG        0u
#define PI_FLAG       1u
#define X_FLAG        2u
#define Y_FLAG        3u
#define Z_FLAG        4u
#define ANS_FLAG      5u
#define NO_CONST      6u

/* Storage note: extern declaration only -- the MCS251 backend rejects
 * defined global data (Phase 12 Step 2 pending); the driver defines it.
 * In the demo the menu layer owns this global; here the drivers do. */
extern u8 g_chCalcStatus;

u8 alg_const_handle(u8 c)
{
    if (c == 'e') {
        return E_FLAG;
    } else if (c == 'p') {          /* was: case PI_FONT */
        return PI_FLAG;
    } else if (c == 'x') {
        return X_FLAG;
    } else if (c == 'y') {
        return Y_FLAG;
    } else if (c == 'z') {
        return Z_FLAG;
    } else if (c == 'A') {
        return ANS_FLAG;
    }

    return NO_CONST;
}

u8 alg_judge_type(u8 c)
{
    /* normal calculator mode */
    if (g_chCalcStatus == CALC_NORMAL)
    {
        if (c == '+' ||
            c == '-' ||
            c == '*' ||
            c == '/' ||
            c == '^' ||
            c == '(' ||
            c == ')' ||
            c == '!' ||
            c == 'd'               /* was: DEGREE_FONT */
            )
        {
            return OPERATOR;
        }
        else if ((c >= '0' && c <= '9') || c == '.')
        {
            return NUMBER;
        }
        else if (alg_const_handle(c) != NO_CONST)    /* constant symbol */
        {
            return CONST_NUM;
        }
        else if (c == 'l' || c == 's' || c == 'c' || c == 't' || c == 'a')
        {
            return FUNCTION;
        }
        else
        {
            return INVALID_TYPE;
        }
    }
    /* complex-number mode */
    else if (g_chCalcStatus == CALC_COMPLEX)
    {
        if (c == '+' ||
            c == '-' ||
            c == '*' ||
            c == '/' ||
            c == '^' ||
            c == '(' ||
            c == ')' ||
            c == 'i'               /* imaginary marker */
            )
        {
            return OPERATOR;
        }
        else if ((c >= '0' && c <= '9') || c == '.')
        {
            return NUMBER;
        }
        else if (alg_const_handle(c) != NO_CONST)    /* constant symbol */
        {
            return CONST_NUM;
        }
        else
        {
            return INVALID_TYPE;
        }
    }

    return INVALID_TYPE;
}
