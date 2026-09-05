# MCS-251 端序统一验收报告

> 落盘：PM，2026-09-05。正文来自 Alice（端序统一专项工程师）最终交付，
> 报告由 PM 写入。原始日志镜像 `transcripts/`，源/工具指纹
> `transcripts/source-sha256.json`。

## 1. 验收总表

| 项目 | 结果 |
| --- | --- |
| 原始 MCS251 lit 基线 | 33/33 PASS |
| 最终 MCS251 lit | 36/36 PASS |
| 原有 lit 断言变更 | 0 |
| TargetParserTests | 696 PASS，4 个 host-specific SKIP，0 FAIL |
| 新端序 QEMU 正例 | 4/4 PASS |
| 每个正例断言数 | 172 |
| 正例断言执行总数 | 688 |
| 保存旧 llc 的负对照 | 2/2 精确复现预期失败 |
| Phase 11 完整 QEMU 回归 | 21/21 镜像 PASS |
| O0 asm/obj 链接字节一致性 | PASS |
| O2 asm/obj 链接字节一致性 | PASS |
| 最终构建、lit、单元测试、六图矩阵串联执行 | exit 0 |

## 2. 本次变更文件

生产代码仅三处：

1. `llvm/lib/TargetParser/TargetDataLayout.cpp` — MCS251 DataLayout 首字符 `e` → `E`。
2. `llvm/lib/TargetParser/Triple.cpp` — 修正端序分类和不存在的小端变体行为。
3. `llvm/lib/Target/MCS251/MCTargetDesc/MCS251MCAsmInfo.cpp` — 修正 MC 数据端序及宽数值 directive fallback。

测试与验证资产：

4. `llvm/unittests/TargetParser/TripleTest.cpp`
5. `llvm/test/CodeGen/MCS251/endian-memory.ll`
6. `llvm/test/CodeGen/MCS251/endian-prefix.ll`
7. `llvm/test/CodeGen/MCS251/endian-fold.ll`
8. `validation/mcs251-endian/run.py`

另新增原始日志与指纹目录：`validation/mcs251-endian/transcripts/`。

## 3. 双向互操作构造

### 3.1 SDCC 真正初始化的对象

对象由 SDCC provider 中的 C `= initializer` 定义生成，不由 LLVM 或手写字节替代初始化。

| 对象 | 宽度 | 物理地址 | 初值 |
| --- | ---: | ---: | ---: |
| e16 | 16 | 0x000801 | 0x1357 |
| e32 | 32 | 0x000805 | 0x89ABCDEF |
| x16 | 16 | 0x010401 | 0x1357 |
| x32 | 32 | 0x010405 | 0x89ABCDEF |

SDCC 使用 `__xdata` 拼写其 flat far 访问，包括 region-00 的 edata 地址。刻意选用非对称值和奇地址，并在对象前后布置 `A5/5A` guards。

provider 用 `--no-xinit-opt` 编译，初始化由 SDCC 生成的 GSINIT 标量存储完成，不依赖模板中为空的 XINIT runtime hook。

### 3.2 正向

SDCC 初始化后：SDCC 读取对象并检查初值；LLVM 读取同一对象，SDCC harness 检查数学值；SDCC 按字节检查实际内存顺序；检查前后 guards。

### 3.3 反向

LLVM 从 ABI 参数接收值，写入相同对象；SDCC 检查完整数值、每个内存字节、前后 guards。

u16 写入集合：`2468, 8001, FFFF, 0000`；u32 写入集合：`10293847, 80010203, FFFFFFFF, 00000000`。

### 3.4 附加验证

每图还验证：load→trunc（u16→u8、u32→u8、u32→u16）；常量 store→首字节 load 转发；volatile i16/i32 栈往返各五个值；i32→i16 truncating store；i16→i32 zero-extending load；21 个 LLVM prefix 字节；5 个 ASxxxx `.word/.3byte` 字节；5 个 opt 常量折叠返回值。

