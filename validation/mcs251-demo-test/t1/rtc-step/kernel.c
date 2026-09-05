/* Extracted T1 kernel: demo-14 RTC body from the main-loop path. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

typedef struct {
    u8 hour;
    u8 minute;
    u8 second;
    u16 millisecond;
} rtc_state;

/* One call represents the one-second event in the main-loop body. */
void rtc_step(rtc_state *clock)
{
    if (++clock->second >= 60u) {
        clock->second = 0;
        if (++clock->minute >= 60u) {
            clock->minute = 0;
            if (++clock->hour >= 24u) clock->hour = 0;
        }
    }
}

u8 rtc_hour(const rtc_state *clock) { return clock->hour; }
u8 rtc_minute(const rtc_state *clock) { return clock->minute; }
u8 rtc_second(const rtc_state *clock) { return clock->second; }
