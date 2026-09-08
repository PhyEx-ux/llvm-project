# MCS251 官方例程移植工具链 v1

此目录是“移植官方全部例程”战役的准备工具，不修改 LLVM/Clang/LLD 源码，也不改写 `~/stcex/src` 语料。它处理 Keil C251 的设备头、`<intrins.h>` 最小兼容层，以及 270 个旧式 `.uvproj` 的项目描述。

所有命令以下均在 Debian WSL 中运行，仓库根目录为 `/mnt/c/Prj/LLVM/MCS251`，本目录为 `validation/mcs251-porting`。

## 当前验收快照

| 项目 | 结果 |
| --- | --- |
| `stc32g.h`（1,232 行） | 117 个直寻址 SFR、418 个 XFR 登记、280 个 sbit 登记、0 个忽略声明 |
| `STC32G144K246.H`（2,810 行） | 119 个直寻址 SFR、1,706 个唯一 XFR 登记（原始声明 1,708 条，2 条重复名入 ignored）、323 个 sbit 登记、13 个忽略声明（2 条重复 XFR + 11 条官方头自身 `))` 括号笔误的 XFR，全部带行号保留） |
| 直 SFR 自检 | 两份生成头均以 `-Werror` 由 s1 Clang 编译为 MCS251 IR，并经 s1 `llc -verify-machineinstrs` 降低成功 |
| `<intrins.h>` 调用审计 | 三口径：原始文本命中 `_nop_()` 608 次/80 文件（含被注释调用）；去注释词法候选 585 次/79 文件；再剔除整数 `#if 0` 死分支后 507 次/79 文件。ELF 路径使用已审计的 `NOP; ERET` helper（`00 AA`，逐字节核验） |
| `.uvproj` | 270/270 XML 成功解析并生成 270 个 Makefile；GNU make dry-run 270/270 成功；201 个含启用 C 源工程的全部 467 条编译规则逐规则核验为显式 ELF `llc`（Keil `IncludeInBuild=0` 的 1 条 `song.c` 保留登记但不构建；其余 69 个为纯汇编/无启用 C 工程不生成编译规则） |
| 自包含负例测试 | `tests/test-porting-tools.py` 53 例全过：注入拒绝、假 PASS 门禁负例、词法分类、三口径计数、构建选择语义、不可表示路径与终审衔接 |

原始可重放证据位于 `/home/liu/porting-tools-v1-evidence/`；仓库内的 JSON、日志和生成物位于 `reports/` 与 `generated/`。工具链与语料身份（二进制 SHA-256、语料 2,303 文件快照哈希、两输入头哈希）见 `reports/provenance.json`。

## 1. SFR 头转换器

`tools/sfr-convert.py` 接受官方 Keil 设备头并生成仅含当前 C 前端可安全使用部分的 C 头和分类 JSON。

```sh
cd /mnt/c/Prj/LLVM/MCS251/validation/mcs251-porting
python3 tools/sfr-convert.py \
  '/home/liu/stcex/src/t24401_69-HID(Human_Interface_Device)协议范例/69-HID(Human Interface Device)协议范例/src/stc32g.h' \
  -o generated/stc32g-v1.h --report reports/stc32g-v1-report.json

python3 tools/sfr-convert.py \
  '/home/liu/stcex/src/t24732_63-QSPI-TFT_DMA_P2P外设到外设_显示视频级动画效果程序-ILI9341/63-QSPI-TFT_DMA_P2P外设到外设_显示视频级动画效果程序-ILI9341/Sources/STC32G144K246.H' \
  -o generated/stc32g144k246-v1.h --report reports/stc32g144k246-v1-report.json

python3 tests/verify-sfr-artifacts.py . \
  --output reports/sfr-artifact-validation.json
```

分类规则如下。

* `sfr NAME = 0x80..0xff;` 生成 `(*(volatile unsigned char *)0xNN)` 宏。这是 v1 唯一启用的寄存器访问形式。
* 位于 `0xFE00..0xFFFF` 或 `0x7EF000..0x7EFFFF` 且带 Keil `xdata`/`far` 限定的 XFR 指针宏仅登记到报告，并输出为注释；开区间式 `>=0xFE00` 判定被显式拒绝，区间外的值记 ignored。
* `sbit NAME = P3 ^ 2;` 仅登记，并在生成头中规范为 `P3^2` 注释。
* `sfr16`、同名重复声明（保留原行号与原因）、地址/位号范围不合法或无法识别的 Keil 声明记为 `ignored`；不会被静默丢弃。`STC32G144K246.H` 中 11 条 `#define X (*(unsigned char volatile far *)0x7ef4xx))` 双右括号笔误即以此路径完整保留。

