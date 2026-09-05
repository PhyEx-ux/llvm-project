/* Extracted T1 kernel: demo-32 periodic task mark callback. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned long u32;
#endif

typedef struct {
    u16 time_count;
    u16 retry_time;
    u8 run;
} task_mark;

typedef struct {
    task_mark *tasks;
    u8 count;
} task_marks_args;

/* Native callback has a task array plus count; T1 packs both into one pointer. */
u8 task_marks_step(task_marks_args *args)
{
    u8 i;
    u8 marked = 0;
    for (i = 0; i < args->count; i++) {
        if (args->tasks[i].time_count) {
            args->tasks[i].time_count--;
            if (args->tasks[i].time_count == 0) {
                args->tasks[i].time_count = args->tasks[i].retry_time;
                args->tasks[i].run = 1;
                marked++;
            }
        }
    }
    return marked;
}
