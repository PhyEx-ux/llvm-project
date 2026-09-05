# selfstart-smoke — crt-selfstart.asm / link-selfstart.lk 验收证据用例

针对 `../crt-selfstart.asm` + `../link-selfstart.lk`（Step 4a 自有启动资产）
的验收证据。两个用例都链接**未经修改**的库资产，只换 `_main` 模块：

| 用例 | _main 行为 | 期望 transcript | 验证点 |
| --- | --- | --- | --- |
| smoke1 | 打印 'M' 后返回 | `MS` | 复位入口、SPX 初始化、boot→main 的 ECALL/ERET 往返、post-main 'S' 标记 |
| smoke2 | 打印 'M'、武装 T0（mode1 全量程 65536 tick、EA\|ET0）、返回，不链任何 TF0 处理器 | `MS!` | 同上 + 默认向量兜网：TF0 溢出落 VECS 0xFF000B 槽 → `isr_unhandled` 打印 '!' |

运行：`bash build.sh`（rc=0 且输出 ALL PASS 为通过）。工具路径可用
`LLC`/`SDAS`/`MCS251_LD`/`QEMU`/`TIMEOUT` 环境变量覆盖；中间产物在
`build/`。

注意：smoke2 里 '!' 前的 'MS' 依赖"全量程计数值 ≫ main 执行时长"这一
宽余量关系（实测主函数 ~20 周期 vs 溢出 ~786K 周期），无时序敏感性。