## 4. 负对照：真实误编译证据

保存的旧编译器：`/home/liu/mcs251-endian/baseline/llc`。

旧版 O2 的 asm、obj 两条链均先通过普通 u16/u32 读取和内存字节检查，然后在窄化读取处失败。旧版 obj transcript 连续原文：

```text
SDCC.init.x16=1357
LLVM.read.x16=1357
init.bytes.x16.0=13
init.bytes.x16.1=57
guard.init.x16.before=A5
guard.init.x16.after=5A
SDCC.init.x32=89ABCDEF
LLVM.read.x32=89ABCDEF
init.bytes.x32.0=89
init.bytes.x32.1=AB
init.bytes.x32.2=CD
init.bytes.x32.3=EF
guard.init.x32.before=A5
guard.init.x32.after=5A
LLVM.low16=13
FAIL expected=0x57 got=0x13
qemu-system-mcs251: terminating on signal 15 from pid 3295 (timeout)

timeout exit=124
```

完整证据：`transcripts/matrix/negative-asm.qemu.log`、`transcripts/matrix/negative-obj.qemu.log`。

结论：完整宽度往返的成功不能证明 DataLayout 正确；本次负对照直接证明旧布局导致实际 load 缩窄误编译。

## 5. 新版 QEMU transcript

新版四条链均通过，且每条输出 172 个已检查值。O2 obj 串口原文连续片段：

窄化与转发：

```text
LLVM.low16=57
LLVM.low32=EF
LLVM.lowword32=CDEF
LLVM.forward16=13
LLVM.forward32=89
```

LLVM 写、SDCC 读：

```text
SDCC.read.x32=10293847
write.bytes.x32.0=10
write.bytes.x32.1=29
write.bytes.x32.2=38
write.bytes.x32.3=47
guard.write.x32.before=A5
guard.write.x32.after=5A
```

截断和零扩展：

```text
SDCC.trunc_store16=CDEF
LLVM.zext_load16=0000CDEF
```

ASxxxx 数据与常量折叠：

```text
asm_byte.0=13
asm_byte.1=57
asm_byte.2=A1
asm_byte.3=B2
asm_byte.4=C3
LLVM.fold_bytes16=1234
LLVM.fold_bytes32=12345678
LLVM.fold_scalar_byte=89
LLVM.fold_scalar_word=CDEF
LLVM.fold_record=5789
PASS
```

完整四份串口原文：`transcripts/matrix/endian-{O0,O2}-{asm,obj}.serial.raw`（同目录 `.qemu.log` 保存完整命令、串口、QEMU stderr 和 timeout 返回值）。

判定要求为完整 PASS 且无 FAIL。harness 末尾故意死循环，六条链的 timeout 均为 124；**124 本身从未被用作成功依据**。

## 6. Phase 11 回归

执行原有驱动 `llvm/test/CodeGen/MCS251/Inputs/phase11-qemu.py`：

| 分组 | 镜像数 | 结果 |
| --- | ---: | --- |
| ISA | 1 | PASS，127 个 ISA assertions |
| comparisons | 4 | O0/O2 × asm/obj 全通过 |
| pointers | 4 | O0/O2 × asm/obj 全通过 |
| regressions | 4 | O0/O2 × asm/obj 全通过 |
| legacy i32 | 4 | O0/O2 × asm/obj 全通过 |
| smoke | 4 | O0/O2 × asm/obj 全通过 |
| 合计 | 21 | 全部 PASS |

覆盖跨 64K 指针运算、指针槽存储序、i32 内存、间接调用、动态栈、VLA、递归和原有算术路径，强于原任务要求的抽查。

代表性原文：`BABCDEFGHIJKLMNOPQRSPASS`。

日志：`transcripts/phase11-run.log`、`transcripts/phase11/`。原 Phase 11 驱动使用 Python 超时终止而非 GNU timeout，不把这些运行的终止状态记为 124；驱动整体 exit 0，并已逐份核查 21 个 transcript 均含 PASS、无 FAIL。

