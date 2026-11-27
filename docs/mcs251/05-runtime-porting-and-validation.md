# 05. 运行时库、移植工具链与全链路验证体系

> **核对源码基线**：`cad7f0ab1f32`  
> **核心目录**：`llvm/lib/Target/MCS251/Runtime/`、`validation/mcs251-runtime/`、`validation/mcs251-elf/runtime/`、`validation/mcs251-porting/`、`validation/mcs251-*`

---

## 1. 运行时库体系与构建规范

所有 MCS-251 运行时库共同遵循四条硬性工程铁律：
1. **严格隔离于 LLVM 链接图**：绝不对 LLVM/Clang/LLD 库使用 `target_link_libraries`，仅以 CMake 构建依赖表达两阶段顺序；
2. **两阶段同树自举**：第一阶段构建 `clang` 与 `llc`，第二阶段调用同一构建树产物（或显式传入 `MCS251RT_CLANG`/`MCS251RT_LLC`）；
3. **三步审计管线**：`clang -S -emit-llvm` → **对交付给后端的精确 `.ll` 执行 IR 审计** → `llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj`（禁止绕过审计直接 `clang -c`）；
4. **独立 `.o` + 显式清单交付（可选 `.a` 归档）**：现行 `mcs251-lld` 已支持经典/Thin `.a` 静态库按需提取与 `--gc-sections`；现有审计运行时（`mcs251rt` 与 `validation/mcs251-runtime`）仍保留按函数/模块拆分的独立 ELF32BE `ET_REL` 对象与显式清单交付，以维持确定性审计溯源。

### 1.1 整数除法/取模运行时（`mcs251rt`）
- **源码位置**：`llvm/lib/Target/MCS251/Runtime/`（Apache-2.0 WITH LLVM-exception 纯 C 独立实现，采用公开恢复余数除法算法 + N 位余数不变量，无 64 位中间值、无 `/` 或 `%`、无内联汇编、无全局可变状态）。
- **构建接入**：已在 `llvm/CMakeLists.txt:1629-1640` 通过 `-DLLVM_MCS251_ENABLE_RUNTIME=ON`（默认 `OFF`）挂载为显式目标 `mcs251rt`（不在 `ALL` 中）。
- **IR 审计**：`mcs251rt-audit-ir.cmake` 三态检查 `grep -Eq -e udiv -e sdiv -e urem -e srem`（仅 `rc == 1` 通过，命中或工具异常均中止），杜绝隐式递归调用。
- **固定 8 行交付清单（`mcs251rt-objects.list`，UTF-8 无 BOM，LF）**：在 Route B 默认栈传参 ABI 下，8 个除/余运行时均改由栈传递第 2 参数（不再占用静态 `OSEG`/`DSEG` `_PARM_2` 槽，天然可重入，可供 ISR 与前台安全共享）：

| 序号 | 源码 | 定义符号 | 签名 | 备注 |
|---:|---|---|---|---|
| 1 | `mcs251rt_divuint.c` | `__divuint` | `i16(i16,i16)` | 叶函数，第 2 参走栈 |
| 2 | `mcs251rt_divulong.c` | `__divulong` | `i32(i32,i32)` | 叶函数，第 2 参走栈 |
| 3 | `mcs251rt_divsint.c` | `__divsint` | `i16(i16,i16)` | 非叶（调 `__divuint`），可重入 |
| 4 | `mcs251rt_divslong.c` | `__divslong` | `i32(i32,i32)` | 非叶（调 `__divulong`），可重入 |
| 5 | `mcs251rt_moduint.c` | `__moduint` | `i16(i16,i16)` | 叶函数，第 2 参走栈 |
| 6 | `mcs251rt_modulong.c` | `__modulong` | `i32(i32,i32)` | 叶函数，第 2 参走栈 |
| 7 | `mcs251rt_modsint.c` | `__modsint` | `i16(i16,i16)` | 非叶（调 `__moduint`），可重入 |
| 8 | `mcs251rt_modslong.c` | `__modslong` | `i32(i32,i32)` | 非叶（调 `__modulong`），可重入 |

