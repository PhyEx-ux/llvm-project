/* Extracted T1 kernel: demo-37 stack_push with native two arguments packed. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned long u32;
#endif
#define STACK_CAPACITY 8u

typedef struct {
    u8 *data;
    u8 length;
    u8 capacity;
    const u8 *element;
    u8 element_size;
} stack_args;

/* Native: stack_push(LArray *pArray, const LArrayElem elem).
 * T1 packs pArray and elem into one context pointer.  The element is an
 * opaque byte object so the original whole-struct assignment is preserved. */
u8 stack_push_packed(stack_args *args)
{
    u8 i;
    u16 offset;
    if (args->length >= args->capacity || args->length >= STACK_CAPACITY) return 0;
    offset = (u16)args->length * args->element_size;
    for (i = 0; i < args->element_size; i++) {
        args->data[offset + i] = args->element[i];
    }
    args->length++;
    return 1;
}
