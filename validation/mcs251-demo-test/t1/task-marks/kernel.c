/* Extracted T1 kernel: demo-32 periodic task mark callback. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

typedef struct {
    u16 time_count;
    u16 retry_time;
    u8 run;
} task_mark;

/* Return the number of tasks marked in this tick as an observable checkpoint. */
u8 task_marks_step(task_mark *tasks, u8 count)
{
    u8 i;
    u8 marked = 0;
    for (i = 0; i < count; i++) {
        if (tasks[i].time_count) {
            tasks[i].time_count--;
            if (tasks[i].time_count == 0) {
                tasks[i].time_count = tasks[i].retry_time;
                tasks[i].run = 1;
                marked++;
            }
        }
    }
    return marked;
}