### 1.2 软浮点 `f32`、位工具、原子库与 `libc`/`printf` 运行时（`validation/mcs251-runtime/`）
- **语言与构建契约**：统一 `-std=c11 -DMCS251_RT_TARGET`（`mcs251_float.h` 内置 ILP32 `_Static_assert` 互锁）；`mcs251rt-ir-audit.cmake` 剔除注释/元数据/`target datalayout` 后，严格禁止 `float`/`double`/`i64` token 及 `ctlz`/`cttz`，且一切外部 `@symbol` 必须精确命中白名单 `{mcs251_msb32, putchar, out_char, out_str, out_float}` 或已验下译内建 `llvm.{lifetime.start,lifetime.end,umax,abs,ucmp,fshl,fshr,va_start,va_end,va_copy,mcs251.ea.save.disable,mcs251.ea.restore,mcs251.atomic.fence}`。
- **16 位 Limb 技巧（`mcs251_float_limb.h`）**：MCS-251 无 32 位桶形移位器（32 位常右移每移 1 位约展开 ~21B `rrc a` 链，`v>>24` 可达 527B）；软浮点通过大端 `volatile union` 拆为 16 位 `h[0]`/`h[1]` 规避体积爆炸，乘法重写后精确命中 10 条硬件 `MULW`（0 条 `MULAB`）。
- **全部 13 个产出 `.o` 对象（由 12 个 `.c` 构建，`mcs251_printf.c` 构建两套形态；`mcs251_float_math.c` 保留工作树不参与构建；已全部基于 Route B 栈 ABI 重建并通过 IR 审计与一键链接验证）**：

| 输出 `.o` | 源码与优化级 | 额外参数 | 导出符号与设计说明 |
|---|---|---|---|
| `mcs251_float_arith.o` | `mcs251_float_arith.c` (`-O2`) | — | `__negsf2`、`__floatsisf`、`__fixsfsi`、G7 S1' 无符号转换 `__floatunsisf`、`__fixunssfsi` |
| `mcs251_float_addsub.o` | `mcs251_float_addsub.c` (`-O2`) | — | `__addsf3`、`__subsf3`（G7 S1'' 拆包实现按例程链接） |
| `mcs251_float_mul.o` | `mcs251_float_mul.c` (`-O2`) | — | `__mulsf3`（16 位 limb 硬件 `MULW` 重写） |
| `mcs251_float_div.o` | `mcs251_float_div.c` (`-O2`) | — | `__divsf3` |
| `mcs251_float_cmp.o` | `mcs251_float_cmp.c` (`-O2`) | — | 7 个 IEEE-754 比较：`__eqsf2`/`__nesf2`/`__ltsf2`/`__lesf2`/`__gtsf2`/`__gesf2`/`__unordsf2` |
| `mcs251_bitutil.o` | `mcs251_bitutil.c` (**`-O0`**) | — | `mcs251_msb32`（32B de Bruijn 表；独立 TU + `-O0` 防止 `-O2` 综合成不支持的 `ctlz`） |
| `mcs251_libc.o` | `mcs251_libc.c` (**`-O0`**) | — | `memset`、`memcpy`、**重叠安全 `memmove`**、`strcpy`、`memcmp`（已适配 Route B 栈 ABI，闭合 `>16B` 重叠拷贝的 `_memmove` 符号） |
| `mcs251_str.o` | `mcs251_str.c` (**`-O0`**) | — | `strlen`（G13a-S2 拆出支持最小对象闭包） |
| `mcs251_putchar.o` | `mcs251_putchar.c` (**`-O0`**) | — | `void putchar(char)`（11B 丢弃型 sink，用于未自定义 `putchar` 的 `printf` 工程闭合链接） |
| `mcs251_printf.o` | `mcs251_printf.c` (**`-O0`**) | `-ffreestanding -fno-builtin` | 默认完整变参 `printf`/`sprintf`（已迁移至标准 `<stdarg.h>` `va_start`/`va_arg`/`va_end`，彻底移除 `*_PARM_*` 静态槽与 6 变参硬上限，依赖 `UND _out_float`） |
| `mcs251_printf_float.o` | `mcs251_printf_float.c` (**`-O0`**) | `-ffreestanding -fno-builtin` | `out_float`（G13a-S3 拆出，约 4469B） |
| `mcs251_printf_nofloat.o` | `mcs251_printf.c` (**`-O0`**) | `-ffreestanding -fno-builtin -DMCS251_PRINTF_NO_FLOAT` | 无浮点版 `printf`（G13a-S3，标准 `va_list` 栈变参，节省浮点格式化体积；禁止在含 `%f/%F/%g/%G/%e/%E` 格式符时选用） |
| `mcs251_atomic.o` | `mcs251_atomic.c` (**`-O0`**) | `-ffreestanding -fno-builtin` | 大对象 C11/GNU 原子运行库：`___atomic_{load,store,exchange,compare_exchange,is_lock_free}` 及 `atomic_{thread,signal}_fence`（通过 `__builtin_mcs251_ea_save_disable`/`restore` 加锁保护） |

