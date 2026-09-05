/* Extracted T1 kernel: demo-37 stack_pop with native two arguments packed. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned long u32;
#endif

typedef struct {
    u8 *data;
    u8 length;
    u8 *out;
    u8 element_size;
} stack_pop_args;

/* Native: stack_pop(LArray *pArray, LArrayElem *pElem), packed into one
 * context pointer.  Elements are copied as opaque bytes. */
u8 stack_pop_packed(stack_pop_args *args)
{
    u8 i;
    u16 offset;
    if (args->length == 0) return 0;
    offset = (u16)(args->length - 1u) * args->element_size;
    if (args->out != (u8 *)0) {
        for (i = 0; i < args->element_size; i++) {
            args->out[i] = args->data[offset + i];
        }
    }
    args->length--;
    return 1;
}
