/*===-- mcs251_putchar.c --------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 libc 子集运行时 —— putchar 提供者（独立 TU，G13a-S2 / PM D4）。
 *
 * 背景（G13A-CODE-DESIGN-draft.md rev-2 §1.3/§3-S2）：printf 运行时按
 * ABI 调用 `extern void putchar(char c)`（见 mcs251_printf.c），putchar
 * 由固件提供；demo 44/82 自带定义，demo 41 未定义——没有提供者时链接
 * 以 `undefined symbol: _putchar` 失败。本 TU 补齐运行时侧提供者：
 *
 *   - 形态与 G13A 探针 putchar-rt.c（11B）一致：接受单个 char、不返回
 *     值、丢弃字符（丢弃型 sink）。语义边界（评审须知）：它只保证
 *     引用 printf 且未自带 putchar 的程序可以闭合链接；串口输出仍由
 *     自带 putchar 的固件 TU 负责。放行账目按此口径，不宣称输出能力。
 *   - 闭包规则：仅当链接输入的未解析集含 _putchar 时才被拉入
 *     （drive.py 按需闭包）。自带 putchar 定义的程序（44/82 类）不会
 *     拉入本对象，因此不存在重复定义面。
 *   - 签名协议：源级签名与 mcs251_printf.c 的 extern 声明同型
 *     （void putchar(char)），Tag 28 记录同型一致；协议与 Tag 28 语义
 *     零改动。
 *
 * 红线（同 mcs251_libc.c）：纯 C；无 64 位类型；无浮点运算符；
 * 无内联汇编；无 pragma；静态全局仅限整数类型。
 */

/* 与 mcs251_printf.c:41 的 extern 声明同型（本运行时的 putchar ABI）。 */
void putchar(char c);

void putchar(char c)
{
    (void)c;    /* 丢弃型 sink：只闭合链接，不做 I/O（见文件头说明） */
}
