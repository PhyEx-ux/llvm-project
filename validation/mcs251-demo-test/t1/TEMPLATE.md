# T1 用例模板与目录约定（TEMPLATE）

状态：v1（2026-09-05，Shizuku）。这是 batch 提取（kernel.c + EXTRACT.md）到
端到端可判定用例的机械化套用说明。runner（../run-tests.py）只认文件约定，
不看人：放齐文件即入链，缺件即 pending。

## 标准件（每个 t1/<name>/ 目录）

| 文件 | 必需 | 职责 | 谁写 |
| --- | --- | --- | --- |
| kernel.c | 是 | 被测内核，三方同源（gcc/SDCC/clang 共用同一份） | Momo（batch）或用例作者 |
| EXTRACT.md | 是 | 提取出处（demo 路径+行号）、逐条改写说明、已知限制 | 同上 |
| wrapper.c | 判定需要 | 固件驱动（SDCC --c1mode 编；Oracle-B 与 DUT 链共用，仅 kernel .rel 不同） | 实例化者 |
| host-main.c | 行为判定需要 | Oracle-A host 驱动（gcc 编）。**缺此文件 = smoke 用例**（只做编译+终止判定，无行为 oracle） | 实例化者 |
| vectors.h / gen-vectors.py | 可选 | 共享测试激励（两个驱动 include 同一份）与再生成脚本 | 实例化者 |

目录里只有 kernel.c + EXTRACT.md 时 runner 报 `pending-instantiation`（不算
失败）——batch 提取可先落盘，build 侧后续随时套模板。

## kernel.c 写法约束（现代形态，PM 裁定 2026-09-05）

1. 自包含：文件头自带显式宽度 typedef，不 include 任何系统头：
   ```c
   typedef unsigned char  u8;
   typedef unsigned short u16;   /* NOT unsigned int: SDCC int=16 but host
                                    clang int=32 (user ruling "int=32");
                                    `unsigned int` makes clang declare the
                                    extern as i32 and store 4 bytes into
                                    SDCC's 2-byte global (observed: stomps
                                    neighbouring variables) */
   typedef unsigned long  u32;
   ```
   宽度是硬规则：demo 原文的 `unsigned int` 一律折算为 `unsigned short`。
2. 零全局定义：可变全局状态一律 `extern` 声明，定义放 wrapper.c（固件侧）
   与 host-main.c（host 侧）。原因：MCS251 后端拒绝 defined global data
   （"Phase 12 Step 2 pending"，llc 报错原文）。三方零初始化语义一致。
3. 避开已实测的编译器边界（每条都有 probe 支撑，见 ../RESULTS.md）：
   - 字符串字面量初始化的数组在 SDCC --c1mode 下会全 0（数据丢失）；
     向量一律显式逐元素初始化。
   - 未限定指针形参/局部指针遍历会触发 SDCC __gptrget（strict 链无 libc）；
     用数组下标遍历全局缓冲（C 语义 p[i]==*(p+i)，形态等价）。
   - char 形参在 x86 前端 signext，撞后端"无 8→16 符号扩展"；demo 的 char
     分类参数改 u8（对全部分支结构逐字论证过等价，见各 EXTRACT.md）。
   - switch 会被 -O1 合成/保持为跳转表（br_jt 无 ISel）且 -O0 也直接
     lower IR switch：稀疏 switch 改写为 if/else 链（控制流同构）。
   - 跨编译器 u16 全局读（SDCC 写→llc 读）有 load i16 错码 bug（0x1234
     读成 0x1212）：16 位输入拆双 u8，等价改写记录进 EXTRACT.md。
     u8 双向、store i16 方向已验证可靠。
4. 移位算子照常写（demo 原形保留）——runner 的临时 lowering 会展开成
   and/or/select 链（见下"垫片"）。
5. C90 风格声明置顶；多参函数按 DESIGN.md §2-T1 记 T3，不进 T1 首批
   （参数打包降维路线除外，EXTRACT.md 标注"原生多参"）。

## wrapper.c 骨架

```c
typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;
/* kernel 状态在此定义（kernel.c 只 extern）： */
u8 g_input; u8 g_out[8];
extern u8 kernel_step(u8 x);          /* 与 kernel.c 签名逐字一致（strict ABI） */

#define MCS251_CHECKPOINTS() do { u8 v; \
    g_input = 0x11; \
    v = kernel_step(g_input); UART_PUTC('a'); harness_hex8(v); \
    harness_check_u8(0xAB, v);        /* 期望值冻结自 Oracle-A 真值 */ \
} while (0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
```

规则：
- checkpoint 形态固定：`UART_PUTC(tag); harness_hex8/16/32(v);
  harness_check_uX(expected, v);`——先打实际值（供三方 diff）再判期望。
- 期望值流程：先写 host-main.c 跑 Oracle-A 拿真值 → 回填 wrapper →
  三角 diff 兜底。绝不允许"看着 DUT 输出填期望"。
- ROM 表用显式初始化的 `static const`（进 CONST 区，runner 已把 CONST
  固定到 0xFC8000——QEMU 对 0xFFxxxx 窗口数据读返回 0，probe 见 RESULTS）。
- 大数组激励（如 NEC 样本流）放 vectors.h 由两驱动共享。

## host-main.c 骨架

```c
#include <stdio.h>
#include "kernel.c"            /* 先 include（typedef 可用）再定义状态 */
u8 g_input; u8 g_out[8];       /* 与 wrapper 侧同名同型 */

int main(void) {
    putchar('B');              /* 与固件 'B' 进入标记对齐 */
    /* 同 wrapper 的输入序列；打印同格式 checkpoint： */
    printf("a%02X", v);
    puts("PASS");
    return 0;
}
```

串口三方比对格式：`'B' + <tag><HEX>* + "PASS\n"` 逐字符相等。

## 判定（runner 自动）

- full 用例：serial(Oracle-A) == serial(Oracle-B) == serial(DUT) 且 DUT
  含完整 PASS 无 FAIL。QEMU timeout rc=124 属预期（harness 死循环设计）。
- smoke 用例：DUT 到达 PASS 即通过（编译+终止）。

## 可退役垫片（PM 裁定 2026-09-05：不成长期为隐式行为）

runner 内两个 IR 兼容垫片，默认开启，`--no-ir-shims` 可整体关闭：

| 垫片 | 作用 | 退役条件 |
| --- | --- | --- |
| 符号适配 @name→@_name | SDCC 的 C 符号 `_` 前缀约定 vs apt-clang 裸名 | fork-clang 前端原生出 `_` 前缀符号（前端专项裁定） |
| 常量移位 lowering | lshr/shl/ashr 展开 and/or/select 链（含 volatile alloca 屏障防 DAG combiner 折叠回移位） | 后端落地真实移位 ISel（已进冲刺排期） |

定期用 `--no-ir-shims` 跑回归：通过即到退役窗口。链级布线修复
（lk 注入 `-b CONST = 0xFC8000`）不属于垫片，是标准布线（QEMU 行为事实）。
