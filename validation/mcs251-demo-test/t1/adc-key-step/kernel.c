/* Extracted T1 kernel: demo-15 ADC-key classification/debounce FSM. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

#define ADC_OFFSET 64u
static u8 adc_key_state;
static u8 adc_key_state1;
static u8 adc_key_state2;
static u8 adc_key_state3;
static u8 adc_key_hold_count;
static u8 adc_key_code;
static u8 adc_key_event;

void adc_key_reset(void)
{
    adc_key_state = 0;
    adc_key_state1 = 0;
    adc_key_state2 = 0;
    adc_key_state3 = 0;
    adc_key_hold_count = 0;
    adc_key_code = 0;
    adc_key_event = 0;
}

/* ADC conversion/SFR access is removed; adc is the sampled 12-bit value. */
void adc_key_step(u16 adc)
{
    u8 i;
    u16 j;
    if (adc < (u16)(256u - ADC_OFFSET)) {
        adc_key_state = 0;
        adc_key_hold_count = 0;
    }
    j = 256;
    for (i = 1; i <= 16; i++) {
        if ((adc >= (u16)(j - ADC_OFFSET)) &&
            (adc <= (u16)(j + ADC_OFFSET))) {
            break;
        }
        j += 256;
    }
    adc_key_state3 = adc_key_state2;
    adc_key_state2 = adc_key_state1;
    if (i > 16) {
        adc_key_state1 = 0;
    } else {
        adc_key_state1 = i;
        if ((adc_key_state3 == adc_key_state2) &&
            (adc_key_state2 == adc_key_state1) &&
            (adc_key_state3 > 0) && (adc_key_state2 > 0) &&
            (adc_key_state1 > 0)) {
            if (adc_key_state == 0) {
                adc_key_code = i;
                adc_key_state = i;
                adc_key_hold_count = 0;
                adc_key_event = 1;
            }
            if (adc_key_state == i) {
                adc_key_hold_count++;
                if (adc_key_hold_count >= 100) {
                    adc_key_hold_count = 90;
                    adc_key_code = i;
                    adc_key_event = 1;
                }
            } else {
                adc_key_hold_count = 0;
            }
        }
    }
}

u8 adc_key_code_result(void)
{
    return adc_key_code;
}

u8 adc_key_event_result(void)
{
    return adc_key_event;
}
