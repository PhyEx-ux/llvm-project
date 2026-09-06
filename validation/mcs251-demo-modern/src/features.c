/*
 * features.c — 现代 C 特性展示函数的实现。
 *
 * 每个函数覆盖一个特性族，返回值交给 main() 比对。所有计算保持
 * 在 u8/u16/u32 域内（MCS-251 的 int=32 数据模型），乘法/无符号除法/
 * 变量移位等均为后端已实测支持的能力。
 */
#include "mcs251_features.h"

/* ===================== 可变全局（XINIT / BSS 能力） ===================== */

/* 非零初值全局：ROM 中的 XINIT 镜像在启动时由 __mcs251_globals_init 拷贝到 DSEG */
uint32_t g_counter = 0x11223344u;
uint16_t g_hist[6] = { 1000u, 2000u, 3000u, 4000u, 5000u, 6000u };

/* 零初始化 BSS：稀疏 XINIT 协议下只占 6 字节 ROM 元数据，启动时清零 */
uint8_t  g_zero_buf[64];

/* 统计失败数的全局（隐式零初始化，同样走 BSS 清零路径） */
uint8_t   g_fail_count;

/* ===================== const 全局（rodata CSEG 能力） ===================== */

/* CRC8 查表：const 全局数组进只读 CSEG，不占 RAM */
static const uint8_t crc8_table[16] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D
};

/* const 字符串字面量：同样落在只读 CSEG */
static const char banner[] = "MCS251 modern-C demo";

/* ===================== C11 编译期断言（本文件顶部即断言本体） ===================== */
_Static_assert(sizeof(uint8_t) == 1, "u8 必须是 1 字节");
_Static_assert(sizeof(uint16_t) == 2, "u16 必须是 2 字节");
_Static_assert(sizeof(uint32_t) == 4, "u32 必须是 4 字节");
_Static_assert(sizeof(int) >= 4, "本目标 int 至少 32 位（数据模型裁定）");

/* ===================== 特性实现 ===================== */

/* C99：声明可以出现在任意块内、for 的初始化子句里；// 行注释 */
uint32_t feat_c99_decl(void)
{
    uint32_t acc = 0;
    for (uint8_t i = 1; i <= 8; i++) {              /* for 内声明（C99） */
        uint32_t sq = (uint32_t)i * (uint32_t)i;    /* 块内声明（C99） */
        acc += sq;                                  // 1²+2²+…+8² = 204
    }
    uint16_t tail = 0x1234u >> 4;                   /* 函数后段继续声明（C89 不允许） */
    return acc + (uint32_t)tail;                    /* 204 + 0x0123 */
}

/* stdint 精确宽度 + 乘法（原生 MUL）+ 无符号除法（udiv libcall） */
uint32_t feat_stdint_math(void)
{
    uint32_t big = 123456789u * 987654321u;         /* u32 乘法，模 2^32 截断语义 */
    uint16_t q = 60000u / 7u;                       /* u16 无符号除法：8571 */
    uint8_t  wrap = (uint8_t)(250u + 10u);                     /* u8 回绕：260 -> 4 */
    return big ^ ((uint32_t)q << 8) ^ (uint32_t)wrap;
}

/* 变量移位 + 循环移位：移位次数是运行时值（单位移位循环 lowering） */
static uint32_t rol32(uint32_t x, uint8_t n)        /* 左循环移位（模宽语义） */
{
    n &= 31u;                                       /* rotate 的次数按模宽规约 */
    return (x << n) | (x >> ((32u - n) & 31u));     /* n==0 时两侧都移 0，安全 */
}
uint32_t feat_shift_var(uint32_t seed)
{
    uint8_t n1 = seed & 7u;                         /* 运行时决定移位量 */
    uint8_t n2 = (seed >> 8) & 15u;
    uint32_t a = seed << n1;                        /* 变量左移 */
    uint32_t b = seed >> n2;                        /* 变量逻辑右移 */
    uint32_t r = rol32(seed, (uint8_t)(n1 + n2));   /* rotate（展开为双移位+或） */
    return a ^ b ^ r;
}

/* bool 与短路求值：++g_side_hits 只允许在按 C 语义到达的分支里执行 */
uint8_t g_side_hits;                                /* 短路验证的副作用计数器（BSS） */
static int bump_side(void) { g_side_hits++; return 1; }
uint32_t feat_bool_logic(void)
{
    _Bool t = 1, f = 0;                             /* C99 _Bool（stdbool 之前的原生拼写） */
    g_side_hits = 0;
    /* f 为假：&& 短路不执行 bump_side；t && bump_side() 执行一次并使整个
       表达式为真，第三个 || 分支被短路 —— bump_side 总共恰好执行 1 次 */
    if ((f && bump_side()) || (t && bump_side()) || bump_side())
        g_side_hits += 0x10u;
    return ((uint32_t)g_side_hits << 8) | ((t && !f) ? 0xA5u : 0x5Au);
}

/* C99 指定初始化器：.field 与 [index] 两种形态 */
struct sensor_cfg {
    uint16_t id;
    uint8_t  flags;
    uint32_t scale;
};
static const struct sensor_cfg cfg = { .scale = 0x00010000u, .id = 0x0102u, .flags = 0x0Fu };
static const uint8_t sparse_map[6] = { [2] = 7u, [4] = 9u };  /* 其余元素为 0 */
uint32_t feat_designated_init(void)
{
    uint32_t acc = (uint32_t)cfg.id | ((uint32_t)cfg.flags << 16) | cfg.scale;
    for (uint8_t i = 0; i < 6; i++)                 /* 求和验证未列出的元素确实为 0 */
        acc += (uint32_t)sparse_map[i] << (i + 1);
    return acc;
}

