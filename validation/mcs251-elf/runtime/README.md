# validation/mcs251-elf/runtime — 纯 ELF crt fixture（E4 chain-B）

本目录存放纯 ELF32/MSB/ET_REL 的 MCS251 crt 对象：fixture 源、生成器与生成物。
原 ASxxxx 资产 `validation/mcs251-firmware/crt-selfstart.asm` 不修改、保持生产链
默认行为；本目录是 SPEC.md 第 9.3 节规定的独立 CRT ELF fixture。

## 文件

| 文件 | 说明 |
| --- | --- |
| `crt-selfstart.yaml` | 手工 YAML fixture（ELF32/MSB/ET_REL/EM 0x9999，含 RELA） |
| `gen-crt-elf.sh` | 生成器：冻结 yaml2obj 造对象 + 冻结 llvm-readobj 冒烟校验 |
| `crt.o` | 生成物（2696 字节；可用生成器随时重现） |

## 路线裁定：yaml2obj 手造（路 A），不采用 C + fork-clang（路 B）

选路 A 的理由（路 B 经评估被结构性排除）：

1. llc v1 明确不开放显式 IR section（SPEC 第 8 节），C 代码无法落进
   `.mcs251.HOME / .mcs251.VECS.* / .mcs251.BOOT` 三个固定区域；crt 的三段布局
   是语义的一部分，无法从 C 表达。
2. XINIT 遍历器依赖 WR/DR 寄存器操作码（`7E 08 …`/`0B 0A 40 …`/`7D A4 …`），
   llc 现有 C 后端不会发射这些形态；也没有等价的可移植 C。
3. 本目标没有 AsmParser（llvm-mc 不支持 MCS251 汇编），inline asm 无法桥接；
   SPEC 亦要求 ELF 分支不吞 raw text。
4. SPEC 9.3 本身即规定"E3 在 validation/mcs251-elf/runtime/ 新增独立的、可复现的
   CRT ELF fixture 源（LLVM YAML，yaml2obj 生成）"。路 A 是规范内路线。

## 编码依据（逐字节）

机器字节转录自冻结资产及其汇编清单
`validation/mcs251-firmware/selfstart-smoke/build/crt-selfstart.lst`（同目录
`crt-selfstart.rel` 为旧重定位形式），链接后字节与
`selfstart-smoke/build/smoke1.hex` 黄金镜像逐项对上：

| 指令 | 字节（占位） | ELF RELA | 链接后实例（黄金 HEX） |
| --- | --- | --- | --- |
| ljmp __mcs251_selfstart_boot | `02 00 00` | R_MCS251_J16 @+1 | `02 01 00`（同 bank 16 位） |
| ejmp __mcs251_isr_unhandled ×8 | `8A 00 00 00` | R_MCS251_24 @+1 | `8A FF 01 6A` |
| mov spx,#__mcs251_stack_base | `7E F8 00 00` | R_MCS251_16 @+2 | `7E F8 01 0F`（SPX=0x10f） |
| ecall __mcs251_globals_init | `9A 00 00 00` | R_MCS251_24 @+3 | `9A FF 01 14` |
| ecall _main | `9A 00 00 00` | R_MCS251_24 @+7 | `9A FF 02 00` |
| mov wr8/r12,#s_XINIT（.db 手工序言） | `7E 08 00 00` / `7A 0C 00 00` | MID8@+2、LO8@+3 / HI8@+3 | `7E 08 80 00` / `7A 0C 00 FF` |
| mov wr4,#l_XINIT | `7E 24 00 00` | R_MCS251_16 @+2 | `7E 24 00 00`（空 XINIT=0） |
| mov WTST/SBUF,#imm、sjmp、以及 XINIT 循环体 | 字面字节 | 无重定位 | 与 .lst 逐字节一致 |

ECALL=9A+addr24(大端)、EJMP=8A 家族、ERET=AA 另有 QEMU 实证：
`/home/liu/mcs251-models-probe/p2`（链接镜像 `9A FF 04 00`、`8A FF 01 19`）与
`p5`（ECALL/ERET 每层 3 字节返回帧）；`cmp spx` 编码 `BE F8 hh ll` 同见 p5。

结构要点：

- VECS 按第 4.3 节 fragment 表达：`.mcs251.VECS.<n>.bytes`（PROGBITS，4 字节）
  与 `.mcs251.VECS.<n>.hole`（NOBITS，4 字节）按 header 序号交替，8 槽合计
  64 字节跨度、32 字节装载；洞不烧零。
- 符号：`__mcs251_selfstart_boot`(BOOT+0)、`__mcs251_globals_init`(BOOT+0x14)、
  `__mcs251_isr_unhandled`(BOOT+0x6A) 为 defined GLOBAL；`_main` 为 undefined
  GLOBAL 供 lld 解析；`__mcs251_stack_base`（UNDEF 请求，触发第 6.3 节合成与
  栈门禁）、`s_XINIT`、`l_XINIT`（链接器边界符号）为 undefined GLOBAL。
  符号一律 STT_NOTYPE，与 .rel 资产的无类型标签一致。
