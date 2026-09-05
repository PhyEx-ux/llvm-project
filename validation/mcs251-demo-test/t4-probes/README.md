# t4-probes — QEMU 行为假设保护探针（T4 基线）

来源：`/home/liu/mcs251-qemu-periph/probes/`（Moka 外设调查，2026-09-05），
10 个纯 sdas251 汇编 probe **逐字节原样**入库（md5 校验一致，未增删任何字节；
全部说明以源码头注释为准）。用途见 `../DESIGN.md` §2-T4：保护"测试体系对
QEMU stc32g144k246 行为的假设"不被 QEMU 升级无声破坏。

## 运行

```sh
cd validation/mcs251-demo-test/t4-probes
bash build-all.sh          # 全量：汇编→链接→QEMU→transcript 判定
```

环境变量可覆盖工具路径：`SDAS` / `SDCC` / `QEMU` / `TIMEOUT`（默认 5 秒）。
中间产物在 `build/`（可随时删除）。T4  qualification 跑分使用冻结二进制
`/home/liu/mcs251-demo-test-t4/bin-frozen/`（llc md5 09c438e6…，qemu md5
6b9edfd0…，2026-09-05 冻结）。

## 判定规则（transcript 是唯一 oracle）

每个 probe 的 `<name>.expect` 存两行：第 1 行 = 串口证据行 pattern
（`X` = 时序相关、逐 run 可变的十六进制位），第 2 行 = `PASS`。判定：
串口首行全匹配 pattern（逐字符，`X` 通配任意单字符）+ 存在独立 `PASS` 行
+ 全文无 `FAIL`。probe 在 PASS/FAIL 后原地 spin，被 timeout 杀掉（rc 124）
是**预期**，不参与判定。QEMU 自身的 stderr 行（`qemu-system-mcs251: …`）
在判定前剥离。

## probe 一览

| probe | 断言内容 | 期望串口 pattern | 链接参数 / 备注 |
| --- | --- | --- | --- |
| a_timer | T0/T1 计数·溢出·停走·mode2 重载·AUXR 复位值·TM0PS 锁存 | `a01101234FX56A57` | X = mode2 重载后 TL0 低半字节（断言范围 [0xF0,0xFF]，实测 F5/F6/F9/FA/FB） |
| b_gpio | P0–P7 四模式（输入/推挽/开漏/准双向）读回语义 + 复位值 | `bFFFF001FF25A3A543C5C36` | 固定 |
| c_uart234 | UART2/3/4 缺席（写特征值读回 0）+ UART1 SCON/SBUF 对照 | `c000010000200003504U5` | 固定 |
| d1_tint | TF0 中断：向量 0xFF000B、ISR 计数、入场自动清 TF0 | `d1I0100K` | `-b VEC=0xff000b -b PROBE=0xff0100` |
| d2_uint | UART1 TI 中断：向量 0xFF0023、ISR 自清 TI、无中断风暴 | `d2xU0100K` | `-b VEC=0xff0023 -b PROBE=0xff0100` |
| d3_extint | INT0：P3.2 软件下降沿触发、向量 0xFF0003、自动清 IE0 | `d3E0101K` | `-b VEC=0xff0003 -b PROBE=0xff0008`；HOME 复位 stub 仅 2 字节 `ljmp 0x0008`（INT0 向量距入口 3 字节），是 HOME≤3 字节约束的范本 |
| d4_t1int1 | TF1（0xFF001B）+ INT1（0xFF0013，P3.3 下降沿）双中断 | `d4J007F048` | `-b VEC1=0xff0013 -b VEC2=0xff001b -b PROBE=0xff0100` |
| e_absent | ADC/WDT SFR + 未映射 XFR（0x7EFE80）：写后读回 0、无总线错误 | `e0000001002004` | 固定 |
| f_t234_presc | T2/T3/T4 SFR 全缺席 + TM0PS 预分频真实生效（同指令流计数比值） | `f1XXXXXXXX2` | 基线 4 位 + 分频后 2×2 位均为墙钟时序相关（观测 0101/0202 等）；断言为区间比值，非精确值 |
| g_uartrx | UART1 RX：REN 轮询 RI，收到管道注入字节 | `g5A1` | 运行时需 `printf "Z" \| qemu …`（build-all.sh 已内置） |

## 陷阱（实测踩过，改动本目录前必读）

1. `sdcc` 驱动链接失败（ASlink error）时**退出码仍可能为 0**——build-all.sh
   因此显式检查 `.hex` 是否生成，不能信 rc。
2. `-Wl-b AREA=0xaddr` 必须以**含空格的单个 argv** 传给 sdcc；拆成两个参数
   会触发 ASlink error 119 且 rc=0（见上）。
3. QEMU 的 MCS-251 loader 只认 `.hex` 后缀；sdld/sdcc 可能产出 `.ihx`，
   必须复制/重命名（本目录走 sdcc 驱动直接出 `.hex`）。
4. 向量区重叠：HOME 复位 stub 超过 7 字节会盖住 TF0 向量 0xFF000B；
   INT0 向量 0xFF0003 距复位入口仅 3 字节（d3 的 2 字节 `ljmp 0x0008` 是范本）。
5. ES 使能期间任何 SBUF 打印都会再触发 UART IRQ（d2 的 ISR 自清 TI + 主程序
   打印前关 IE 是范本）；SBUF 打印自身置 TI，先打印再读 SCON 会被污染。
6. 无 icount：虚拟时钟 = host 墙钟。定时器断言一律"轮询 + 超时"或
   "同指令流比值"，transcript 里凡是 tick 计数的位都标 X。
7. XFR 窗口（TM0PS 等）须先 `P_SW2.EAXFR=1`（SFR 0xBA 写 0x80），否则
   0x7E0000 整窗静默 0。
8. sdcc 驱动链接纯 asm 模块需要源内自带 `.area XSEG (XDATA)` /
   `.area PSEG (PAG,XDATA)` 空 stub；`.optsdcc` 行是 ABI 契约，逐字节保留。

## 复跑记录

- 2026-09-05（入库当日，冻结 QEMU 6b9edfd0…）：10/10 PASS，期望值即由该轮
  transcript 固化；a_timer / f_t234_presc 的可变位由多轮（≥3 次/个）串行
  transcript 逐位比对确定。
