/*
 * ============================================================================
 *  01-selftest.c — MCS251/STC32 自研工具链第一个单文件自检 demo（可分发）
 * ============================================================================
 *
 *  本文件是"零工程"演示：一个 C 文件 + 一段可复制的命令，即可用
 *  Clang + LLVM 把现代 C 编译成 STC32（MCS-251）固件并在 QEMU 上运行。
 *  无 Makefile、无目录结构要求 —— 拿到这个文件就能跑。
 *
 *  覆盖的能力面（每项自检，全部通过输出 SELFTEST-PASS）：
 *    1. 数据模型：int=32/long=32/ptr=32，u8/u16/u32 精确宽度
 *    2. 算术：u32 乘法（原生 MUL）、u16 无符号除法（udiv 库调用）
 *    3. 变量移位：移位次数来自运行时（单位移位循环）
 *    4. 多参数函数：3 参混合宽度（第 1 参寄存器、第 2+ 参静态槽）
 *    5. 可变全局：非零初值（XINIT 启动拷贝）+ BSS 清零 + 写读回
 *    6. const 查表：只读 CSEG（CRC8 查表）+ 字符串字面量
 *    7. 递归：斐波那契（栈帧 + CALLSEQ 调用序列）
 *    8. C99/C11 细节：bool 短路求值、指定初始化器、_Static_assert
 *
 *  覆盖口径：上述是源码特性清单，-O2 可合法折叠固定输入的算术、移位、
 *  多参数调用和查表，SELFTEST-PASS 不等于这些机器指令都在运行时执行。
 *  当前 IR 保留 fastcc 递归、自检调用、全局/XINIT 与串口路径；独立后端
 *  lit/QEMU 差分负责逐项指令覆盖。仅验证下述 -O2 配置，不承诺整体 -O0。
 *
 *  期望值来源：同一份源文件在宿主机 gcc（-DHOST_BUILD）上运行回填，
 *  与编译器实现无关 —— 目标机输出与 C 语言语义逐项对拍。
 *
 * ---------------------------------------------------------------------------
 *  构建与运行（全部在 WSL 内，从仓库根目录 /mnt/c/Prj/LLVM/MCS251 执行；
 *  分发时随附仓库中 crt-selfstart.asm 与 mcs251_ld.py 两个资产即可）
 * ---------------------------------------------------------------------------
 *
 *    # 1) C -> 目标 IR（fork-clang 前端，mcs251 数据模型）
 *    /home/liu/build-clang/bin/clang --target=mcs251-unknown-none \
 *        -std=c11 -O2 -Wall -Wextra -S -emit-llvm \
 *        validation/mcs251-demos/01-selftest.c -o /home/liu/d01.ll
 *
 *    # 2) 目标 IR -> ASxxxx .rel 对象（MCS251 后端）
 *    /home/liu/build-mcs251/bin/llc -mtriple=mcs251-unknown-none \
 *        -verify-machineinstrs -filetype=obj /home/liu/d01.ll -o /home/liu/d01.rel
 *
 *    # 3) 手写启动模块 -> .rel（sdas251 仅用于这一个手写 asm 资产）
 *    /home/liu/build-sdcc/bin/sdas251 -plosgffw -o /home/liu/d01crt.rel \
 *        validation/mcs251-firmware/crt-selfstart.asm
 *
 *    # 4) 链接 -> Intel-HEX（自研链接器，严格 ABI 模式；.lk 由 heredoc 生成）
 *    cat > /home/liu/d01.lk <<'EOF'
 *    -muwx
 *    -i /home/liu/d01
 *    -I 0x0100
 *    -b HOME = 0xff0000
 *    -b VECS = 0xff0003
 *    -b XINIT = 0xfe0000
 *    -b BOOT = 0xff0100
 *    -b CSEG = 0xff0200
 *    -A stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
 *    /home/liu/d01crt.rel
 *    /home/liu/d01.rel
 *    -e
 *    EOF
 *    python3 validation/mcs251-ld/mcs251_ld.py --mcs251-abi -f /home/liu/d01.lk
 *
 *    # 5) QEMU 运行（串口即输出；8 秒后结束）
 *    timeout 8 /home/liu/mcs251-clang/bin-frozen/6b9edfd0/qemu-system-mcs251 \
 *        -M stc32g144k246 -bios /home/liu/d01.hex \
 *        -accel tcg -icount shift=0,align=off,sleep=off \
 *        -display none -monitor none -serial stdio
 *
 *  预期串口输出（全部 OK，末行 SELFTEST-PASS）：
 *    widths:OK
 *    arith:OK
 *    shift:OK
 *    multiparam:OK
 *    globals:OK
 *    const_table:OK
 *    recursion:OK
 *    c99c11:OK
 *    SELFTEST-PASS
 *
 *  宿主机真值再生成（期望值校准用，不需要工具链）：
 *    cc -std=c11 -O2 -Wall -Wextra -DHOST_BUILD 01-selftest.c -o /home/liu/d01host
 *    # 先用 -DPROBE_TRUTH 打印各项真值，回填下方 0xEXPECT_* 后复跑应全 OK
 * ============================================================================
 */

