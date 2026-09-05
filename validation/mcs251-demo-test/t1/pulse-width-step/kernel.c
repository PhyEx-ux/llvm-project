/* Extracted T1 kernel: one timer tick of demo-05 pulse-width FSM. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

extern u16 pulse_count;
extern u16 measured_width;
extern u8 input_high;
extern u8 completed;

void pulse_width_reset(void)
{
    pulse_count = 0;
    measured_width = 0;
    input_high = 1;
    completed = 0;
}

/* One call replaces one timer1 ISR invocation. */
void pulse_width_step(u8 pin_high)
{
    input_high = pin_high ? 1 : 0;
    if (!input_high) {
        pulse_count++;
    } else {
        if (pulse_count > 10) {
            measured_width = pulse_count;
            completed = 1;
        }
        pulse_count = 0;
    }
}

u16 pulse_width_result(void)
{
    return measured_width;
}

u8 pulse_width_ready(void)
{
    return completed;
}
