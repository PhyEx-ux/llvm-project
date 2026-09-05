/* Extracted T1 kernel: demo-37 stack_pop with native two arguments packed. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

#define STACK_DATA_CAPACITY 64u
#define STACK_ELEMENT_CAPACITY 8u

extern u8 stack_data[64];
extern u8 stack_output[8];
extern u8 stack_has_output;

typedef struct {
    u8 length;
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
    if (stack_has_output) {
        for (i = 0; i < args->element_size; i++) {
            stack_output[i] = stack_data[offset + i];
        }
    }
    args->length--;
    return 1;
}
