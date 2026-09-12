# validation/mcs251-elf/runtime — 纯 ELF crt fixture（E4 chain-B；X4 扩展）

本目录存放纯 ELF32/MSB/ET_REL 的 MCS251 crt 对象：fixture 源、生成器与生成物。
原 ASxxxx 资产 `validation/mcs251-firmware/crt-selfstart.asm` 不修改、保持生产链
默认行为；本目录是 SPEC.md 第 9.3 节规定的独立 CRT ELF fixture。X4（xdata/code
切片）在两个 fixture 中各加入了 XDATA_INIT 遍历器（见下文 X4 节）。

## 文件

| 文件 | 说明 |
| --- | --- |
| `crt-selfstart.yaml` | 手工 YAML fixture（ELF32/MSB/ET_REL/EM 0x9999，含 RELA） |
| `gen-crt-elf.sh` | 生成器：冻结 yaml2obj 造对象 + 冻结 llvm-readobj 冒烟校验 |
| `crt.o` | 生成物（3096 字节；可用生成器随时重现） |
| `crt-xdata-init-walker.asm` | X4 XDATA_INIT 遍历器的 sdas251 汇编证据源（不参与链接） |
| `crt-irq.yaml` / `gen-crt-irq.sh` / `check-crt-irq.py` | T08 IRQ 模式 fixture（独立 checker 20 项断言，含 X4 遍历器） |

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
| ecall __mcs251_globals_init | `9A 00 00 00` | R_MCS251_24 @+3 | `9A FF 01 14`（X4 后 BOOT+0x18） |
| ecall __mcs251_xdata_init（X4） | `9A 00 00 00` | R_MCS251_24 @+0xC | 新增于 globals_init 与 _main 之间 |
| ecall _main | `9A 00 00 00` | R_MCS251_24 @+0x10 | `9A FF 02 00` |
| mov wr8/r12,#s_XINIT（.db 手工序言） | `7E 08 00 00` / `7A 0C 00 00` | MID8@+2、LO8@+3 / HI8@+3 | `7E 08 80 00` / `7A 0C 00 FF` |
| mov wr4,#l_XINIT | `7E 24 00 00` | R_MCS251_16 @+2 | `7E 24 00 00`（空 XINIT=0） |
| mov WTST/SBUF,#imm、sjmp、以及 XINIT 循环体 | 字面字节 | 无重定位 | 与 .lst 逐字节一致 |
| XDATA_INIT 遍历器（X4，见下节） | 见字节表 | MID8/LO8/HI8/R16 | crt-xdata-init-walker.asm gold |

ECALL=9A+addr24(大端)、EJMP=8A 家族、ERET=AA 另有 QEMU 实证：
`/home/liu/mcs251-models-probe/p2`（链接镜像 `9A FF 04 00`、`8A FF 01 19`）与
`p5`（ECALL/ERET 每层 3 字节返回帧）；`cmp spx` 编码 `BE F8 hh ll` 同见 p5。

## X4：XDATA_INIT 遍历器（两个 fixture 同步扩展）

X4 把 xdata/code 切片的链路闭合到运行时：llc 为 `__xdata` 对象发射
`.mcs251.XSEG.<符号>` NOBITS 节 + `.mcs251.xdata_init` v1 记录（X3），本 CRT 的
遍历器在 `__mcs251_globals_init`（内部 RAM XINIT）之后、`ecall _main` 之前消费
这些记录。插入点裁定：内部 RAM 先于 XDATA（与官方 Keil 启动序一致）。

**记录格式 v1（X3 冻结，DESIGN-SUPPLEMENT §7）**：`u8 bank + u16 window + u16
object_size + u16 payload_size + payload`，全大端。bank=canonical[23:16]（DPXL
要装的值），window=canonical[15:0]（DPH:DPL）。payload_size==0 表示"仅清零"
（清 object_size 字节）。

