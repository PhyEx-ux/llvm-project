# RESULTS — demo×QEMU 测试体系首次全量（T1 pilot 批）

日期：2026-09-05（Shizuku）。runner：`run-tests.py --filter t1`。
结果 JSON：`/home/liu/mcs251-demo-test/build/results.json`。

## 工具链（全部冻结/固定）

| 工具 | 版本/校验 |
| --- | --- |
| llc（冻结副本） | LLVM 24.0.0git，md5 09c438e632489efc5df478757893e72a（/home/liu/mcs251-demo-test/bin-frozen/llc） |
| qemu-system-mcs251（冻结副本） | QEMU 11.1.0，md5 6b9edfd0be5618a466c846df0f488faa |
| clang | 19.1.7（Debian 系统包 /usr/bin/clang；用户拍板路线，clang19-IR→llc24 顺向 auto-upgrade 全程零摩擦） |
| gcc（Oracle-A） | 14.2.0 |
| sdcc/sdas251/sdld（Oracle-B） | SDCC 4.6.0 #16555 |
| 链接（DUT 生产链） | validation/mcs251-ld/mcs251_ld.py --mcs251-abi |

自建 clang 计划（rsync+cmake+ninja 脚本 /home/liu/mcs251-demo-test/
build-clang.sh）已按 PM 指令暂停，作为冒烟失败时的备胎保留。

## 结果总表（t1，2026-09-05 14:3x）

| 用例 | 类别 | Oracle-A | Oracle-B | DUT | 判定 |
| --- | --- | --- | --- | --- | --- |
| pilot-const-handle | 状态机/字符分类/switch 查找 | PASS | PASS | PASS | **PASS**（三方逐字符一致） |
| pilot-string-length | 指针(下标)遍历 | PASS | PASS | PASS | **PASS** |
| pilot-sum-sfn | 移位/数值计算（FAT SFN 校验和） | PASS | PASS | PASS | **PASS** |
| pilot-judge-type | 状态机/跨函数调用链 | PASS | PASS | PASS | **PASS** |
| pilot-led8-nibble | 主循环提取/移位+nibble 拆包 | PASS | PASS | PASS | **PASS** |
| pilot-delay-smoke | 编译+终止冒烟（空循环存活） | — | — | PASS | **PASS**（smoke） |
| pilot-nec-decode | ISR 状态机+移位解码 | PASS | PASS | 帧对/帧错 | **FAIL（已知差异，不绕过）** |

- 6/7 通过；全部 7 用例 **Oracle-A == Oracle-B 逐字符一致**（双 oracle
  层全绿——同一 kernel.c 在 gcc(host) 与 SDCC→sdld→QEMU 参考链行为相同）。
- Momo batch 提取的 15 个用例目录（kernel.c+EXTRACT.md）处于
  pending-instantiation，按 t1/TEMPLATE.md 实例化后入链。
- 每用例三方串口全文存于 /home/liu/mcs251-demo-test/build/<case>/
  {oracle-a,oracle-b,dut}.serial。

### nec-decode 已知差异详情（保持 FAIL，裁定不绕假 PASS）

- 期望（A==B）：帧1 `p01u3412kB5H34L12dB5` + 帧2 `p01u00FFk5AH00LFFd5A`
- 实际（DUT）：帧1 完整正确；帧2 `p01u0000kB5H34L12dB5`——UserCode 写 0
  （应 0x00FF）、IR_code store 未生效（保持帧1 值），同块连续赋值部分
  生效（B_IR_Press 置位成功）。变量地址（wrapper.lst）：_UserCode=u16
  @idata 0x000B、_IR_code=u8 @0x000A、_B_IR_Press=u8 @0x0009。
- 已排除（对照 probe /home/liu/mcs251-demo-test/probes/endianness/）：
  store i16 方向正确（0x00FF 精确回读）、u8 双向含 0xFF 正确、
  wrapper→kernel 的 load u8 0xFF 正确。疑点收敛于 kernel 内部 u8 轮转
  链 / shl 输入为 0 的加法路径，已打包给静默错码专项（Alice）。

## 发现坑清单（全部实测，含 probe）

### A. 后端能力缺口（llc 24，冻结版）

1. **移位 ISel 完全缺失**：lshr/shl/ashr × i8/i16/i32 × 常量/变量全部
   `Cannot select`（紧急级，已进冲刺排期）。mul、udiv 同样无选择。
   临时缓解：runner 的常量移位 lowering（见"可退役垫片"）。
2. **DAG combiner 折叠回移位（含静默错码形态）**：or/select（或 add/
   select）位重建链会被 SelectionDAG combiner 重新组合成 `srl`——要么
   ISel 直接失败，要么产生"可选择但语义错误"的形态（sum-sfn 曾输出
   0x60 而非 0xBC 的静默错码）。volatile alloca 往返是唯一可靠屏障。
   最小重现：probes/combiner-fold/plain-or-select-chain.ll（+llc.log）。