词法策略（详见脚本注释）：

* 注释与字符串/字符字面量先行掩码，且逐字符保长替换，所有报告行号对应原始物理行；`//` 行拼接注释（反斜杠续行）不会让下一行声明泄漏成宏。
* 条件编译只剔除**确定**死分支：整数 `#if 0`。未知条件（如普通 include guard、`#ifdef __KEIL__`）保留首分支，被剔除的声明以 `<inactive>` 记入 ignored，不会变成 v1 映射。
* 未识别声明原文进入注释前做 `*/` 中和，无法通过生成头注入注释边界。

直 SFR 的编译自检使用只读 s1 二进制：

```sh
/home/liu/build-clang/bin/clang --target=mcs251-unknown-none \
  -std=c11 -Wall -Wextra -Werror -S -emit-llvm \
  tests/stc32g-direct-sfr-selfcheck.c -o /tmp/stc32g-sfr.ll
/home/liu/mcs251-demo-test/bin-frozen/llc -mtriple=mcs251-unknown-none \
  -verify-machineinstrs /tmp/stc32g-sfr.ll -o /tmp/stc32g-sfr.s
```

## 2. `<intrins.h>` 最小兼容层

`include/intrins.h` 只公开：

```c
#define _nop_() __asm__ volatile("nop")
```

重跑语料审计和代码生成实验：

```sh
python3 tests/audit-intrinsics.py /home/liu/stcex/src \
  --output reports/intrinsics-audit.json

/home/liu/build-clang/bin/clang --target=mcs251-unknown-none \
  -std=c11 -Wall -Wextra -Werror -S -emit-llvm \
  tests/inline-asm-nop.c -o /tmp/inline-asm-nop.ll
/home/liu/mcs251-demo-test/bin-frozen/llc -mtriple=mcs251-unknown-none \
  -verify-machineinstrs /tmp/inline-asm-nop.ll -o /tmp/inline-asm-nop.s
grep -n '^[[:space:]]*nop$' /tmp/inline-asm-nop.s
```

`tests/inline-asm-nop.c` 包含公共 `include/intrins.h` 后调用 `_nop_()`，而不是手写替代 inline asm。审计区分三个口径，`reports/intrinsics-audit.json` 同时记录三者：

| 口径 | 定义 | 当前快照 |
| --- | --- | --- |
| `raw_text_call_matches` | 原始文本子串命中（含注释、字符串） | 608 次 / 80 文件 |
| `lexical_call_candidates` | 掩码注释与字面量后的词法调用 | 585 次 / 79 文件 |
| `preprocessed_call_candidates` | 再剔除整数 `#if 0` 死分支 | 507 次 / 79 文件 |

608 与 585 的差额主要来自被注释掉的调用，例如 `~/stcex/src/t24377_51-CAN1-CAN2*/.../main.c:206-212` 连续 7 条 `// _nop_()`。扫描范围是全部 `.c`/`.h` 共 1,005 个文件；29 个文件以某种拼写 include 了 `intrins.h`；三个口径中出现的下划线函数调用都只有 `_nop_()`。

默认 ASxxxx `.rel` 路径中，`_nop_()` 直接展开为 `__asm__ volatile("nop")`，已验收汇编中恰有一条 `nop`。ELF 生产路径需要传 `-DMCS251_PORTING_ELF_NOP_HELPER`；当前 ELF streamer 没有 MCS251 inline-asm parser，公共头会调用 `runtime/elf-nop.yaml` 生成的 `NOP; ERET` helper。该 helper 保留了**真实 NOP**，但含 call/return 开销，不能宣称与 Keil 的单条 inline `_nop_()` 周期相同。

## 3. `.uvproj` 到 Makefile

```sh
python3 tools/uvproj2make.py /home/liu/stcex/src \
  -o generated/projects \
  --report reports/uvproj-summary.json \
  --tsv reports/uvproj-projects.tsv

python3 tests/validate-generated-projects.py \
  reports/uvproj-summary.json generated/projects \
  --output reports/generated-makefile-validation.json
```