### 1.3 纯 ELF 启动运行时 CRT（`validation/mcs251-elf/runtime/`）
尽管工具链现已补齐 `MCS251AsmParser` 与 `llvm-mc`，现有全部已冻结纯 ELF CRT 仍以手写 YAML（经 `yaml2obj` 编译，机器字节逐字节核对 `sdas251` 黄金清单）维护以保证字节级回归可复现性：

| YAML 源 | 对象身份 | 关键布局与启动流程 | 校验脚本 |
|---|---|---|---|
| `crt-selfstart.yaml` | **v1** (`e_flags=0x1`, `.note.mcs251.abi`) | `HOME=0xFF0000`, 8 槽 `VECS`, `BOOT=0xFF0100` (`0xD5`B) | `WTST=0` → `SPX=#__mcs251_stack_base` → `__mcs251_globals_init`（XINIT）→ `__mcs251_xdata_init`（X4 7B 头，执行后不恢复 `DPXL`）→ `_main` → halt |
| `crt-selfstart-v2.yaml` | **v2** (`e_flags=0x102`, `.mcs251.attributes`) | 同上代码/重定位字节（由 `gen-crt-v2.sh` 生成并修补 `e_flags`） | `check-crt-v2.py` |
| `crt-irq.yaml` | **v1** + ISR v2（G1-2） | `HOME=0xFF0000` (`LJMP 0xFF0500`), 127 槽 `VECS`, **`BOOT=0xFF0500`** (`0x106`B), **`CSEG=0xFF0700`** | `clr EA`, `PSW=0`, `DPS=0`, `SPX`, 全清 16B `BSEG_BYTES` (`0x20..0x2F`), XINIT, XDATA_INIT, `_main`；默认槽 `BOOT+0x102` 为 `C2 AF 80 FE` |
| `crt-irq-v2.yaml` | **v2** + ISR v2 | 同 `crt-irq.yaml` 代码字节 | `check-crt-v2.py` + `check-crt-irq.py --chain`（20 项断言） |
| `crt-bit.yaml`（BT14） | **v1** + ISR v2 + `.mcs251.bitprofile` | 同 IRQ 布局，`BOOT=0x111` (273B) | **取代**全清 16B，调用 55B `__mcs251_bit_init` 消费 `lld` 合成的 `BITINIT` 3B 表，执行 `(IRAM[a] & ~mask) \| (val & mask)` 保护邻位 |

---

## 2. Keil 官方例程移植工具链与受控语料

### 2.1 官方例程移植工具（`validation/mcs251-porting/`）
1. **SFR 头转换器 `tools/sfr-convert.py`**：
   - 支持 `stc32g.h`（117 直 SFR / 418 XFR / 280 sbit）与 `STC32G144K246.H`（119 直 SFR / 1,706 唯一 XFR / 323 sbit / 13 ignored 含官方头 11 处 `))` 笔误）；
   - **两种风味选择红线**：v2 布局（**含当前不传参默认链 `{1,2,32,8,1}`**）**必须使用 `--sfr-address-space=6` 生成的 `*-as6.h`**（AS0 绝对地址在 v2 下生成 `@dr` RAM 访问而非直 SFR）；其中 `RSTCFG@0xFF` 因后端禁用 AS6 `0xFF` 降级为结构化 `as6_demoted: true` 注释；XFR（`0x7E0000..0x7EFFFF`）始终输出标准 32 位 AS0 `volatile` 指针宏（使用前须置 `P_SW2.EAXFR=1`）。
2. **`<intrins.h>` 最小兼容层**：对官方语料 1,005 文件三口径审计显示唯一用到的内建为 `_nop_()`（预处理后 507 次/79 文件）；`.rel` 路径展开为 `__asm__ volatile("nop")`，ELF 路径（`-DMCS251_PORTING_ELF_NOP_HELPER`）调用 `runtime/elf-nop.yaml`（`00 AA`, `NOP; ERET`，有调用周期开销）。
3. **`.uvproj` 批转 Makefile（`tools/uvproj2make.py`）**：已验证解析 270/270 官方 `.uvproj`（143 个 G12K128 + 127 个 G144K246）、270/270 `make -n` dry-run 通过、467 条启用 C 规则均带 `-mcs251-object-format=elf` 且拒绝 `$`/`#`/引号注入；附 87 项负例测试（`test-porting-tools.py`）与 QEMU 最小样板（`PORTING-MINIMAL-PASS`）。