#include <stdint.h>
#ifdef HOST_BUILD
#include <stdio.h>
#endif

/* ---------------- 串口 / 输出 ---------------- */

/*
 * 串口输出有三条路径，编译期选一：
 *
 *  QEMU 模型路径（默认）：stc32g144k246 的 QEMU 机器把 UART1 实现为
 *   test-port 语义 —— 写 SBUF(0x99) 即刻出现在 transcript 上，不检查
 *   SCON 模式、不模拟波特率、不置 TI，因此无需任何初始化或轮询。
 *   （整个测试体系 harness 的 serial oracle 都建立在这个语义上。）
 *
 *  真机路径（-DSTC32_REAL_HW）：烧到 STC32G144K246 真芯片上必须先做
 *   串口初始化，且发送需要 TI 轮询：
 *     1) TX 引脚 GPIO 模式配置（STC32 的 IO 模式寄存器：准双向/推挽）
 *     2) SCON = 0x40   —— 模式 1（8 位可变波特率），暂不开收
 *     3) 波特率发生器  —— 独立波特率 BRT 或 T1，分频值按主频计算
 *     4) TI = 1         —— 模式 1 发送的前置就绪标志
 *   时钟：上电默认内部 IRC 已在跑（不初始化也能工作）；需要标称主频
 *   再配时钟寄存器；访问扩展 SFR 区（EAXFR）要先开 P_SW2。
 *   注意：下方真机参数是按手册推算的典型形态，未经真机实测——烧录前
 *   按你的主频/引脚映射核对填写（TODO 标记处）。
 */
#ifdef STC32_REAL_HW
#define SCON (*(volatile uint8_t *)0x98)
#define SBUF (*(volatile uint8_t *)0x99)
/* 位访问：本后端无 SFR 位域，用读-改-写 */
#define TI_GET()  ((SCON >> 1) & 1u)
#define TI_SET(v) do { SCON = (uint8_t)((SCON & ~0x02u) | ((v) ? 0x02u : 0u)); } while (0)

static void uart_init(void)
{
    /* TODO(真机): TX 引脚 GPIO 模式配置（P3.0/P3.1 或重映射引脚） */
    SCON = 0x40u;            /* 模式 1：8 位可变波特率，REN=0 */
    /* TODO(真机): 波特率发生器 AUXR/BRT 分频值按主频与目标波特率计算
     * （见 STC32G 数据手册"串口波特率"章节） */
    TI_SET(1);               /* 发送就绪起步 */
}
static void putc_(char c)
{
    while (!TI_GET()) { }    /* 等上一字节移出 */
    TI_SET(0);
    SBUF = (uint8_t)c;
}
static void puts_(const char *s) { while (*s) putc_(*s++); }

#elif defined(HOST_BUILD)
static void uart_init(void) { }                 /* 宿主机：无需初始化 */
static void putc_(char c) { putchar(c); }
static void puts_(const char *s) { fputs(s, stdout); }

