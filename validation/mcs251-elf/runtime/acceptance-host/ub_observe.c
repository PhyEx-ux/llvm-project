/*
 * ub_observe.c — UB 输入观察固件（除法设计 v4 §8.3 测试三分离之 2/3 档）。
 *
 * 独立固件、独立 QEMU 会话；与合法语义固件 rt_firmware.c 绝不混链。
 *
 * 观察对象与口径（§8.3）：
 *   1. IR 探针（UB 输入观察行）：非法输入经 volatile 供给抵达 IR 层
 *      除法指令（除零 / INT_MIN÷-1），观察形态允许"返回某值 / 终止 /
 *      超时"三态，不预设确定性输出，不与宿主对拍，不建立契约。
 *   2. helper 行为观察（拆出单列）：C 直接调用自研 helper 传非法参数
 *      （_divuint(1,0) 等）——观察的是实现定义行为，不是 IR UB 语义，
 *      档案单独标注，不与 IR 语义混称。
 *
 * 历史输出自比只记录变化，不设兼容门槛（本固件是首次运行，记录基准）。
 */
#include <stdint.h>

#define SBUF (*(volatile uint8_t *)0x99)

static void uart_putc(char c) { SBUF = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static void uart_hex8(uint8_t v)
{
    uint8_t hi = (uint8_t)(v >> 4) & 0x0Fu;
    uint8_t lo = v & 0x0Fu;
    uart_putc((char)(hi < 10 ? '0' + hi : 'A' + hi - 10));
    uart_putc((char)(lo < 10 ? '0' + lo : 'A' + lo - 10));
}
static void uart_hex16(uint16_t v) { uart_hex8((uint8_t)(v >> 8)); uart_hex8((uint8_t)v); }
static void uart_hex32(uint32_t v) { uart_hex16((uint16_t)(v >> 16)); uart_hex16((uint16_t)v); }

/* IR 探针入口（extern 声明在各自的 .ll fixture 中定义；
 * 这里只声明，链接时由探针对象提供——见 ub_probe.ll） */
uint16_t probe_udiv16_ub(uint16_t a, uint16_t b);
int16_t  probe_sdiv16_ub(int16_t a, int16_t b);
uint32_t probe_udiv32_ub(uint32_t a, uint32_t b);
int32_t  probe_sdiv32_ub(int32_t a, int32_t b);

/* 自研 helper 直接调用（helper 行为观察档） */
uint16_t _divuint(uint16_t x, uint16_t y);
uint32_t _divulong(uint32_t x, uint32_t y);
int16_t  _divsint(int16_t x, int16_t y);
int16_t  _modsint(int16_t x, int16_t y);

static volatile uint16_t z16 = 0;
static volatile uint32_t z32 = 0;
static volatile uint16_t m16 = 0x8000;   /* INT16_MIN 位型 */
static volatile int16_t nm16 = -1;
static volatile uint32_t m32 = 0x80000000u;
static volatile int32_t nm32 = -1;
static volatile uint16_t one16 = 1;
static volatile uint32_t one32 = 1;

int main(void)
{
    /* ===== 档 1：IR 探针（UB 输入观察）=====
     * 输出前缀 UBIR-，只记录形态与值。 */
    uart_puts("UBIR-U16-DIV0 q="); uart_hex16(probe_udiv16_ub(one16, z16)); uart_putc('\n');
    uart_puts("UBIR-S16-MINM1 q="); uart_hex16((uint16_t)probe_sdiv16_ub(m16, nm16)); uart_putc('\n');
    uart_puts("UBIR-U32-DIV0 q="); uart_hex32(probe_udiv32_ub(one32, z32)); uart_putc('\n');
    uart_puts("UBIR-S32-MINM1 q="); uart_hex32((uint32_t)probe_sdiv32_ub(m32, nm32)); uart_putc('\n');

    /* ===== 档 2：helper 行为观察（实现定义，非 IR 语义）=====
     * 输出前缀 UBH-，单独标注。 */
    uart_puts("UBH-divuint(1,0)="); uart_hex16(_divuint(one16, z16)); uart_putc('\n');
    uart_puts("UBH-divulong(1,0)="); uart_hex32(_divulong(one32, z32)); uart_putc('\n');
    uart_puts("UBH-divsint(MIN,-1)="); uart_hex16((uint16_t)_divsint((int16_t)m16, nm16)); uart_putc('\n');
    uart_puts("UBH-modsint(MIN,-1)="); uart_hex16((uint16_t)_modsint((int16_t)m16, nm16)); uart_putc('\n');

    uart_puts("UB-OBSERVE-DONE\n");
    for (;;) { }
}
