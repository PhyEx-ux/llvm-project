/*===-- mcs251_printf_internal.h -------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS251 libc 子集 —— printf/sprintf 家族私有跨 TU 接口（G13a S3）。
 *
 * 背景（G13A-CODE-DESIGN-draft.md rev-2 §3-c，Alice APPROVED）：
 *   _out_float 函数字节 4469B（-O2 口径）是 demo 41 CODE 窗口预算的大头
 *   之一；mcs251_printf.c 原先将 out_float 实现为 TU 内 static 函数，
 *   格式引擎（%f/%F、%g/%G、%e/%E 分支）直接调用——只拆 TU 会保留
 *   UND、闭包仍拉入浮点对象，不拉则链接失败。
 *
 * S3 拆分形态（本头 + 两个 TU）：
 *   - mcs251_printf.c        保留格式引擎/printf/sprintf 与字符输出原语
 *                            out_char/out_str（原 static，S3 起外部链接，
 *                            本头声明；对象符号 _out_char/_out_str）。
 *   - mcs251_printf_float.c  out_float 完整实现**原样**迁入（函数体逐字
 *                            保持；对象符号 _out_float，第二参数经
 *                            _out_float_PARM_2 静态槽，emitParameterSlots
 *                            既定机制，跨 TU 与 putchar 同构）。
 *   - 闭包规则：程序引用 printf/sprintf → 拉入 printf.o；printf.o 的
 *     UND 含 _out_float → 默认再拉入 printf_float.o（完整浮点格式化
 *     可用，默认形态）。
 *
 * 无浮点引擎（MCS251_PRINTF_NO_FLOAT）判定规则（设计稿 §3-c 冻结表述，
 * 保守取向，违反任何一条都必须保留完整实现）：
 *   1. 仅当**可证明**程序无浮点格式时才以 -DMCS251_PRINTF_NO_FLOAT
 *      编译裁剪形态（配方开关，非默认）；裁剪形态下 %f/%F/%g/%G/%e/%E
 *      落入未知格式符分支（原样输出 '%' + 格式符；该分支不消费实参，
 *      实参索引不前进），且对象不含 UND _out_float、无需 printf_float.o。
 *      违反无浮点前提（实际出现浮点转换）时**不提供降级安全保证**：
 *      后续转换会继续读取该浮点实参槽——参数索引失配、错读槽位乃至
 *      非法访存（宿主探针实测 `%f %s`：`%s` 解引用首槽浮点位模式即
 *      SIGSEGV）。
 *   2. 判定必须覆盖**全部浮点转换符**（f/F/g/G/e/E），含任意
 *      flags/宽度/精度/长度修饰组合——`%.2f` 与 `%f` 同为浮点格式，
 *      不许只扫裸 `%f`。
 *   3. **非字面量格式串**（运行期拼串、经变量/参数传入的 fmt、或任何
 *      无法静态还原为单一字符串字面量的调用点）：一律视为无法证明，
 *      保留完整实现。
 *   4. 链接期格式串校验兜底（对最终对象内 fmt 字面量复核）为后续
 *      独立切片；本头只冻结源级规则，不宣称链接期兜底已存在。
 *   5. 默认（未定义宏）永远是完整实现；裁剪是可选配方，不是优化
 *      默认值。扫描器（build 侧）判定见 GAP-G13A-PROBES/s3/。
 *
 * 红线（继承 mcs251_printf.c）：纯 C；无 64 位；无浮点类型（out_float
 * 以位模式 u32 传入）；无内联汇编；无 pragma。
 */

#ifndef MCS251_PRINTF_INTERNAL_H
#define MCS251_PRINTF_INTERNAL_H

#include "mcs251_libc.h"

/* 内部输出一个字符（printf/sprintf 双模式，定义于 mcs251_printf.c）。
 * 原 TU 内 static（fastcc 内部链接），G13a-S3 起跨 TU 外部链接供
 * mcs251_printf_float.c 复用；noinline 保持（防全局变量读取被内联到
 * 调用方后产生后端无法 select 的 load-from-global DAG 节点）。 */
__attribute__((noinline))
void out_char(char c);

/* 内部输出 NUL 结尾字符串（定义于 mcs251_printf.c，经 out_char）。 */
__attribute__((noinline))
void out_str(const char* s);

/* 输出 f32 浮点（bit pattern 传入；fs = 0x80000000|frac_digits（%g 带
 * strip 位）或 frac_digits（%f/%e），见 mcs251_printf.c 文件头打包规则）。
 * 定义于 mcs251_printf_float.c（G13a-S3 自 mcs251_printf.c 原样迁入，
 * 函数体逐字保持）。裁剪形态（-DMCS251_PRINTF_NO_FLOAT 编译
 * mcs251_printf.c）不引用本符号。 */
void out_float(uint32_t bits, uint32_t fs);

#endif /* MCS251_PRINTF_INTERNAL_H */
