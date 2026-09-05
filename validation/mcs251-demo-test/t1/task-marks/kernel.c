/* Extracted T1 kernel: demo-32 periodic task mark callback. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

typedef struct {
    u16 time_count;
    u16 retry_time;
    u8 run;
} task_mark;

#define TASK_MARKS_CAPACITY 8u

#ifdef SDCC_FW
#define MCS251_DATA __data
#else
#define MCS251_DATA
#endif
extern MCS251_DATA task_mark mark_tasks[8];
extern u8 mark_task_count;

/* Native callback has a task array plus count. The extracted table/count are
 * driver-owned globals and are traversed in ascending order. */
u8 task_marks_step(void)
{
    u8 i;
    u8 marked = 0;
    MCS251_DATA task_mark *task = mark_tasks;
    for (i = 0; i < mark_task_count; i++, task++) {
        if (task->time_count) {
            task->time_count--;
            if (task->time_count == 0) {
                task->time_count = task->retry_time;
                task->run = 1;
                marked++;
            }
        }
    }
    return marked;
}