#else
/* QEMU 模型路径（默认）：test-port 语义，无需初始化 */
static void uart_init(void) { }
#define SBUF (*(volatile uint8_t *)0x99)
static void putc_(char c) { SBUF = (uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
#endif

static void hex32(uint32_t v)          /* 大写十六进制，固定 8 位 */
{
    const char *d = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)    /* 常量循环 + 常量移位 */
        putc_(d[(v >> (unsigned)i) & 0xFu]);
}

/* ---------------- C11 编译期断言（数据模型自描述） ---------------- */
_Static_assert(sizeof(uint8_t) == 1, "u8 必须是 1 字节");
_Static_assert(sizeof(uint16_t) == 2, "u16 必须是 2 字节");
_Static_assert(sizeof(uint32_t) == 4, "u32 必须是 4 字节");
_Static_assert(sizeof(int) == 4, "本目标 int=32（数据模型裁定）");
#ifndef HOST_BUILD
/* long 宽度仅目标机断言：宿主机 LP64 为 8 字节，属实现定义差异 */
_Static_assert(sizeof(long) == 4, "目标机 long=32（数据模型裁定）");
#endif

/* ---------------- 可变全局（XINIT / BSS 能力展示） ---------------- */
uint32_t g_state = 0xAABBCCDDu;                       /* 非零初值：XINIT 拷贝 */
uint16_t g_table[4] = { 0x0102u, 0x0304u, 0x0506u, 0x0708u };
uint8_t  g_bss[32];                                   /* 零初始化 BSS：启动清零 */
uint8_t  g_fail;                                      /* 失败计数（BSS） */

/* ---------------- const 全局（只读 CSEG 能力展示） ---------------- */
static const uint8_t crc8_tab[16] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D
};
static const char banner[] = "MCS251 single-file selftest";   /* 字符串字面量 */

/* ---------------- 特性 1：数据模型与精确宽度 ---------------- */
static uint32_t feat_widths(void)
{
    /* 宽度打包：u8<<16 | u16<<8 | u32 = 0x010204；int 两侧均为 32 位可对拍。
     * （long 除外：宿主机 LP64 为 8 字节、目标机为 4 —— 实现定义差异，
     *   不进双机对拍集，由目标机侧 _Static_assert 单独钉住。） */
    uint32_t w = ((uint32_t)sizeof(uint8_t) << 16)
               | ((uint32_t)sizeof(uint16_t) << 8)
               | (uint32_t)sizeof(uint32_t);
    if (sizeof(int) != sizeof(uint32_t))
        w |= 0x80000000u;               /* int 非 32 位则置错位 */
    return w;
}

/* ---------------- 特性 2：u32 乘法 + u16 无符号除法 ---------------- */
static uint32_t feat_arith(void)
{
    uint32_t m = 123456789u * 987654321u;      /* 原生 MUL，模 2^32 语义 */
    uint16_t q = 60000u / 7u;                  /* udiv 库调用 */
    uint8_t  wrap = (uint8_t)(250u + 10u);     /* u8 回绕：260 -> 4 */
    return m ^ ((uint32_t)q << 8) ^ (uint32_t)wrap;
}

/* ---------------- 特性 3：变量移位（运行时次数） ---------------- */
static uint32_t feat_shift(uint32_t seed)
{
    uint8_t n = seed & 15u;                    /* 运行时决定移位量 */
    uint32_t a = seed << n;                    /* 变量左移 */
    uint32_t b = seed >> (15u - n);            /* 变量右移（次数也随运行时） */
    return a ^ b;
}

/* ---------------- 特性 4：多参数函数（静态参数槽） ---------------- */
static uint16_t inner_mix(uint8_t a, uint16_t b)        /* 嵌套 2 参（叶子） */
{
    return (uint16_t)((uint16_t)(a * 5u) + (uint16_t)(b / 3u));
}
static uint32_t triple(uint8_t a, uint16_t b, uint32_t c)   /* 非叶 3 参 */
{
    return c + (uint32_t)inner_mix(a, b) * 100u;
}
static uint32_t feat_multiparam(void)
{
    /* 同一表达式两次独立多参调用：覆盖静态槽的调用间串行化 */
    return triple(6u, 900u, 100000u) ^ triple(4u, 300u, 200000u);
}

