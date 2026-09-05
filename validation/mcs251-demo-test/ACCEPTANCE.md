# ACCEPTANCE.md -- MCS251 测试体系修复 复验收报告（终验）

- 日期：2026-09-05（复验收执行于 2026-09-05/06 夜间）
- 测试工程师：Sakuna（只读验收：不修改测试树、不 commit）
- 沙盒：`/home/liu/mcs251-sakuna-accept/`（旧证据未动，本轮新证据在 `reacceptance/`）
- 被测 runner（pristine）：`/mnt/c/Prj/LLVM/MCS251/validation/mcs251-demo-test/run-tests.py`
- 工具版本：
  - 新 llc：`/home/liu/build-mcs251/bin/llc`，md5 `f6c2f4034f9e32ee61ecfb690b238c93`（post-30a8af665）
  - 基线 llc：`/home/liu/mcs251-clang/bin-frozen/67a17057/llc`，md5 `67a170573cba625fe24441d0b6825d10`
  - QEMU（冻结）：`/home/liu/mcs251-clang/bin-frozen/6b9edfd0/qemu-system-mcs251`

## 总裁定：复验收 PASS

判定标准与实测：

| # | 判定项 | 标准 | 实测 | 结论 |
|---|--------|------|------|------|
| 1 | 全量主链（--no-ir-shims + 新 llc） | 19 PASS + 3 SKIP 口径正确 | 19/22 PASS（三边 serial md5 全等）+ 3 SKIP | 符合 |
| 2 | 真值重抽 | 抽验全符 | 5/5 案 all_match | 符合 |
| 3 | runner 机制 | Oracle-A 60s 超时在位；--case 正常 | 静态+行为双验证 OK | 符合 |
| 4 | 基线回归（旧 llc + 默认 shims） | 7 pilot = 7/7 | 7/7 PASS | 符合 |
| 5 | smoke（新 llc） | 双 PASS | SDCC reference PASS + LLVM replacement PASS | 符合 |

结论：修复体系达到验收口径，**建议立即翻转 IR_SHIMS 默认值为 False**（无阻塞项；翻转后建议按惯例复跑 smoke + 7 pilot 一次确认）。

## 矩阵 1：全量主链对账（--no-ir-shims + 新 llc，t1 全 22 案）

期望（PM 汇总）vs 实测逐案对账：19 案 PASS 清单与 3 案 SKIP 清单**逐案完全一致**。
全部 PASS 案 triangle_equal=True（Oracle-A == Oracle-B == DUT serial 逐字符相等）；
全部 Oracle-A serial md5 与上一轮一致（真值未漂移）；wildcard-recursive 由上轮挂死（空 serial）修复为本轮 PASS。

