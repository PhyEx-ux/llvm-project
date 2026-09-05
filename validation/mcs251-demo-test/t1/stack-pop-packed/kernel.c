/* Extracted T1 kernel: demo-37 stack_pop with native two arguments packed. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

typedef struct {
    u8 *data;
    u8 length;
    u8 *out;
} stack_pop_args;

/* Native: stack_pop(LArray *pArray, LArrayElem *pElem), packed into one
 * context pointer. The output buffer is the observable result. */
u8 stack_pop_packed(stack_pop_args *args)
{
    if (args->length == 0) return 0;
    if (args->out != (u8 *)0) *args->out = args->data[args->length - 1];
    args->length--;
    return 1;
}