/* const 查表 + 字符串字面量：对 banner 做 4 位 CRC8（查表法） */
uint32_t feat_const_table(void)
{
    uint8_t crc = 0xFFu;
    for (uint8_t i = 0; banner[i] != '\0'; i++) {
        uint8_t idx = (uint8_t)((crc ^ (uint8_t)banner[i]) & 0x0Fu);
        crc = (uint8_t)((crc >> 4) ^ crc8_table[idx]);   /* 查表 + 异或 */
    }
    return ((uint32_t)crc << 8) | 0xC8u;            /* 混入固定标记位便于比对 */
}

/* 可变全局：初值正确（XINIT 拷贝）、BSS 清零、写后读回 */
uint32_t feat_mutable_globals(void)
{
    uint32_t bad = 0;
    if (g_counter != 0x11223344u) bad |= 1u;        /* XINIT 初值必须就位 */
    for (uint8_t i = 0; i < 64; i++)
        if (g_zero_buf[i] != 0u) bad |= 2u;         /* BSS 必须已清零 */
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 6; i++)
        sum += g_hist[i];                           /* 初值数组求和 = 21000 */
    g_counter = 0xDEADBEEFu;                        /* 写 */
    if (g_counter != 0xDEADBEEFu) bad |= 4u;        /* 读回 */
    g_zero_buf[3] = 0x5Au;
    if (g_zero_buf[3] != 0x5Au) bad |= 8u;
    g_counter = 0x11223344u;                        /* 恢复，供后续读取 */
    return bad ? bad : (sum + 0x90000u);
}

/* 多参数函数：4 参混合宽度（第 1 参走寄存器，第 2+ 参走静态槽） */
static uint16_t pair_mix(uint8_t a, uint16_t b)     /* 2 参（嵌套层） */
{
    return (uint16_t)((uint16_t)(a * 3u) + (uint16_t)(b / 5u));
}
static uint32_t feat_quad(uint8_t a, uint16_t b, uint32_t c, uint8_t d)
{
    uint16_t sub = pair_mix(a, b);                  /* 非叶：内部再调多参函数 */
    return c + (uint32_t)sub * 100u + (uint32_t)d;
}
uint32_t feat_multiparam(void)
{
    /* 同一表达式里两次独立多参调用（覆盖静态槽串行化的 CALLSEQ 修复场景） */
    uint32_t x = feat_quad(7u, 500u, 100000u, 3u);
    uint32_t y = feat_quad(9u, 900u, 200000u, 4u);
    return x ^ y;
}

/* 递归：斐波那契与阶乘（u32 域内） */
static uint32_t fib(uint32_t n) { return n < 2u ? n : fib(n - 1u) + fib(n - 2u); }
static uint32_t fact(uint32_t n) { return n <= 1u ? 1u : n * fact(n - 1u); }
uint32_t feat_recursion(void)
{
    return fib(16u) * 100u + fact(10u);             /* 987*100 + 3628800 */
}

/* 函数指针表 + 间接调用：表在运行时构造（栈上），单参签名 */
static uint32_t op_double(uint32_t x) { return x * 2u; }
static uint32_t op_square(uint32_t x) { return x * x; }
static uint32_t op_rev16(uint32_t x) { return ((x & 0xFFu) << 8) | ((x >> 8) & 0xFFu); }
uint32_t feat_funcptr(uint8_t sel)
{
    uint32_t (*ops[3])(uint32_t);                   /* 局部函数指针表 */
    ops[0] = op_double;
    ops[1] = op_square;
    ops[2] = op_rev16;
    uint32_t x = 0x00ABu;
    for (uint8_t i = 0; i < 3; i++)
        x = ops[(sel + i) % 3u](x);                 /* 依次经三个函数接力 */
    return x;
}

/* 指针算术：遍历数组求和取最大 */
uint32_t feat_pointer_walk(void)
{
    static const uint16_t data[8] = { 3u, 11u, 7u, 42u, 19u, 5u, 30u, 8u };
    const uint16_t *p = data;
    const uint16_t *end = data + 8;                 /* 指针 + 整数 */
    uint32_t sum = 0, max = 0;
    while (p != end) {                              /* 指针比较 */
        sum += *p;
        if (*p > max) max = *p;
        p++;                                        /* 指针自增（@DRk 寻址） */
    }
    return (sum << 8) | max;                        /* 125<<8 | 42 */
}

/* 嵌套结构体全局（聚合非零初值，走 XINIT）+ 字段修改 */
struct point { uint16_t x, y; };
struct track {
    struct point origin;                            /* 嵌套结构 */
    uint8_t  steps;
    uint32_t dist;
};
struct track g_track = { { 0x0010u, 0x0020u }, 12u, 0x00003000u };
uint32_t feat_struct_nest(void)
{
    g_track.origin.x += 5u;                         /* 修改嵌套字段 */
    g_track.dist += (uint32_t)g_track.steps * 100u;
    return ((uint32_t)g_track.origin.x << 16)
         | ((uint32_t)g_track.origin.y << 8)
         | (g_track.dist >> 8);                     /* 打包展示 */
}

/* C99 inline：同翻译单元定义，编译器可内联 */
static inline uint32_t mix32(uint32_t x)
{
    return (x ^ (x >> 16)) * 0x9E37u;               /* 乘一个类似黄金比的奇常数 */
}
uint32_t feat_inline(void)
{
    return mix32(0x13572468u) ^ mix32(0x2468ACE0u);
}

/* C11 编译期断言的运行时侧写：断言本体在文件顶部，这里返回宽度打包 */
uint32_t feat_static_assert(void)
{
    return ((uint32_t)sizeof(uint8_t) << 16)
         | ((uint32_t)sizeof(uint16_t) << 8)
         | (uint32_t)sizeof(uint32_t);              /* 0x010204 */
}
