/* Extracted T1 kernel: demo-32 task processing callback. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

typedef void (*task_hook)(void);
typedef struct {
    u8 run;
    task_hook hook;
} task_item;

/* Function-pointer dispatch is preserved; return count makes execution observable. */
u8 task_dispatch_step(task_item *tasks, u8 count)
{
    u8 i;
    u8 dispatched = 0;
    for (i = 0; i < count; i++) {
        if (tasks[i].run) {
            tasks[i].run = 0;
            if (tasks[i].hook != (task_hook)0) {
                tasks[i].hook();
                dispatched++;
            }
        }
    }
    return dispatched;
}