### 2.2 受控改写语料与集成切片
- **`validation/mcs251-agg85-corpus/`**：demo 58 OLED 仅改 2 处 `__code` 即与平铺基线 `.text` SHA-256 逐字节一致；demo 85（148K SRAM）经证实超出板级 XDATA 与栈红线，保持 T1 探针（其 `>64K` 全零 XDATA 拆分由 G13b 在产品中落地）。
- **`validation/mcs251-bitaddr-corpus/`（G6）**：修正外部 `rewrite.py` 宏包含顺序与 `NOP<n>` 缺头，零产品改动使 3 个 demo 的 bit-addr T0 缺口清零。
- **`validation/mcs251-g5-corpus/`（G5）**：demos 82/44 CANFD-DMA 精确补齐 AS3 `__xdata` 形参闭合（零 `addrspacecast`，100% 24 位 `MOVX` + DPXL）。
- **`validation/mcs251-xdata-e2e/`（X4 + A3）**：`check-bytes.py` 146/146 source 模式解码通过、QEMU 预污染内存验证清零 walker（`XDATA-E2E-PASS`）、`-O2` Glue 焊接回归与 `AS4→AS0` 跨 TU 验证（`A3-LAYOUT-PASS`）。
- **`validation/mcs251-tiny-impl/`**：Tiny/XTiny 分 TU 2B 指针参数槽汇编级验证。

---

## 3. 自动化测试规模与「静态 / QEMU / 真机」证据矩阵

### 3.1 树内自动化测试规模

| 测试套件 | 文件总数 | Lit / GTest 用例数 | `Inputs/` 辅助 | 备注 |
|---|---:|---:|---:|---|
| `llvm/test/CodeGen/MCS251` | 221 | **203** | 18 | 含 `function-data-sections.ll`、`stack-abi-csr-tailcall.ll`、`atomics-ea.ll` 与 `Inputs/phase11-qemu.py` |
| `llvm/test/MC/MCS251` | 3 | **2** | 0 | `disassembler.txt`、`asm-parser.s`（含 `lit.local.cfg`） |
| `llvm/test/` 其余 MCS251 | 4 | **4** | 0 | `Assembler/mcs251-isr-cc.ll`, `Verifier/mcs251-isr-invalid.ll`, `llvm-readobj/.../attributes.test`, `yaml2obj/ELF/mcs251.yaml` |
| `clang/test/` MCS251 | 141 | **122** | 19 | CodeGen 63（含 `mcs251-inline-asm.c`、`mcs251-dwarf.c`、`mcs251-struct-abi.c`、`mcs251-reentrant-abi.c`、`mcs251-atomics.c`）/ Sema 46 / Parser 8 / Driver 3 / PCH 1 / Preprocessor 1 |
| `lld/test/MCS251` | 74 | **47** | 27 | 含 `archive.test`（含 `-L`/`-l`）、`weak-comdat.test`、`gc-sections.test`、`dwarf-debug.test`、`reentrant-isr-shared.test` |
| `llvm/unittests/` MCS251 | 3 | **3** | 0 | `MCS251AttributesTest`, `MCS251SignaturesTest`, `TripleTest` |
| **合计** | **446** | **381** | **64** | 378 个 lit 测试 + 3 个 C++ GTest |

### 3.2 严格分级验证证据矩阵（绝不将静态或 QEMU 证据升级为真机证据）