**遍历器行为**：空区（l_XDATA_INIT==0）在入口 cmp 后直接 ERET 跳过；每记录
DPXL←bank、DPH:DPL←window，payload_size 非 0 时逐字节 `movx @dptr,a` + `inc
dptr` 写载荷，为 0 时 `clr a` + `movx` 写 object_size 个零。**记录不跨 64K 窗、
目的不重叠由 lld validateXDATAInit() 门禁保证（fail-closed 在链接期）；CRT 不再
校验记录格式——硬件上无法报错，畸形记录的行为超出契约（本节即成文记录）。**

**DPXL 保持裁定（X4，引 DESIGN-SUPPLEMENT §3）**：遍历器后 **不恢复** DPXL=01h
复位值。§3 场景表明文"CRT 启动：无义务——生成代码不依赖复位值 01h，无置初值
动作（自愈式）"：每条 AS3 访问序列在 movx 前一步重设 DPXL，遍历器残留的最后
一个 bank 对 `_main` 无影响。DPS 复位值 0（crt-irq 另有显式 `mov DPS,#0`），
AU0/ID0/TSL 全 0，`movx @dptr` 不会自动增减或切换 DPTR——指针推进只有显式
`inc dptr`。

**字节表（98 字节，sdas251 gold：`sdas251 -l` 汇编 crt-xdata-init-walker.asm
的清单；QEMU 实证见 validation/mcs251-xdata-e2e 固件链）**：

| 指令 | 编码 | 证据 |
| --- | --- | --- |
| mov dr0[15:0],#window（.db 手工序言） | `7E 08 MM LL` | XINIT 序言同形；sdas251 `mov dr0,#imm16` gold |
| mov dr0[31:16],#00:bank（.db 手工序言） | `7A 0C 00 HH` | XINIT 序言同形（MOVH 通道） |
| mov wr4,#l_XDATA_INIT | `7E 24 ll ll` | .lst 同形（wr4=l_XINIT） |
| cmp wr4/wr12/wr16,#imm16 | `BE 24/64/84 ..` | .lst 同形 |
| je/sjmp 相对跳转 | `68 xx` / `80 xx` | .lst 同形；位移随块内重算 |
| mov r14,@dr0 / mov wr8/12/16,@dr0 | `7E 0B E0` / `0B 0A 40/60/80` | .lst 同形（XINIT 读头复用） |
| inc dr0 / inc dptr | `0B 0C` / `A3` | .lst 同形 / sdas251 gold |
| sub wr4,#0x0007 | `9E 24 00 07` | sdas251 gold（XINIT 为 …06） |
| mov dpxl,r14（DPXL←bank，SFR 0x84） | `7A E1 84` | sdas251 gold；X2 MOV8dpxl 家族（补充稿 §2 "7A 21 84" 同 opcode） |
| mov dph,r8 / mov dpl,r9 | `7A 81 83` / `7A 91 82` | sdas251 gold（mov direct,rN 家族） |
| mov a,r14 | `7C BE` | sdas251 gold（X2 MIR 同形） |
| movx @dptr,a | `F0` | sdas251 gold；X2 xdata-code-bytes.mir::movxast 同形 |
| clr a | `E4` | sdas251 gold |
| eret | `AA` | .lst/p5 QEMU 实证 |

遍历器重定位（walker 起点 B）：MID8@B+2、LO8@B+3、HI8@B+7（s_XDATA_INIT）、
R_MCS251_16@B+0xA（l_XDATA_INIT）。crt-selfstart.yaml B=BOOT+0x6E；
crt-irq.yaml B=BOOT+0xA0（check-crt-irq.py 20 项断言含遍历器模板与 DPL/DPH/DPXL
直写白名单）。

结构要点：

- VECS 按第 4.3 节 fragment 表达：`.mcs251.VECS.<n>.bytes`（PROGBITS，4 字节）
  与 `.mcs251.VECS.<n>.hole`（NOBITS，4 字节）按 header 序号交替，8 槽合计
  64 字节跨度、32 字节装载；洞不烧零。
