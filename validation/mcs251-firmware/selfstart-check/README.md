# selfstart-check — harness-llvm 判定协议验收证据（Step 4b）

验证 `../harness-llvm.ll` + `../harness-llvm-cells.asm`（叠在
`../crt-selfstart.asm` 之上）的判定协议与 C 版 harness-template.c
逐字节一致。四个用例，镜像 = crt-selfstart + cells + harness-llvm +
各例 `_main`：

| 用例 | 期望 transcript | 验证点 |
| --- | --- | --- |
| pass | `BrwdPASS\n` | 三路 check（u8/u16/u32）成功静默；got 来自真实函数调用+ALU；`_harness_pass` 打印停机 |
| fail8 | `BFAIL expected=0xA5 got=0x00\n` 后停机 | u8 失败格式 + 停机 |
| fail16 | `BFAIL expected=0x1357 got=0x0000\n` 后停机 | u16 四位宽度格式 |
| fail32 | `BFAIL expected=0x12345678 got=0x00000000\n` 后停机 | u32 八位大端显示顺序（端序修复后基线） |

运行：`bash build.sh`（rc=0 且 ALL PASS 为通过；`LLC`/`SDAS`/`MCS251_LD`/
`QEMU`/`TIMEOUT` 可覆盖；中间产物在 `build/`）。

## 调用约定（T1 wrapper 迁移接口，Shizuku 用）

```llvm
@_harness_expect8  = external global i8     ; idata 0x60
@_harness_expect16 = external global i16    ; idata 0x61
@_harness_expect32 = external global i32    ; idata 0x63
declare void @_harness_check_u8(i8)
declare void @_harness_check_u16(i16)
declare void @_harness_check_u32(i32)
declare void @_harness_pass()               ; prints "PASS\n", never returns

; per checkpoint:
store volatile i8 66, ptr inttoptr(i32 153 to ptr)   ; marker letter (optional)
store volatile i8 165, ptr @_harness_expect8         ; expected FIRST
%g = call i8 @kernel_under_test()                    ; got SECOND (single arg)
call void @_harness_check_u8(i8 %g)
```

规则：
- **expected 先行**：先存 expect 单元再产生/传 got——check 函数内部从单元
  读 expected，与 C 版 `harness_check_uN(expected, got)` 实参序对应。
- got 必须单参可传（后端多参未实现）；多参 kernel 先经 T1 改写层收敛。
- IR 形态避开已探明缺口：i8/i16 移位与 i32 lshr/ashr 不可选（用减计循环
  代替）；`inttoptr(` 无空格；块标签前必须有终结指令（无 fall-through）。
- expect 单元地址（0x60-0x66）固定，避开寄存器 banks（0x00-0x1F）与
  t4-probes 的 0x30-0x34 约定区。