- `.note.mcs251.abi` 恰一个：namesz=7、descsz=32、type=1、owner `MCS251`，
  descriptor 八个大端 u32（SPEC 3.2）。
- 区域基址（HOME 0xff0000 / VECS 0xff0003 / BOOT 0xff0100 / XINIT 0xff8000）是
  链接参数（`--area-start`），绝不以 ET_REL sh_addr 偷渡（SPEC 3.1）。
- 旧 ASXXXX 的 J11 家族三字节暂存形态不复用：字段整体置零、RELA 提供全部
  地址字节（SPEC 7.1/7.3）。

## 生成与验收

```sh
# WSL Debian（固定纯净 PATH）
export PATH=/home/liu/build-mcs251/bin:$PATH
/mnt/c/Prj/LLVM/MCS251/validation/mcs251-elf/runtime/gen-crt-elf.sh <out.o>
# 验收指纹
llvm-readobj --file-headers --sections --symbols --relocations <out.o>
```

实测指纹（2026-09-07，冻结 yaml2obj/llvm-readobj，LLVM 24.0.0git）：

- Header：`Class: 32-bit`、`DataEncoding: BigEndian`、`Type: Relocatable`、
  `Machine: EM_MCS251 (0x9999)`、`Flags: EF_MCS251_ABI_V1 (0x1)`、无 program headers。
- Sections：`.mcs251.HOME`(PROGBITS,3B)、`.mcs251.VECS.0..7.bytes`(PROGBITS,4B×8)、
  `.mcs251.VECS.0..7.hole`(NOBITS,4B×8，ALLOC|EXECINSTR)、`.mcs251.BOOT`
  (PROGBITS,0x6F=111B)、10 个 SHT_RELA、一个 `.note.mcs251.abi`(52B)。
- Symbols：defined GLOBAL `__mcs251_selfstart_boot/__mcs251_globals_init/
  __mcs251_isr_unhandled`；undefined GLOBAL `_main/__mcs251_stack_base/s_XINIT/
  l_XINIT`；7 个 LOCAL 标签。
- Relocations：HOME 1 条（J16）、VECS 8 条（R24）、BOOT 7 条
  （R16×2、R24×2、MID8/LO8/HI8、加数全 0）。

## 用途与边界

- e4 chain-B 沙盒工作副本：`/home/liu/mcs251-elf-e4/objects/elf/crt.o`
  （`run-chain-b.sh` 的 `CRT_ELF` 默认路径）。
- 本对象是可重定位输入，不可直接加载；不携带任何器件名字符串。
- 与旧 crt 的结构对照以 `crt-selfstart.rel/.lst` 与 `smoke1.hex` 为指纹基准；
  如需修改启动语义，先按 SPEC 9.3 报 PM，不暗中改 fixture。

## 验收记录 2026-09-07

MCS251 除法/取余运行时算术库（八个 ELF32BE `.o` + 显式清单）完成 QEMU 级验收，
**总裁决 8/8 门 PASS**（裁决书与五节报告见 WSL 证据目录
`/home/liu/mcs251-rt-acceptance/verdicts/`：`VERDICTS.txt`、`acceptance-report.md`）。
依据：除法设计 v4 §8 + SPEC 2026-09-07 修正案 §3/§6。

- 对象级门（§8.6）：8 对象符号/`_PARM_2` 槽（2B/4B、STT_OBJECT、叶 OSEG/非叶 DSEG）、
  RELA、清单 8 行行序合规；链接负例（重复定义/抽走对象）均以正确诊断报错。
- QEMU 语义门（§8.2/§8.4）：`rt_fw.serial` vs `host.expected` 680 语义行逐字节一致
  （85 向量 × 8 操作，边界表全覆盖；终止行各为 RT-SEMANTIC-PASS/HOST-EXPECT-DONE 属收尾标记）。
- Oracle-B 三方门（§8.5）：宿主 int64 / 自研运行时 / SDCC 4.6.0 mcs251-large 库目标码
  三方 **680/680 零差异**；八 helper 供给独立性归档（`oracle/sdccb_supply.txt`）。
- UB 隔离门（§8.3）：独立固件独立会话，IR 探针（UBIR-）与 helper 行为观察（UBH-）分标，
  只记录不设契约（`qemu/ub_ob.serial`，209B/9 行基准建档）。

证据根：`/home/liu/mcs251-rt-acceptance/`（audit/ir/asm/obj/readobj/qemu/oracle/verdicts）。
等级声明：QEMU 证据，非真机；§8.8 优化级审计归档与 temperature-lookup 回归等退出项
未在本收尾会话复验，见报告 §6。产品源码零改动。