- 符号：`__mcs251_selfstart_boot`(BOOT+0)、`__mcs251_globals_init`(BOOT+0x18)、
  `__mcs251_xdata_init`(BOOT+0x6E，X4)、`__mcs251_isr_unhandled`(BOOT+0xD0)
  为 defined GLOBAL；`_main` 为 undefined GLOBAL 供 lld 解析；
  `__mcs251_stack_base`（UNDEF 请求，触发第 6.3 节合成与栈门禁）、`s_XINIT`、
  `l_XINIT`、`s_XDATA_INIT`、`l_XDATA_INIT`（链接器边界符号，X4）为 undefined
  GLOBAL。符号一律 STT_NOTYPE，与 .rel 资产的无类型标签一致。
- `.note.mcs251.abi` 恰一个：namesz=7、descsz=32、type=1、owner `MCS251`，
  descriptor 八个大端 u32（SPEC 3.2）。
- 区域基址（HOME 0xff0000 / VECS 0xff0003 / BOOT 0xff0100 / XINIT 0xff8000）是
  链接参数（`--area-start`），绝不以 ET_REL sh_addr 偷渡（SPEC 3.1）。
- 旧 ASXXXX 的 J11 家族三字节暂存形态不复用：字段整体置零、RELA 提供全部
  地址字节（SPEC 7.1/7.3）。

## 生成与验收

```sh
# WSL Debian（固定纯净 PATH；build-mcs251 树现仅含 llc，yaml2obj/readobj 由
# lld 树供给——YAML2OBJ/READOBJ 环境变量覆盖）
export YAML2OBJ=/home/liu/build-mcs251-lld/bin/yaml2obj
export READOBJ=/home/liu/build-mcs251-lld/bin/llvm-readobj
/home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/gen-crt-elf.sh <out.o>
# 验收指纹
llvm-readobj --file-headers --sections --symbols --relocations <out.o>
# X4 遍历器编码证据（sdas251 gold 清单）
/home/liu/build-sdcc/bin/sdas251 -l -o /tmp/x.rel \
  /home/liu/LLVM_STC32/MCS251/validation/mcs251-elf/runtime/crt-xdata-init-walker.asm
# IRQ 模式 fixture 独立验收（20 项断言）
python3 check-crt-irq.py <crt-irq.o>
```

实测指纹（2026-09-07 建档，2026-09-12 X4 后复测；冻结 yaml2obj/llvm-readobj）：

- Header：`Class: 32-bit`、`DataEncoding: BigEndian`、`Type: Relocatable`、
  `Machine: EM_MCS251 (0x9999)`、`Flags: EF_MCS251_ABI_V1 (0x1)`、无 program headers。
- Sections：`.mcs251.HOME`(PROGBITS,3B)、`.mcs251.VECS.0..7.bytes`(PROGBITS,4B×8)、
  `.mcs251.VECS.0..7.hole`(NOBITS,4B×8，ALLOC|EXECINSTR)、`.mcs251.BOOT`
  (PROGBITS,0xD5=213B，X4 后含 XDATA 遍历器)、10 个 SHT_RELA、一个
  `.note.mcs251.abi`(52B)。
- Symbols：defined GLOBAL `__mcs251_selfstart_boot/__mcs251_globals_init/
  __mcs251_xdata_init/__mcs251_isr_unhandled`；undefined GLOBAL
  `_main/__mcs251_stack_base/s_XINIT/l_XINIT/s_XDATA_INIT/l_XDATA_INIT`；
  11 个 LOCAL 标签。
- Relocations：HOME 1 条（J16）、VECS 8 条（R24）、BOOT 12 条
  （R16×3、R24×3、MID8×2、LO8×2、HI8×2、加数全 0）。

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
