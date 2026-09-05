# t4/timer0-irq-llvm — Timer0 溢出中断 × LLVM 编译 ISR 体（T4 升级用例）

验证"普通 LLVM 编译函数经 asm 向量 stub 充当 TF0 中断服务体"这一形态，
是 `../../t4-probes/ISR-STUB-VERDICT.md` 实测结论的固化回归。

## 形态（结论来自 QEMU transcript，非源码推断）

```
VEC 0xff000b:  ejmp isr_stub
isr_stub:      push PSW/ACC/B/R0-R7
               ecall _isr_body     ; 24 位调用，匹配 LLVM 函数的 ERET
               pop  R7-R0/B/ACC/PSW
               reti
```

设计文档 §2-T4 原假设 `lcall _isr_body; reti` 实测失败（LCALL 压 2 字节、
ERET 弹 3 字节，返回路径失衡），详见 verdict 文档。

## 文件

| 文件 | 作用 |
| --- | --- |
| `isr-body.ll` | 被测 LLVM 模块：`_isr_body` 自增 CNT(idata 0x30)、置 FLAG(0x31)、SBUF 打印 'I'；全部走固定地址 volatile 指针（T2 习语） |
| `vector-stub.asm` | HOME 复位 stub（7 字节，不盖 0xff000b）+ VEC 向量槽 + 保现场 stub + 主 harness（probe 风格：轮询+超时、PASS/FAIL spin） |
| `link.lk` | mcs251_ld.py 命令文件（无空行；HOME/VEC/PROBE/CSEG 基址 + 完整 ABI 签名） |
| `serial.expect` | 期望串口：`T4I0100KI0100K` + `PASS` |
| `build.sh` | 生产链构建运行判定：llc -filetype=obj → mcs251_ld.py --mcs251-abi → .hex → QEMU → transcript 判定 |

## 判定内容（transcript 解码 `T4 I 01 00 K I 01 00 K`）

- 相 1：T0 mode1、16 tick 溢出；ISR 进入（打印 'I'，证明 LLVM 体真实执行）、
  CNT≥1、TF0 入场自动清零（TCON=00）、main 的 r0=0x5A 哨兵跨中断完好
  （证明 stub 保恢复有效——LLVM 体实测会踩 r0/PSW）。
- 相 2：清零后再次放炮，CNT 恰好 =1、TF0 再次自动清零——证明重复进出栈
  平衡。确定性由"ISR 自清 TR0"保证：实测发现 TR0 保持时 QEMU 会在 main
  反应窗口内多次重触发 TF0（进入次数随时序 1–7 漂移），故由 ISR 体在
  置 FLAG 后立即 `TCON &= ~0x10`，每次武装恰一炮。

## 运行

```sh
cd validation/mcs251-demo-test/t4/timer0-irq-llvm
bash build.sh    # rc=0 且输出 PASS 为通过
```

环境变量可覆盖：`LLC` / `SDAS` / `MCS251_LD` / `QEMU` / `TIMEOUT`。
中间产物在 `build/`。harness 在 PASS/FAIL 后 spin，QEMU 被 timeout 杀
（rc 124）属预期，不参与判定。

## 已知约束（本用例遵守）

- HOME 复位 stub ≤11 字节（0xff000b 前；本用例 7 字节）。若改用 INT0
  向量 0xff0003 则 ≤3 字节，范本见 `t4-probes/d3_extint.asm`。
- 未开 ES，ISR 内打印 SBUF 无中断风暴风险；若未来加 UART IRQ，先读
  `t4-probes/README.md` 陷阱 5。
- 无 icount：定时器断言全部为"轮询 FLAG + 超时"与宽余量周期设计。