| 案例 | 期望 | 实测 | A md5(12) | B md5(12) | DUT md5(12) | tri |
|------|------|------|-----------|-----------|-------------|-----|
| adc-key-step | PASS | PASS | 763d265f92fa | 763d265f92fa | 763d265f92fa | T |
| decimal-length-parse | PASS | PASS | 7ad5066dd778 | 7ad5066dd778 | 7ad5066dd778 | T |
| hex-address-parse | PASS | PASS | fbeaf6184275 | fbeaf6184275 | fbeaf6184275 | T |
| keyboard-scan-step | PASS | PASS | 9dcd4d2c4ac5 | 9dcd4d2c4ac5 | 9dcd4d2c4ac5 | T |
| operator-precedence | PASS | PASS | b9cc3a2422ce | b9cc3a2422ce | b9cc3a2422ce | T |
| pilot-const-handle | PASS | PASS | ec2e11fa12b5 | ec2e11fa12b5 | ec2e11fa12b5 | T |
| pilot-delay-smoke (smoke) | PASS | PASS | - | e9faa75a0dfe | e9faa75a0dfe | - |
| pilot-judge-type | PASS | PASS | 8a037ca42b4f | 8a037ca42b4f | 8a037ca42b4f | T |
| pilot-led8-nibble | PASS | PASS | 0a09c0483230 | 0a09c0483230 | 0a09c0483230 | T |
| pilot-nec-decode | PASS | PASS | 2b7e4d92f527 | 2b7e4d92f527 | 2b7e4d92f527 | T |
| pilot-string-length | PASS | PASS | 7fb8aae24310 | 7fb8aae24310 | 7fb8aae24310 | T |
| pilot-sum-sfn | PASS | PASS | 73d9c7b2973e | 73d9c7b2973e | 73d9c7b2973e | T |
| pulse-width-step | PASS | PASS | 44ced401f3c4 | 44ced401f3c4 | 44ced401f3c4 | T |
| reverse16 | PASS | PASS | 7d78e022924e | 7d78e022924e | 7d78e022924e | T |
| reverse32 | PASS | PASS | 465429d6d933 | 465429d6d933 | 465429d6d933 | T |
| rtc-step | PASS | PASS | e60874e9a984 | e60874e9a984 | e60874e9a984 | T |
| task-dispatch | PASS | PASS | 55df3a031c95 | 55df3a031c95 | 55df3a031c95 | T |
| task-marks | PASS | PASS | a7a3f272a312 | a7a3f272a312 | a7a3f272a312 | T |
| wildcard-recursive | PASS | PASS | ba4b61ee84c1 | ba4b61ee84c1 | ba4b61ee84c1 | T |
| stack-packed | SKIP | SKIP（llc fatal） | 802ffecbaeda | 802ffecbaeda | - | - |
| stack-pop-packed | SKIP | SKIP（llc fatal） | 6412adce7cd9 | 6412adce7cd9 | - | - |
| temperature-lookup | SKIP | SKIP（llc fatal） | bf561fd81ae0 | bf561fd81ae0 | - | - |

SKIP 三案 DUT fatal 诊断（与 EXTRACT.md 记录一致，kernel 原样保留为后端验收样本）：
- stack-packed：`LLVM ERROR: Cannot select: ... i32 = mul nsw ...`
- stack-pop-packed：`LLVM ERROR: Cannot select: ... i32 = mul ...`
- temperature-lookup：`LLVM ERROR: Cannot select: ... i32 = mul ..., Constant:i32<10>`

三案 Oracle-A/Oracle-B 均正常（B serial md5 == A），符合 "SKIP-known-limitation" 口径。

## 矩阵 2：真值重抽（重点：temperature-lookup 闭环）

方法：沙盒内全新 `gcc -std=c99 -O0` 编译 host-main.c 产出独立真值 serial，与 wrapper.c 解析出的期望常量逐 tag 比对。
真值侧不取任何 DUT/llc 输出（防上轮"期望抄 DUT"式作弊）。脚本与结果：`reacceptance/truth_recheck.py`、`reacceptance/truth-build/truth-recheck.json`。

| 案例 | 抽验性质 | 结果 |
|------|----------|------|
| temperature-lookup | 上轮作弊案复核 | all_match=True：a FFFE / b FFFF / c 0000 / **d 028A** / e 0640 / **f 0001**，wrapper 期望与宿主 gcc 真值一致（与 EXTRACT.md 期望重导记录 0x0320→0x028A、0x000A→0x0001 吻合），主链 Oracle-B serial 同为 `BaFFFEbFFFFc0000d028Ae0640f0001PASS`，**闭环成立** |
| reverse32 | 改写案（Shizuku r2） | all_match=True（4/4 tag） |
| hex-address-parse | 改写案（Shizuku r2） | all_match=True（5/5 tag） |
| task-dispatch | 改写案（Shizuku r2） | all_match=True（8/8 tag） |
| adc-key-step | 上轮已抽 4 案复查 | all_match=True（14/14 tag） |

## 矩阵 3：runner 机制复核

- 静态：pristine runner `ORACLE_A_TIMEOUT = 60`；Oracle-A 以 `Popen(start_new_session=True)` 启动，`wait(timeout=60)` 超时后 `os.killpg(SIGKILL)`，状态记 `run-failed timeout=60s`（run-tests.py L71、L399-415）。
- 行为：沙盒假案 host-main 死循环实测——60.1s 精确掐断、结果状态正确、无孤儿进程残留（`reacceptance/timeout-selftest.log`、`reacceptance/mechcheck/`）。
- `--case`：矩阵 4（7 次单案）与矩阵 5（2 次单案）实际行使，结果落盘正常（results.<case>.json）。