3. **defined global data 被拒**：`defined global data is not supported
   yet (it would be emitted into the CODE area CSEG; data-area support is
   Step 2 of the Phase 12 plan)`——kernel 一切全局（含 const 表）只能
   extern，定义放驱动侧。违反时 llc 直接报错（fail loud，良好）。
4. **br_jt（跳转表）无选择**：clang -O1 会把 if/else 链合成 switch、
   -O0 直接 lower IR switch，6-case 稀疏 switch 即生成 br_jt。llc 无
   `-disable-jump-tables` 选项（尝试即 unknown argument）。用例层规避：
   稀疏 switch 源级改 if/else（控制流同构）+ 全用例 -O0。
5. **8→16 位符号扩展缺失**：`sign-extending loads are not supported`——
   char 形参经 x86 前端 signext 即触发。用例层 char→u8（等价论证见
   各 EXTRACT.md）。

### B. 工具链行为（非后端）

6. **llc load i16 单向错码**：SDCC 写 u16（大端）→ llc load i16 读
   0x1212（应 0x1234，0x34 字节从未出现，疑同字节广播）；反向 store i16
   与 u8 双向（含 0xFF）全部正确。失败最小例：
   /home/liu/mcs251-demo-test/probes/endianness-fail/（Bl12h12PASS 复验）；
   对照版：probes/endianness/。用例层规避：16 位输入拆双 u8（led8）。
   已移交静默错码专项。
7. **c1mode 丢字符串字面量初始化**：`static const char A[2][8]={"","1"}`
   的数据在 asm 里全 0（显式数值/字符初始化正常）。Oracle-B 构建约束：
   固件侧数组一律显式初始化。

### C. 链与 QEMU 行为事实

8. **QEMU 0xFFxxxx 窗口数据读返回 0**：CONST 区默认顺排在 HOME 后
   （0xFF0001 起），hex 中字节存在但 CPU movc 读 0；执行（HOME 复位
   跳板取指）不受影响。0xFC 窗口正常。标准布线：lk 注入
   `-b CONST = 0xFC8000`（runner 已内置，probe Bk22PASS 验证）。
9. **SDCC mcs251 无 code 存储类关键字**：`code`/`static code char`
   语法错误（mcs51 同版本支持）；`__code` 可用但仍路由到 CONST 区。
   ROM 数据固定走 const 数组 + CONST 基址布线。
10. **sdld -r -nf 需要 .rel 旁置 .lst**（已知，沿用空文件凑数）；
    .lk 命令文件不能有空行（已知，模板遵守）。
11. **clang -O1 删除空循环**：delay_ms 在 -O1 下变空函数——smoke 用例
    的"空循环存活"回归必须 -O0（这也是全用例统一 -O0 的原因之一，
    另一条是 -O1 合成 switch）。
12. **C 整数提升产生 ashr**：`u8 x >> k` 提升为 signed int 后 clang 出
    ashr（非 lshr）；值域非负（源自 zext）下位级等价，移位 lowering
    按 lshr 处理（源操作数域已论证）。

### D. 测试体系经验

13. **NEC 帧间时序**：无帧间 idle 时，帧 2 的 SYNC 判定沿读到的
    SampleTime=尾段 11（进不了 [97,150) 窗）被静默丢弃，计数器中途回
    零，全部数据沿错位一格 → /data 校验失败无输出。160 tick 高电平
    idle 使该沿以 166>150 判错丢弃、计数器干净重启（gen-vectors.py
    注释有完整分析）。
14. **wrapper 期望值纪律**：先 Oracle-A（host 真值）后回填 wrapper 的
    harness_check；三角 diff 是主判定。绝不"看着 DUT 输出填期望"。

## 可退役垫片（PM 裁定 2026-09-05；--no-ir-shims 可整体关闭验证）

| 垫片 | 现状 | 退役条件 |
| --- | --- | --- |
| 符号适配 @name→@_name（run-tests.py mangle_ll_symbols） | 默认开 | fork-clang 原生 `_` 前缀符号（前端裁定） |
| 常量移位 lowering + volatile alloca 屏障（lower_constant_shifts） | 默认开 | 后端真实移位 ISel 落地（冲刺排期） |

`--no-ir-shims` 当前验证结果：sum-sfn llc failed（移位缺口仍在，
垫片仍必要）。定期重跑探测退役窗口。

## 与 DESIGN.md 的对照

- 分层/目录/runner 职责照 §3 落地；t2/t4 目录预留（runner 报 reserved）。
- T1 选案：7 个 pilot 覆盖 状态机×2 / 字符分类×2 / 查找(switch)×1 /
  移位解码×3 / 数值×1 / 指针(下标)遍历×2 / 主循环提取×1 / ISR 提取×1 /
  冒烟×1；路线 1/2/3 各有用例。函数指针/递归类未进首批（L0 清单无直接
  候选，待 T3 矩阵按 Momo 数据反查扩容）。
- 双 oracle 三角判定 §2-T1 全链落地并工作。
