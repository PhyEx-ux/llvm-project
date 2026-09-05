/* Extracted T1 kernel: demo-14 row/column keyboard debounce FSM. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

static u8 key_state;
static u8 previous_sample;
static u8 hold_count;
static u8 key_code;
static u8 event_ready;

void keyboard_scan_reset(void)
{
    key_state = 0;
    previous_sample = 0;
    hold_count = 0;
    key_code = 0;
    event_ready = 0;
}

/* raw_rows is the already sampled 8-bit matrix value. GPIO writes/reads and
 * IO_KeyDelay are deliberately outside the extracted algorithm kernel. */
void keyboard_scan_step(u8 raw_rows)
{
    u8 old_state = previous_sample;
    u8 stable_state;
    u8 first_or_repeat = 0;

    previous_sample = raw_rows;
    stable_state = raw_rows;
    if (old_state == stable_state) {
        old_state = key_state;
        key_state = stable_state;
        if (key_state != 0) {
            if (old_state == 0) {
                first_or_repeat = 1;
            } else if (old_state == key_state) {
                hold_count++;
                if (hold_count >= 20) {
                    hold_count = 18;
                    first_or_repeat = 1;
                }
            }
            if (first_or_repeat) {
                key_code = key_state;
                event_ready = 1;
            }
        } else {
            hold_count = 0;
        }
    }
}

u8 keyboard_scan_code(void)
{
    return key_code;
}

u8 keyboard_scan_event(void)
{
    return event_ready;
}
