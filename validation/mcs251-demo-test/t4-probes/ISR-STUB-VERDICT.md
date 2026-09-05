# ISR-STUB-VERDICT — "普通 LLVM 函数当中断服务体"的形态实测结论

日期：2026-09-05（Moka）。对象：QEMU stc32g144k246（冻结二进制
`qemu-system-mcs251` md5 6b9edfd0be5618a466c846df0f488faa，llc md5
09c438e632489efc5df478757893e72a，冻结于
`/home/liu/mcs251-demo-test-t4/bin-frozen/`）。

**结论：设计文档 §2-T4 的原假设 `vector_stub: lcall _isr_body; reti` 被
QEMU transcript 证伪。可用的最小形态是 `ecall _isr_body; reti`；交付形态
是"向量槽 ejmp → asm stub 保存现场 → ecall LLVM 函数 → 恢复现场 → reti"。**
一切结论均来自串口 transcript（本目录 `isr-stub-exp/*.transcript.txt`），
无源码推断成分。

## 1. 被测问题

§2-T4 假设：TF0 向量槽放 `lcall _isr_body; reti`，其中 `_isr_body` 是普通
LLVM 编译函数——"lcall 压调用帧、ERET 弹调用帧、reti 弹硬件中断帧"。

前提事实（llc 输出实测）：此后端的函数返回指令是 **ERET**（24 位弹栈）。
本次被测体 `isr-stub-exp/isr_body.ll`（自增 CNT=idata 0x31、置 FLAG=0x30、
TCON&=~0x10 停 T0、SBUF 打印 'I'）编译为：

```
_isr_body:
	mov r0, 0x31
	add r0, #0x01
	mov 0x31, r0
	mov r0, #0x01
	mov 0x30, r0
	mov r0, 0x88
	anl r0, #0xef
	mov 0x88, r0
	mov r0, #0x49
	mov 0x99, r0
	eret
```

即：体会踩 **r0 与 PSW 标志位**（`add`/`anl`），且以 ERET 收尾。

## 2. 实验设计

被测体固定为上述 LLVM 函数；主程序为 d1_tint 式纯 asm harness（T0 mode1、
EA|ET0、轮询 FLAG+超时、打印 CNT 与 TCON 后 PASS/FAIL spin）。向量
0xFF000B（TF0）。只变向量/stub 形态：

| 形态 | 向量槽/stub | 检验点 |
| --- | --- | --- |
| A | `lcall _isr_body; reti` | 原假设 |
| A2（对照） | `lcall isr_asm; reti`，isr_asm 为 `ret` 收尾的纯 asm 叶函数 | 隔离 lcall 帧大小 |
| B | `ecall _isr_body; reti` | 24 位 call 对 ERET |
| C | `ejmp isr_stub`；stub: push PSW/ACC/B/R0–R7 → `ecall _isr_body` → pop → `reti`；main 持 r0=0x5A 哨兵跨中断 | 交付形态 + 寄存器破坏防护 |

链接：HOME=0xff0000、VEC=0xff000b、PROBE=0xff0100、CSEG=0xff0200
（LLVM 体与向量同 64K bank，LCALL 可达——exp_a.map 确认
`_isr_body` @ 0xFF0200）。

## 3. 结果（transcript 原文，timeout 杀 spin 为预期）

| 形态 | transcript | 判定 |
| --- | --- | --- |
| A | `vAI` 之后静默，5s 超时被杀 | **FAIL** |
| A2 | `v2i0100K` + `PASS` | PASS |
| B | `vBI0100K` + `PASS` | **PASS** |
| C | `vCI0100K` + `PASS`（r0 哨兵完好） | **PASS** |

证据行解读：`vA`/`vB`/`vC`/`v2` = main banner；`I`（大写）= LLVM 体打印、
`i`（小写）= asm 体打印；`01` = CNT=1（ISR 恰好进入一次，体自清 TR0 保证
单发）；`00` = 中断返回后 TCON（TF0 入场自动清零）；`K` = 校验通过标记。