每个 `.uvproj` 获得一个稳定的 `slug/Makefile`，避免同名 `sample.uvproj` 相互覆盖。Makefile 会：

* 保留全部 Keil `File` 条目作为审计注释；C 条目使用编号对象名以避免重名。
* 保留项目 IncludePath 和 Define。
* 映射 `STC32G12K128 Series` 到 `stc32g12k128.mk`（`EDATA_END=0x0fff`，143 个），映射 `STC32G144K246 Series`（83 个）及 `STC32G144K246-32Bit Series`（44 个）到 `stc32g144k246.mk`（`EDATA_END=0x3fff`）。
* 生成可执行的 ELF 生产规则：`llc -mtriple=mcs251-unknown-none -verify-machineinstrs -mcs251-object-format=elf -filetype=obj`（旗标已内置在 `tools/uvproj2make.py` 模板中，命令形态与已 PASS 的 `examples/minimal-direct-sfr` 样板一致）→ `mcs251-lld`（五个 `--area-start` 参数 + `--edata-end`）→ `llvm-objcopy -O ihex`。`objcopy` 的前置输入明确是链接后的 ELF。工具、语料根和 ELF NOP helper 可由 `CLANG`、`LLC`、`LLD`、`OBJCOPY`、`YAML2OBJ`、`ELF_NOP_YAML`、`CORPUS_ROOT`、`BOARD_ROOT` 覆盖。

路径与元数据的三层表示是本轮整改的核心语义，不再混用：

* `PROJECT_DIR` 保留真实拼写（可含空格，如 `89-USB CDC转双串口-…`），供 shell 参数使用；`PROJECT_DIR_MAKE` 是 Make 前置依赖用的 `\ ` 转义形。每个启用源文件同时得到 `C_SOURCE_nnn`（Make 转义，作前置依赖）与 `C_SOURCE_RAW_nnn`（真实路径，作编译命令的双引号参数），二者不交叉使用，dry-run 命令经 shell 词法切分校验为单参数。
* XML `Define`/`IncludePath` 是不可信输入：`$`、`#`、反引号、引号、换行一律拒绝（生成失败并给出明确诊断，绝不产出可被 Make/shell 二次展开的文本）；带空格的 include 路径是合法输入，按单个 shell 参数渲染。`NAME=unquoted-safe-value` 之外的 Define 值拒绝。
* Keil 的 `IncludeInBuild=0` 条目（实测 MP3 例程 `3rd/minimp3/song.c`）保留在审计注释与报告中，但不生成编译规则；含 Make/shell 不可表达字符的启用文件同样降级为“登记不构建”并计数（当前语料为 0 个）。
* 生成 Makefile 的 `clean` 目标带 BUILD 目录边界守卫，`.DEFAULT_GOAL := all` 且 `.NOTPARALLEL:`。

可先检查单个工程的展开命令而不尝试编译未迁移的 Keil C：

```sh
make -C generated/projects/sample-b3fd09d50cb0 -n all
make -C generated/projects/sample-b3fd09d50cb0 print-config
```

转换器会保留 `.uvproj` 内的原始 `FilePath` 用于审计，并在文件仅因大小写不一致时使用语料实际拼写做 GNU make 前置依赖。当前发现并校正了 2 个 `app_display.c`/`app_Display.c` 项；未解析（语料中不存在）的 C 文件条目为 0。计数口径注意：**listed（468）≠ enabled（467）≠ build rules（467）**，三者与排除数都在 `reports/uvproj-summary.json` 与 TSV 中分列；随着 Keil 语义覆盖面变化这些数字会变，不要把任何单项当永恒值。

## 今天可移植的最小样板

`examples/minimal-direct-sfr/` 是实际 ELF/QEMU 证明，而不是 dry-run。业务 `main.c` 只包括 `generated/stc32g-v1.h` 和 `include/intrins.h`，写入转换后的 `P0`、`P1`、`P4` 直 SFR 宏，调用 `_nop_()`，并通过 `SBUF` (`0x99`) 输出 QEMU UART 哨兵。复位入口由 `validation/mcs251-elf/runtime/crt-selfstart.yaml` 以 `yaml2obj` 就地生成到 `build/crt-selfstart.o`（M4：样板不依赖任何未跟踪的裸 `crt.o`，`make clean all` 从 YAML 源闭合重建），不向样板引入中断、bit、xdata 或 Keil 汇编。

