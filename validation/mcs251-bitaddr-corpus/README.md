# G6 bit-addr 改写语料快照（demo 32 / 42 / 58-TFT）

**目的**：G6 缺口的真实根因在仓库外的改写工具（`mcs251-demos-rewritten/tools/rewrite.py`）
的两个缺陷，修复后工具重新生成了 7 个文件。改写包不受 Git 管理，故按 G5/58 先例把
**实际被工具改动的 7 个文件**固化为受控资产（全套语料 776K 太大且含未改动的 348K
图像数据 `pic.h`，故只取命中集）。

**来源**：`/home/liu/LLVM_STC32/mcs251-demos-rewritten/src/` 下，逐字节复制。

## 两个缺陷与本快照的对应

| 缺陷 | 内容 | 本快照中的文件 |
|---|---|---|
| **A** | `fix_bit_names` 的兜底把 `MCS251_SFRBIT` shim 插到文件顶部，而兼容头 `mcs251_bit_compat.h` 是被**传递包含**的（`adc.c → adc.h → config.h`），随后用 error 哨兵重新 `#define` 同名宏、覆盖 shim。修复：先引入有 guard 的兼容头、再定义 shim，使 shim 胜出 | `32/adc.c`、`32/app_ntc.c`、`32/AI8051U.H`、`42/LCD.c`、`58-TFT/tft.c`、`58-OLED/AI8051U.h`（各 +1 行 `#include "mcs251_bit_compat.h"`） |
| **B** | `transform_file` 顺序：`fix_missing_includes`（按字面 `_nop_` 注入 `intrins.h`）跑在 `fix_missing_defines`（注入 `NOP1() _nop_()`）之前，导致只用 `NOP40()` 的文件拿不到 `intrins.h`。修复：把判据扩到 `header_defines.json` 的 `NOP<n>` 注入源（不依赖调序） | `32/app_MatrixKey.c`（+1 行 `#include "intrins.h"`） |

**范围说明（Alice 复核）**：缺陷 A 同时覆盖源码与头文件；缺陷 B **仅覆盖
`transform_file` 的源码路径**（`transform_header` 未调用 `fix_missing_includes`，
头文件路径如需支持是独立后续项）。两缺陷适用面不同 ≠ 命中集不同。

## 冻结 md5（7/7）

```
4d016a6afba9507e46c94d2cb96f93a5  32/adc.c                     (defect A)
2537e3fb61c9dbd9fb56188b0b17aece  32/app_ntc.c                 (defect A)
ea2ae16c370dd638aa1dbf2dfed108b8  32/AI8051U.H                 (defect A)
6b18d2a48b9c1cef5988eb060e755ba7  32/app_MatrixKey.c           (defect B)
bd200c8a6c3b91b75770f43e14871a9f  42/LCD.c                     (defect A)
dee008165d7480c12a76cfb4cd4d0296  58-TFT/tft.c                 (defect A)
0d0e35f5fcc3e0e5c59dbf97b7d9e05c  58-OLED/AI8051U.h            (defect A)
```

## 验收证据（详见 `/home/liu/LLVM_STC32/IMPL-BITADDR-PROGRESS.md`）

- **三 demo T0 全过**：32（11 files）、42（5 files）、58-TFT（2 files）。
- **最小对照**：旧工具输出 == `GAP-BITADDR-PROBES/min/a_order_bug.c`（`macro redefined`
  + 哨兵、rc=1）；新工具输出 == `b_order_ok.c`（rc=0，IR 为字节 RMW `and -33` / `or 32`）。
- **归因归零**：`drive.py` 的 `gap_reason` 里 `bit addr` 计数 **3 → 0**。
- **全批回归**：78 行 A/B 对照——T0 回归 0、提升恰 3 行（`gap,skip → pass,gap`）。
- **工具哈希**：`rewrite.py` 由 `f6f0bc91…` → `f53bf6e7…`；产品源码零改动。

## 残余（如实，未消解）

- `42` T1：`CODE overlap for .text`（G13a/CSEG 族）
- `58-TFT` T1：`CODE address overflow in .mcs251.xdata_init`（G13a/CSEG 族）
- `32` T1：`.mcs251.DSEG.2`（printf.o）28B 窗口（G8）

**本快照不宣称 demo 全绿，也不宣称硬件语义等价**——设计 §3.3 的 W1C（写一清零）与
中断前提原样保留：字节 RMW 模拟仅在"该字节无并发写者、且非 W1C 标志"时成立，
原生位寻址（0xA9 扩展指令）是独立登记的后续切片。