## 4. 判定与机理

1. **形态 A 的失败点是返回路径，且唯一原因是 LCALL/ERET 帧长不匹配。**
   A 中 'I' 已打印：LCALL 正确抵达 LLVM 体且体执行到了最后一条 SBUF 写；
   随后主程序再未获得串口输出——ERET 弹了 3 字节而 LCALL 只压了 2 字节，
   返回地址混入硬件中断帧字节 → PC 跑飞。
   A2 对照（同一向量槽、同一 LCALL、仅换 `ret` 收尾的 asm 体）完整 PASS，
   证明 LCALL 的压栈是 2 字节、与 RET 配对正常，向量槽与寻址均无问题。
2. **形态 B 成立**：ECALL 压 24 位返回地址，与 ERET 的 24 位弹栈平衡；
   RETI 再弹硬件中断帧（PSW1+24 位 PC，见外设调查 §3）。CNT=01、TF0 自动
   清除均符合预期。
3. **形态 C 在 B 的基础上解决寄存器破坏**：LLVM 函数按普通 ABI 可随时
   clobber r0-r7/ACC/PSW（本被测体实测踩 r0+PSW）；main 无法感知异步
   中断，故 stub 必须代做 `__interrupt` 序言的工作。C 中 main 持
   r0=0x5A 哨兵跨中断并校验，PASS 证明保恢复正确。
4. 设计文档提到的另一替代形态"vector 直接放 ISR 入口 + ISR 尾部 reti"
   **与"普通 LLVM 编译函数"前提矛盾**（后端只会发 ERET，不会发 RETI），
   无需实测即排除；若未来后端支持 __interrupt 属性 lowering（直接生成
   保现场+RETI 的函数），该形态才重新有意义。

## 5. 附带实测发现（测试体系须知）

**TR0 保持时 TF0 会在 main 反应窗口内多次重触发。** 初版被测体不停 TR0，
main 从看到 FLAG 到 `anl TCON,#0xef` 之间，ISR 重复进入了 1~7 次
（逐 run 漂移），串口出现多个 'I'。定时器断言要么由 ISR 自清 TR0
（本交付形态），要么按多入口设计。该发现由"精确 transcript 比对"抓出，
同时抓出了初版 .ll 与 asm 间 FLAG/CNT 地址约定写反的问题（已修正：
FLAG=0x30、CNT=0x31，与 d1_tint 一致）。

## 6. 交付形态（写死为测试）

```
.area VEC (CODE)                 ; 0xff000b (TF0)
        ejmp    isr_stub
isr_stub:                        ; PROBE 区
        push PSW / ACC / B / r0-r7
        ecall   _isr_body        ; 24 位调用，匹配 ERET
        pop  r7-r0 / B / ACC / PSW
        reti
```

约束与注意：
- stub 必须保 PSW（`add`/`anl` 等改写标志位）；r8–r15 不在经典 bank0 组
  内，后端若在 ISR 体中使用需扩展 stub（当前被测体不用）。
- ECALL 可达全 24 位空间，LLVM 模块落任何 bank 均可（本测 CSEG
  0xff0200）；若坚持用 LCALL 则体必须与向量同 64K bank 且改 RET 收尾——
  即不再是"普通 LLVM 函数"，放弃。
- ES 使能期间打印陷阱、无 icount 轮询+超时等均与 probe 群一致。

## 7. 复现

```sh
cd validation/mcs251-demo-test/t4-probes/isr-stub-exp
bash reproduce.sh        # 期望：exp_a 静默超时；exp_a2/exp_b/exp_c PASS
```

交付用例（生产链 llc→mcs251_ld.py→QEMU，两相放炮+r0 哨兵）：
`validation/mcs251-demo-test/t4/timer0-irq-llvm/`。