| 验证资产 | 目标板 / 场景 | 静态 / 链接检查 | QEMU 仿真证据 | 真机实测证据 | 关键边界 / 已知 QEMU 偏差 |
|---|---|---|---|---|---|
| **G12 8 项基础自检**（`mcs251-demos/REALHW-GUIDE.md`） | STC32G12K128（24MHz, layout-v2） | PASS（`SPX=0x010F`） | **PASS** | **已证实 PASS**（稳定周期输出 `SELFTEST-PASS`） | 不可外推至 G144 或 `MOVC` bank 行为 |
| **ISR 硬件帧探针 v2**（`mcs251-isr/hwframe/REALHW-20260910.md`） | STC32G12K128 Timer0 | PASS | **PASS**（含超时与 `SP_MISMATCH` 故障注入） | **已证实 PASS**（2026-09-10 实测帧 `00 FF 74 02`，`B=01FE I=0202 R=01FE`，`SPH=0x85` 进位证实） | 单层非嵌套，`PSW1=00` 未测非零标志恢复 |
| **ISR T10 单层实机 Demo**（`mcs251-isr/realhw-demo/`） | STC32G12K128 Timer0 + 37B 编译器 ISR + 跨 TU helper | PASS（`check-crt-irq.py` 20/20） | 按指令未跑 | **G1 前资产已证实 PASS**（`s` 1 次 + `r` 10,000 次全过）；**G1 重建资产 `NOT_RUN`** | 暴露了 `PSW1_RAW=00/20`（V1）；E1–E5 已全部修复；`NESTED`/`WINDOWS` 未测 |
| **V1 PSW1 调查包**（`v1-psw1-investigation/`） | 3 臂 × W/L/I 阶段 | PASS | **PASS**（3 轮含非零 N/Z `02`/`20`） | **待真机 (`NOT_RUN`)** | QEMU 对 `DJNZ`/`INC`/`ORL` 更新 N/Z，可能偏离经典 8051 |
| **V2 高寄存器破坏 & 两级嵌套**（`v2-clobber-nesting/`） | Timer0/Timer1 主动破坏 R16–R31 + 两级嵌套 | PASS | **PASS**（`clobber` 10k + `nesting` 10k 均过） | **待真机 (`NOT_RUN`)** | 逐指令抢占窗口须真机验证 |
| **G144K246 高槽测试计划**（`G144K246-HIGH-SLOT-TEST-PLAN.md`） | STC32G144K246 槽 `52..126` | 静态 127 槽单测 PASS | 跳过（模型未证实） | **仅计划 (`NOT_RUN`)** | 严格区分 CPU 跳转 / 控制器路由 / 外设触发 |
| **TFPU 状态/延迟探针**（`mcs251-tfpu-hw/`） | STC32G12K128 + 272-NOP `JMP @A+DPTR` sled | PASS（串口先于 TFPU 硬序） | **仅协议冒烟**（不可作语义证据） | **待真机 (`NOT_RUN`)** | **QEMU TFPU 瞬时完成且状态位系自创**；手册表显示 12K128 可能无 TFPU 硬件；已反汇编辟谣官方 `A5 EF` 为 `MOV A,R7` 而非 SFR `0xEF` |
| **MOVC Bank 仲裁探针**（`mcs251-demos/movc-arbitration/`） | `FE:0800` 执行 `MOVC` | PASS | **PASS**（`MOVC=3C` 随 PC bank） | **待真机 (`NOT_RUN`)** | 手册暗示可能固定 `FF` bank，须真机仲裁 |
| **现代 C 14 项 Demo / 33.1776MHz UART Demo** | G12K128 / G144K246 | PASS（`check-encoding.py` 1482/1482 指令） | **PASS**（Host==QEMU） | 33.1776MHz 与新版 modern-demo **待真机**（同配方 `g12-divrt-i2c`/`g12-xfrrw` 已实板过） | QEMU 不建模 IRC 频率；`0xFFxxxx` 数据读在 QEMU 返回 0 |
| **运行时与功能集成测试**（`mcs251rt` / `mcs251-runtime` / `xdata-e2e` / `bt14` / `endian` / `p11`） | 除余 680 向量、软浮点 100 万对有理数 oracle + 负控、端序 688 断言、X4/A3/BT14 | 全 PASS（含 IR 审计与 Tag 28 门禁） | **全 PASS**（三方 Host/SDCC/DUT 逐字节一致） | `mcs251rt` 经 `g12-divrt-i2c` 实板覆盖；其余为 QEMU + Host 证据 | 仅代表 QEMU/Host 语义闭合，不自动升级为真机硅片证明 |
| **FE-bank 配方 `g13a-fe640`**（`SPEC.md §5.1.1`） | **仅限 STC32G144K246**（`XINIT=0xFE0000`, `XDATA_INIT=0xFE0280`, `--flash-base=0xFC2800 --flash-size=0x3D800`） | PASS | PASS（大映像 `<=64896B`） | **真机烧录待验证** | **严禁 K128 继承**（K128 `FE:0000` 为不可 `MOVC` 的 EEPROM）；G144 烧录/IAP 须保护 FE 数据窗 |
