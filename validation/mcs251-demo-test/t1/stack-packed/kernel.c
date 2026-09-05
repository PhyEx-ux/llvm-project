/* Extracted T1 kernel: demo-37 stack_push with native two arguments packed. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif
#define STACK_CAPACITY 8u

typedef struct {
    u8 *data;
    u8 length;
    u8 capacity;
    u8 element;
} stack_args;

/* Native: stack_push(LArray *pArray, const LArrayElem elem).
 * T1 packs pArray and elem into one context pointer to stay single-parameter. */
u8 stack_push_packed(stack_args *args)
{
    if (args->length >= args->capacity || args->length >= STACK_CAPACITY) return 0;
    args->data[args->length++] = args->element;
    return 1;
}
