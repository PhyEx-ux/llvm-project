/* Extracted T1 kernel: demo-32 task processing callback. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned long u32;
#endif

typedef void (*task_hook)(void);
typedef struct {
    u8 run;
    task_hook hook;
} task_item;

typedef struct {
    task_item *tasks;
    u8 count;
} task_dispatch_args;

/* Function-pointer dispatch is preserved; array and count are packed. */
u8 task_dispatch_step(task_dispatch_args *args)
{
    u8 i;
    u8 dispatched = 0;
    for (i = 0; i < args->count; i++) {
        if (args->tasks[i].run) {
            args->tasks[i].run = 0;
            if (args->tasks[i].hook != (task_hook)0) {
                args->tasks[i].hook();
                dispatched++;
            }
        }
    }
    return dispatched;
}
