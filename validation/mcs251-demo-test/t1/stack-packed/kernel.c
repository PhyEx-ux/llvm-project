/* Extracted T1 kernel: demo-37 stack_push with native two arguments packed. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
#define STACK_CAPACITY 8u

#define STACK_DATA_CAPACITY 64u
#define STACK_ELEMENT_CAPACITY 8u

extern u8 stack_data[64];
extern u8 stack_element[8];

typedef struct {
    u8 length;
    u8 capacity;
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
        stack_data[offset + i] = stack_element[i];
    }
    args->length++;
    return 1;
}