```sh
cd /mnt/c/Prj/LLVM/MCS251/validation/mcs251-porting/examples/minimal-direct-sfr
make clean all
make run
# 精确输出：PORTING-MINIMAL-PASS
```

它已验收完整链：Clang → llc（ELF）→ mcs251-lld → `llvm-objcopy -O ihex` → `qemu-system-mcs251 -M stc32g144k246`。结构化结果见 `reports/minimal-direct-sfr-validation.json`；最小通用链 smoke 见 `reports/toolchain-smoke.json`。

## 测试与门禁

`tests/` 共 8 个 Python 脚本与 3 个自检 C 文件：

| 脚本 | 角色 |
| --- | --- |
| `test-porting-tools.py` | 自包含边界/负例套件（53 例，输入内嵌，不依赖语料；有冻结工具链时自动加测真实编译/ELF 负例） |
| `verify-sfr-artifacts.py` | SFR 报告/生成头/源身份/自检用例的独立交叉核对（拒绝重复宏、删注释、计数不符） |
| `validate-generated-projects.py` | 全量 270 Makefile `make -n` dry-run 验证 |
| `run-elf-smoke.py` | 最小 clang→llc(ELF)→lld→objcopy 生产链 smoke |
| `run-minimal-direct-sfr.py` | 样板从零构建 + QEMU UART 哨兵 + NOP helper 符号核验 |
| `audit-intrinsics.py` | 语料 intrinsic 三口径审计 |
| `assemble-final-validation.py` | 终审门禁：绑定子报告 `passed`、逐 Makefile SHA-256 与逐规则 ELF 旗标、真实编译自检、逐字节 NOP 解析；任一负形（假 PASS、产物集不符、内容篡改、单规则缺旗标、多余规则、坏自检源、伪 NOP YAML）都会拒绝 |
| `inline-asm-nop.c` 等自检源 | 随脚本编译的输入 |

```sh
python3 tests/test-porting-tools.py   # 自包含套件，工作站在无语料/无冻结二进制时也能跑
```

## v1 限制和待办

1. **Keil 语言方言尚未移植。** `sbit`、`bit`、`xdata`、`far`、`_at_`、中断声明、绝对放置、启动代码及 Keil 汇编均不由 v1 翻译或编译。XFR 和 sbit 仅是完整登记，不是 C 可访问定义。
2. **生成 Makefile 不代表官方例程可端到端构建。** 270 个 Makefile 的解析/dry-run 已验收；例程真实编译验收必须等待上述源语言特性落地。
3. **ELF 旗标是生产模板的硬要求。** s1 `llc -filetype=obj` 的默认值是 ASxxxx `XH3`/`.rel`，不是 lld 缺陷；llc 必须显式使用 `-mcs251-object-format=elf`（已内置在 `tools/uvproj2make.py` 模板中），否则 mcs251-lld 不能接受该旧格式。ELF 链还要求 HOME、VECS、BOOT、CSEG、XINIT 五个 `--area-start` 参数和板级 `--edata-end`。本目录的最小 smoke 及 QEMU 样板均已通过该完整链。
4. **ELF `_nop_()` 有时序限制。** 当前 ELF streamer 缺少 MCS251 inline-asm parser，所以在 ELF 模式下公共头改调 `runtime/elf-nop.yaml` 中 `00 AA`（NOP; ERET）helper。它证明确实执行 NOP，却增加 call/return，暂不适用于依赖 Keil inline NOP 精确周期的例程；原 `.rel` 路径仍直接发射一条 inline `nop`。
5. **工具路径是环境相关默认值。** 默认路径指向本机 WSL 的只读 s1 构建产物；在其他工作站应以 Make 变量覆盖。不得在本任务中重建或修改这些二进制。
6. **审计数字是快照口径，不是永恒值。** 本 README 的 608/585/507 与 468/467/1 是当前 `~/stcex/src` 快照下三口径/三计数的可复算结果，`reports/intrinsics-audit.json` 与 `reports/uvproj-summary.json` 同时记录口径定义；终审门禁对 608（原始文本）、585/79（去注释词法）钉住快照值，语料更新后需随报告同步重钉。