## 矩阵 4：基线回归（旧 llc 67a17057 + 默认 shims，7 pilot）

7/7 PASS，serial md5 与主链一致（如 pilot-led8-nibble DUT 0a09c0483230）。
翻转决策的对照基线成立：翻转前默认链路无回归。证据：`reacceptance/baseline-pilots-reacc.log`、`reacceptance/build-base-shim/`。

## 矩阵 5：默认配置现状记录（默认 IR_SHIMS=True + 新 llc，2 案）

pilot-led8-nibble、reverse16 均 DUT `mcs251_ld failed rc=2`：
新 llc 已按 SDCC 命名契约给 C 符号自动加 `_` 前缀，符号垫片（SHIM 1）再叠一层导致 `__B_IR_Press`/`__reverse16` 双下划线，链接期 Undefined Global。
垫片有害现状仍在（且较上轮提前死于链接），翻转动机证据成立。证据：`reacceptance/shim-harm-reacc.log`、`reacceptance/build-main-shim-recheck/*/dut.mld.log`。

## 矩阵 6：smoke 独立复跑（新 llc）

双 PASS：`SDCC reference: PASS`（BPASS）+ `LLVM replacement: PASS`（BPASS，零文本加工，新命名契约 `_mcs251_probe` 由 llc 原生产出）。
证据：`reacceptance/smoke-reacc.log`、`reacceptance/smoke-build/`。

## 矩阵 7：快照

`/home/liu/mcs251-sakuna-accept/case-snapshot-final.json`：t1 可见 22 案（19 PASS + 3 SKIP + tri 口径）、
t2/t4 可见性现状、runner 机制状态、IR_SHIMS 默认值与翻转状态、四项辅助矩阵结论。

## 遗留问题清单（不阻塞本轮验收）

1. **t2/t4 可见性**：t2 盘上 4 目录（display-table/gpio-readback/sfr-uart-tx/uart-echo-poll）runner 可见 0；t4 isr-stub-exp 可见 0。属各 owner 的实例化排队，非本轮修复范围。
2. **SKIP 池 3 案**（temperature-lookup/stack-packed/stack-pop-packed）：等后端 `i32 mul/div` ISel 落地后回捞；kernel 已按 PM 口径原样保留为后端验收样本。
3. **断线僵尸进程污染共享构建的体系风险**（datalambda 幽灵教训）：runner 已修 Oracle-A 超时+进程组击杀，但多会话共用同一持久 BUILD 目录仍无互斥。建议后续给 runner 加构建目录独占/锁（flock 或 per-session BUILD），本轮仅记录不实施。
4. IR_SHIMS 默认值仍为 True：按流程属 PM 复验收通过后的翻转动作，不在本轮范围。

## 证据文件清单（均在 /home/liu/mcs251-sakuna-accept/reacceptance/）

- main-chain-noshim.log / build-main-noshim/（矩阵 1 全量 22 案，含逐案 serial）
- results-table-reacc.json / collect_reacc.py（逐案对账表生成）
- truth_recheck.py / truth-build/（矩阵 2 真值重抽：5 案 gcc 真值 + truth-recheck.json）
- timeout-selftest.log / mechcheck/（矩阵 3 超时行为自测）
- baseline-pilots-reacc.log / build-base-shim/（矩阵 4 基线 7/7）
- shim-harm-reacc.log / build-main-shim-recheck/（矩阵 5 垫片有害现状）
- smoke-reacc.log / smoke-build/（矩阵 6 smoke 双 PASS）
- skip-llc-fatal-excerpts.log（SKIP 三案 fatal 摘录）
- snapshot_final.py（矩阵 7 快照生成）
- 快照：/home/liu/mcs251-sakuna-accept/case-snapshot-final.json
