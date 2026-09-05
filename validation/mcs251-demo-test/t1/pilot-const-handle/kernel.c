/*
 * T1 pilot kernel: alg_const_handle -- constant-symbol classifier (switch lookup).
 *
 * Source: STC32G144K246 demo 37 (scientific calculator),
 *   37-科学计算器使用CDC虚拟液晶屏显示/Resource/alg_caculate.c:176
 * Return enum from Resource/alg_caculate.h:14 (CONST_FLAG);
 * PI_FONT/'p' and DEGREE_FONT/'d' macros folded in from alg_caculate.c:12-13.
 *
 * Rewrite per DESIGN.md section 2-T1 (types only, semantics unchanged):
 *   - uchar -> u8 (explicit width typedef, self-contained)
 *   - char parameter -> u8: the MCS251 backend has no 8-to-16 bit sign
 *     extension yet ("sign-extending loads are not supported", llc 24,
 *     2026-09-05) and a signed char parameter arrives signext from the
 *     clang/x86 front end.  Behaviour equivalence for this function: every
 *     branch compares c against ASCII literals; for byte values >= 0x80,
 *     signed char (negative) and u8 (>= 128) both fall through every
 *     comparison to NO_CONST, and ASCII inputs compare identically.
 *   - enum CONST_FLAG -> u8 literal macros, original member order preserved
 *     (E_FLAG=0 .. NO_CONST=6)
 *   - switch -> if/else chain: the backend cannot select br_jt jump tables
 *     ("Cannot select: ch = br_jt", llc 24, 2026-09-05).  An if/else chain
 *     is the same control flow as a sparse switch; restore the switch form
 *     once jump-table selection lands (tracked in RESULTS.md).
 *   - no SFR / interrupt / bit types in the original; nothing removed
 */

typedef unsigned char u8;

#define E_FLAG    0u   /* e */
#define PI_FLAG   1u   /* PI_FONT 'p' */
#define X_FLAG    2u
#define Y_FLAG    3u
#define Z_FLAG    4u
#define ANS_FLAG  5u
#define NO_CONST  6u

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
