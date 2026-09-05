/* Extracted T1 kernel: demo-16 NTC table lookup and interpolation. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
#define D_SCALE 10u

/* Exact demo-16 temp_table values, indexes 0..160 (-40..120 C). */
static const u16 temp_table[161] = {
    140,149,159,168,178,188,199,210,222,233,246,259,272,286,301,317,
    333,349,367,385,403,423,443,464,486,509,533,558,583,610,638,667,
    696,727,758,791,824,858,893,929,965,1003,1041,1080,1119,1160,1201,1243,
    1285,1328,1371,1414,1459,1503,1548,1593,1638,1684,1730,1775,1821,1867,1912,1958,
    2003,2048,2093,2137,2182,2225,2269,2312,2354,2397,2438,2479,2519,2559,2598,2637,
    2675,2712,2748,2784,2819,2853,2887,2920,2952,2984,3014,3044,3073,3102,3130,3157,
    3183,3209,3234,3259,3283,3306,3328,3351,3372,3393,3413,3432,3452,3470,3488,3506,
    3523,3539,3555,3571,3586,3601,3615,3628,3642,3655,3667,3679,3691,3702,3714,3724,
    3735,3745,3754,3764,3773,3782,3791,3799,3807,3815,3822,3830,3837,3844,3850,3857,
    3863,3869,3875,3881,3887,3892,3897,3902,3907,3912,3917,3921,3926,3930,3934,3938,
    3942
};

u16 temperature_lookup(u16 adc)
{
    u16 i;
    u8 j;
    u8 k;
    u8 min;
    u8 max;
    u16 value;

    adc = (u16)(4096u - adc);
    if (adc < temp_table[0]) return 0xfffeu;
    if (adc > temp_table[160]) return 0xffffu;
    min = 0;
    max = 160;
    for (j = 0; j < 5; j++) {
        k = (u8)(min / 2u + max / 2u);
        if (adc <= temp_table[k]) max = k;
        else min = k;
    }
    if (adc == temp_table[min]) return (u16)(min * D_SCALE);
    if (adc == temp_table[max]) return (u16)(max * D_SCALE);
    while (min <= max) {
        min++;
        if (adc == temp_table[min]) return (u16)(min * D_SCALE);
        if (adc < temp_table[min]) {
            min--;
            value = temp_table[min];
            value = (u16)((adc - value) * D_SCALE /
                          (temp_table[min + 1] - value));
            i = (u16)(min * D_SCALE);
            return (u16)(i + value);
        }
    }
    return 0xffffu;
}
