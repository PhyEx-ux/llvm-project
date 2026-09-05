/* Extracted T1 kernel: demo-32 task processing callback. */
typedef unsigned char u8;
typedef unsigned short u16;
#ifdef SDCC_FW
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

typedef void (*task_hook)(void);
#define TASK_DISPATCH_CAPACITY 8u
#define TASK_DISPATCH_STRIDE 5u
#define TASK_HOOK_NONE 0u
#define TASK_HOOK_A 1u
#define TASK_HOOK_B 2u

/* Keep the five-byte target record stride, but do not share a function-pointer
 * object between compilers: host, SDCC and LLVM represent that object with
 * different sizes.  One-byte hook IDs are cross-compiler-safe. */
extern u8 dispatch_task_storage[40];
extern u8 dispatch_task_hook_ids[8];
extern u8 dispatch_task_count;
extern void dispatch_hook_a(void);
extern void dispatch_hook_b(void);

static task_hook task_dispatch_resolve(u8 hook_id)
{
    if (hook_id == TASK_HOOK_A) return dispatch_hook_a;
    if (hook_id == TASK_HOOK_B) return dispatch_hook_b;
    return (task_hook)0;
}

/* The callback remains a genuine function-pointer indirect call.  Function
 * addresses are formed inside each compiler's own translation unit instead of
 * crossing the SDCC/LLVM object-representation boundary. */
u8 task_dispatch_step(void)
{
    u8 i;
    u8 offset;
    u8 dispatched = 0;
    task_hook hook;

    for (i = 0; i < dispatch_task_count; i++) {
        offset = (u8)((i << 2) + i);
        if (dispatch_task_storage[offset]) {
            dispatch_task_storage[offset] = 0;
            hook = task_dispatch_resolve(dispatch_task_hook_ids[i]);
            if (hook != (task_hook)0) {
                hook();
                dispatched++;
            }
        }
    }
    return dispatched;
}