## 7. GSINIT 尾跳：验证构造中发现的坑

共享固件模板原本直接跳到 `__sdcc_program_startup`，刻意不执行初始化。本次为验证 SDCC `=initializer`，将本地 crt0 副本改成进入 GSINIT。首次运行没有输出 `B`。检查 map 发现 `GSINIT = 0xfc61fc`、`GSFINAL = 0xff000d`，两区不连续，初始化执行完后不能靠 fall-through 到达 GSFINAL。

修正方式：不改公共 crt0 模板；不手写对象初始化字节；保留 SDCC 生成的 GSINIT 存储；在本次测试 GSINIT 尾部追加独立模块，显式 `EJMP __sdcc_program_startup`。之后旧版负对照和新版正例均进入 harness 并得到预期结果。归因属于测试启动链构造，不属于端序后端回归。

**Step 4 自建启动不能假定 GSINIT 与 GSFINAL 连续。**

## 8. lit 说明

原有 33 个 lit 文件及其断言均未改动，全部保持通过，无需逐项迁就大端结果修改旧断言。

新增：

- `endian-memory.ll`：检查 target-default 下的窄化地址 `+1/+3/+2`，以及返回高字节的 store→load 转发；同时作为 QEMU 被测模块。
- `endian-prefix.ll`：检查大端 `.word` fallback 和直接 object 的实际字节；覆盖标量和聚合 prefix。
- `endian-fold.ll`：不显式覆盖 DataLayout，让 opt 从 target triple 获取布局；验证五种端序敏感常量折叠，并在 globaldce 移除私有数据后执行 codegen。

新增用例开发时只修正了 FileCheck 对现有 `.db` 地址物化拼写的匹配，以及 opt pass pipeline 的嵌套语法；没有削弱既有测试来制造绿色结果。

## 9. 复现命令

在 WSL 执行：

```sh
ninja -C /home/liu/build-mcs251 -j 4 llc opt TargetParserTests

/home/liu/build-mcs251/bin/llvm-lit -v \
  /mnt/c/Prj/LLVM/MCS251/llvm/test/CodeGen/MCS251

/home/liu/build-mcs251/unittests/TargetParser/TargetParserTests

python3 /mnt/c/Prj/LLVM/MCS251/validation/mcs251-endian/run.py \
  --baseline-llc /home/liu/mcs251-endian/baseline/llc

python3 /mnt/c/Prj/LLVM/MCS251/llvm/test/CodeGen/MCS251/Inputs/phase11-qemu.py \
  --work-dir /home/liu/mcs251-endian/phase11
```

不提供 `--baseline-llc` 时，新验证脚本仅要求四个正例通过；提供时，两个负对照必须精确出现预期失败。

## 10. 日志、产物与指纹

持久 WSL 工作目录：`/home/liu/mcs251-endian/`（完整生成输入、汇编、REL、HEX、map、链接日志、旧编译器和运行记录）。Windows 原始日志镜像：`transcripts/`（47 个原始日志/指纹文件）。

主要入口：`transcripts/lit.log`、`transcripts/targetparser.log`、`transcripts/matrix-run.log`、`transcripts/phase11-run.log`、`transcripts/source-sha256.json`、`transcripts/matrix/sha256.json`。

`source-sha256.json` 记录八个源文件的最终完整 SHA256；矩阵指纹记录验证脚本、三个 IR 输入、llc、opt、SDCC、assembler、linker、QEMU 及旧版 llc。最终运行后已逐项核对文件内容与指纹一致。

## 11. 交付状态

实施完成；新旧对照和双向互操作验收完成；原有 lit 与 Phase 11 全链回归完成；原始 transcript 和源指纹已持久化；AUDIT 与验收报告已由 PM 落盘；commit 由 PM 统一执行。
