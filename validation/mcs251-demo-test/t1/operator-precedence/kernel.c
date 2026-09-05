/* Extracted T1 kernel: demo-37 alg_compare_level with native two chars packed. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
#define LEVEL_BIGGER 0u
#define LEVEL_SMALLER 1u
#define LEVEL_SAME 2u
#define LEVEL_INVALID 3u

typedef struct {
    u8 operator1;
    u8 operator2;
} precedence_args;

static u8 precedence(u8 op)
{
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    if (op == '^' || op == 'i' || op == '!' || op == 'd') return 3;
    if (op == 'f') return 4;
    if (op == '(' || op == ')') return 5;
    return 0;
}

/* Native alg_compare_level(char operator1, char operator2) is packed into one
 * pointer parameter; operator semantics remain the original table. */
u8 compare_level_packed(const precedence_args *args)
{
    u8 a = precedence(args->operator1);
    u8 b = precedence(args->operator2);
    if (a == 0 || b == 0) return LEVEL_INVALID;
    if (a > b) return LEVEL_BIGGER;
    if (a < b) return LEVEL_SMALLER;
    return LEVEL_SAME;
}
