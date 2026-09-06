/*
 * main.c — 现代 C 特性演示固件入口。
 *
 * 启动路径：crt-selfstart（HOME 复位 -> 向量表 -> BOOT：设栈顶、
 * 稀疏 XINIT 全局初始化 -> ecall _main）。目标机上 main 不返回
 * （返回会触发 crt 的 'S' 标记，本 demo 把它视为协议违例）。
 *
 * 验收协议：每个特性输出一行 "<特性名>:OK"（或 FAIL 详情），
 * 全部通过输出 "DEMO-PASS"。
 *
 * 期望值（EXPECT_*）的来源：宿主机 gcc 编译并运行同一份源码，
 * 各特性的真实返回值回填至此 —— 目标机输出与 C 语义逐项对拍，
 * 与编译器实现无关（真值回填，非手算）。
 */
#include <stdint.h>
#include <stdbool.h>    /* C99：bool/true/false */
#include "uart.h"
#include "mcs251_features.h"

/* 自检报告：期望与实测都是 u32；失败计数落在可变全局 g_fail_count（BSS） */
static void report(const char *name, uint32_t got, uint32_t expect)
{
    uart_puts(name);
    uart_putc(':');
    if (got == expect) {
        uart_puts("OK\n");
        return;
    }
    uart_puts("FAIL got=");
    uart_hex32(got);
    uart_puts(" expect=");
    uart_hex32(expect);
    uart_putc('\n');
    g_fail_count++;
}

int main(void)
{
    uart_init();  /* HOST/QEMU 为空；真机分支执行串口模板初始化。 */

    /* 特性 1：C99 声明风格（for 内声明、块内声明、// 注释） */
    report("c99_decl", feat_c99_decl(), 0x000001EFu);

    /* 特性 2：stdint 精确宽度 + u32 乘法 + u16 无符号除法 */
    report("stdint_math", feat_stdint_math(), 0xFBDE2881u);

    /* 特性 3：变量移位与循环移位（固定种子保证可复现） */
    report("shift_var", feat_shift_var(0x13572468u), 0x271010AFu);

    /* 特性 4：bool 与短路求值（bump_side 恰好执行一次） */
    report("bool_logic", feat_bool_logic(), 0x000011A5u);

    /* 特性 5：C99 指定初始化器（.field 与 [index]） */
    report("designated_init", feat_designated_init(), 0x000F025Au);

    /* 特性 6：const 查表 + 字符串字面量（CRC8） */
    report("const_table", feat_const_table(), 0x000024C8u);

    /* 特性 7：可变全局（XINIT 初值 / BSS 清零 / 写读回） */
    report("mutable_globals", feat_mutable_globals(), 0x00095208u);

    /* 特性 8：多参数函数（两次独立调用，覆盖静态槽串行化场景） */
    report("multiparam", feat_multiparam(), 0x0002EBC7u);

    /* 特性 9：递归（斐波那契 + 阶乘） */
    report("recursion", feat_recursion(), 0x0038E08Cu);

    /* 特性 10：函数指针表 + 间接调用 */
    report("funcptr", feat_funcptr(1u), 0x000072E4u);

    /* 特性 11：指针算术遍历 */
    report("pointer_walk", feat_pointer_walk(), 0x00007D2Au);

    /* 特性 12：嵌套结构体全局（聚合 XINIT 初值 + 字段修改） */
    report("struct_nest", feat_struct_nest(), 0x00152034u);

    /* 特性 13：C99 inline */
    report("inline_mix", feat_inline(), 0xAD6C85B1u);

    /* 特性 14：C11 _Static_assert 的运行时侧写 */
    report("static_assert", feat_static_assert(), 0x00010204u);

    /* 收尾判定 */
    if (g_fail_count == 0)
        uart_puts("DEMO-PASS\n");
    else {
        uart_puts("DEMO-FAIL(");
        uart_hex8(g_fail_count);
        uart_puts(")\n");
    }

#ifdef HOST_BUILD
    return 0;               /* 宿主机路径：正常退出（期望输出即完整 transcript） */
#else
    for (;;) { }            /* 目标机：固件不返回 */
#endif
}