/* ---------------- 特性 5：可变全局（初值/清零/写读回） ---------------- */
static uint32_t feat_globals(void)
{
    uint32_t bad = 0;
    if (g_state != 0xAABBCCDDu) bad |= 1u;             /* XINIT 初值就位 */
    for (uint8_t i = 0; i < 32; i++)
        if (g_bss[i] != 0u) bad |= 2u;                 /* BSS 已清零 */
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 4; i++)
        sum += g_table[i];                             /* 初值数组求和 */
    g_state = 0x13572468u;                             /* 写 */
    if (g_state != 0x13572468u) bad |= 4u;             /* 读回 */
    g_bss[7] = 0x99u;
    if (g_bss[7] != 0x99u) bad |= 8u;
    g_state = 0xAABBCCDDu;                             /* 恢复 */
    return bad ? bad : (sum + 0x40000u);
}

/* ---------------- 特性 6：const 查表 + 字符串（只读 CSEG） ---------------- */
static uint32_t feat_const_table(void)
{
    uint8_t crc = 0xFFu;
    for (uint8_t i = 0; banner[i] != '\0'; i++) {
        uint8_t idx = (uint8_t)((crc ^ (uint8_t)banner[i]) & 0x0Fu);
        crc = (uint8_t)((crc >> 4) ^ crc8_tab[idx]);
    }
    return ((uint32_t)crc << 8) | 0x5Au;               /* 混入标记位 */
}

/* ---------------- 特性 7：递归 ---------------- */
static uint32_t fib(uint32_t n)
{
    return n < 2u ? n : fib(n - 1u) + fib(n - 2u);
}

/* ---------------- 特性 8：C99/C11 细节 ---------------- */
struct tweak {                                   /* 指定初始化器（C99） */
    uint16_t id;
    uint8_t  enable;
    uint32_t gain;
};
static const struct tweak cfg = { .gain = 0x00010000u, .id = 0x0B0Cu, .enable = 1u };
uint8_t g_probe_hits;                            /* 短路验证计数器（BSS） */
static int probe(void) { g_probe_hits++; return 1; }
static uint32_t feat_c99c11(void)
{
    _Bool t = 1, f = 0;                          /* C99 _Bool */
    g_probe_hits = 0;
    /* f&&probe() 短路不执行；t&&probe() 执行一次成真；第三个 || 被短路 */
    if ((f && probe()) || (t && probe()) || probe())
        g_probe_hits += 0x10u;
    uint32_t c = (uint32_t)cfg.id | ((uint32_t)cfg.enable << 24) | cfg.gain;
    return (c << 8) | ((uint32_t)g_probe_hits << 4) | 0x5u;
}

/* ---------------- 自检与入口 ---------------- */
static void check(const char *name, uint32_t got, uint32_t expect)
{
    puts_(name);
    putc_(':');
    if (got == expect) {
        puts_("OK\n");
        return;
    }
    puts_("FAIL got=");
    hex32(got);
    puts_(" expect=");
    hex32(expect);
    putc_('\n');
    g_fail++;
}

#ifdef PROBE_TRUTH
/* 宿主机真值打印（回填期望值用；编译加 -DPROBE_TRUTH） */
int main(void)
{
    printf("widths %08X\n", feat_widths());
    printf("arith %08X\n", feat_arith());
    printf("shift %08X\n", feat_shift(0x2468ACE0u));
    printf("multiparam %08X\n", feat_multiparam());
    printf("globals %08X\n", feat_globals());
    printf("const_table %08X\n", feat_const_table());
    printf("recursion %08X\n", fib(16u));
    printf("c99c11 %08X\n", feat_c99c11());
    return 0;
}
#else
int main(void)
{
    uart_init();                                 /* 真机：串口初始化；其余路径为空 */

    check("widths",       feat_widths(),       0x00010204u);
    check("arith",        feat_arith(),        0xFBDE2881u);
    check("shift",        feat_shift(0x2468ACE0u), 0x2468E431u);
    check("multiparam",   feat_multiparam(),   0x00013BA8u);
    check("globals",      feat_globals(),      0x00041014u);
    check("const_table",  feat_const_table(),  0x0000275Au);
    check("recursion",    fib(16u),            0x000003DBu);
    check("c99c11",       feat_c99c11(),       0x010B0D15u);

    if (g_fail == 0)
        puts_("SELFTEST-PASS\n");
    else {
        puts_("SELFTEST-FAIL(");
        putc_((char)('0' + g_fail));
        puts_(")\n");
    }

#ifdef HOST_BUILD
    return 0;                                   /* 宿主机：正常退出 */
#else
    for (;;) { }                                /* 目标机：固件不返回 */
#endif
}
#endif
