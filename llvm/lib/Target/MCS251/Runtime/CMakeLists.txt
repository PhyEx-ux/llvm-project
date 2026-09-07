#===-- CMakeLists.txt -----------------------------------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===-----------------------------------------------------------------------===#
#
# mcs251rt —— MCS251 除法/取模运行时的独立显式 CMake target（定义草案）。
#
# 规范依据：
#   - 除法设计 v4 §7.6、§10-Q3、§11.2
#     (validation/mcs251-models/proposals/SIGNED-DIV-REMAINDER-DESIGN.md)
#   - SPEC 2026-09-07 修正案 §2.3、§2.4、§3 (validation/mcs251-elf/SPEC.md)
#
# 红线（逐条对应规范）：
#   1. mcs251rt 不进入任何 LLVM 库或工具的链接图（修正案 §2.4、§11.2-3）：
#      本目录产物禁止出现在任何 target_link_libraries 的 clang/llc/LLVM 支持
#      库一侧；两阶段顺序只允许用构建依赖（DEPENDS / add_dependencies）表达，
#      防止 llc -> Runtime -> llc 环。
#   2. 两阶段依赖（§11.2-1/2）：第一阶段常规构建产出 clang 与 llc；本 target
#      以同一构建树刚产出的工具链编译八源。
#   3. 三步管道（§11.2-2）：clang -S -emit-llvm 产 IR -> 对该批 .ll 原件审计
#      -> 同一批 .ll 原样交 llc 产 ELF 对象。禁止 clang -c 直产 .o（那已是
#      目标码，不是 llc 的 IR 输入）。
#   4. 交付形态 = 八个独立 ELF32BE ET_REL .o + 显式清单（修正案 §2.1）。清单
#      是 mcs251rt 的声明产物、纳入构建依赖链（R3，见下）。不产出 .a：
#      lld MCS251 只收单对象，SPEC §3.5 明确排除 archive。
#
#===-----------------------------------------------------------------------===#
#
# 【R1 接入时机与工具 target 可见性】（Alice 复审修正，2026-09-07）
#
# 本树配置顺序：llvm/CMakeLists.txt 先处理 lib（MCS251 后端在此配置），后处理
# tools（clang/llc target 在 tools 阶段声明）。CMake 的 `if(TARGET ...)` 只
# 检查本次配置**已声明**的 target，与磁盘文件存在性无关——因此若从后端目录
# 直接 add_subdirectory(Runtime)，配置期检查 TARGET clang/llc 必然误报
# "not found"。草案按接入形态自动分派，两种形态都不改变红线：
#
#   形态 A（首选）：由顶层在 tools 处理**之后**接入——
#       llvm/CMakeLists.txt 末尾（tools 子目录处理之后）追加：
#         option(LLVM_MCS251_ENABLE_RUNTIME
#                "Build the mcs251rt division and modulo runtime objects" OFF)
#         if(LLVM_MCS251_ENABLE_RUNTIME)
#           add_subdirectory(lib/Target/MCS251/Runtime)
#         endif()
#     此时 in-tree clang/llc target 已在配置图中：本文件自动改用
#     $<TARGET_FILE:clang|llc> 引用同一构建树的第一阶段产物，并把这两个
#     target 名放进 DEPENDS，构建顺序由依赖图表达。
#
#   形态 B：仍从 llvm/lib/Target/MCS251/CMakeLists.txt 发起
#     add_subdirectory(Runtime)（lib 阶段，TARGET clang/llc 尚未声明）——
#     此时必须在配置时给缓存变量 MCS251RT_CLANG / MCS251RT_LLC 传第一阶段
#     二进制的绝对路径（配置期校验存在性）；该路径同时进入文件级 DEPENDS，
#     工具更新后受影响的 .ll/.o 自动重生成（增量失效传播）。文件依赖与
#     身份冻结审计（§6.1/§11.2-6）互补、不互替；两阶段先后顺序仍由构建者
#     保证：先完成第一阶段构建，再构建 mcs251rt。
#
#   形态 A/B 均不产生任何链接依赖；mcs251rt 始终是独立显式 target。
#
#===-----------------------------------------------------------------------===#

# 八个源文件。符号对照（除法设计 v4 §6.1 终表）：
#   mcs251rt_divuint.c   -> __divuint    mcs251rt_moduint.c  -> __moduint
#   mcs251rt_divulong.c  -> __divulong   mcs251rt_modulong.c -> __modulong
#   mcs251rt_divsint.c   -> __divsint    mcs251rt_modsint.c  -> __modsint
#   mcs251rt_divslong.c  -> __divslong   mcs251rt_modslong.c -> __modslong
set(MCS251RT_SOURCES
  mcs251rt_divuint.c
  mcs251rt_divulong.c
  mcs251rt_divsint.c
  mcs251rt_divslong.c
  mcs251rt_moduint.c
  mcs251rt_modulong.c
  mcs251rt_modsint.c
  mcs251rt_modslong.c
  )

# 清单行序固定（修正案 §3.2）：
# __divuint __divulong __divsint __divslong __moduint __modulong __modsint __modslong
# 【R3】该顺序不再是独立的第二份列表：配置期与源文件名派生的 stem 序列逐项
# 锁死（长度、逐位一致、唯一性），不一致即配置失败。
set(MCS251RT_ORDER
  divuint
  divulong
  divsint
  divslong
  moduint
  modulong
  modsint
  modslong
  )

set(MCS251RT_OUTDIR ${CMAKE_CURRENT_BINARY_DIR})

# 【R1】工具供给两形态（见文件头说明）。
set(MCS251RT_CLANG ""
  CACHE FILEPATH
  "Stage-1 clang for mcs251rt; required only when the in-tree clang target is not yet declared at this point in the configure graph (R1 form B)")
set(MCS251RT_LLC ""
  CACHE FILEPATH
  "Stage-1 llc for mcs251rt; required only when the in-tree llc target is not yet declared at this point in the configure graph (R1 form B)")

if(TARGET clang AND TARGET llc)
  # 形态 A：顶层在 tools 之后接入，in-tree target 可见。
  set(MCS251RT_CLANG_EXE "$<TARGET_FILE:clang>")
  set(MCS251RT_LLC_EXE "$<TARGET_FILE:llc>")
  set(MCS251RT_DEP_CLANG "clang")
  set(MCS251RT_DEP_LLC "llc")
else()
  # 形态 B：lib 阶段接入，TARGET 不可见，必须显式给路径。
  foreach(_v MCS251RT_CLANG MCS251RT_LLC)
    if("${${_v}}" STREQUAL "")
      message(FATAL_ERROR
        "mcs251rt: the in-tree clang/llc targets are not declared at this "
        "point of the configure graph (lib phase runs before tools; R1), and "
        "${_v} is empty. Either wire mcs251rt from the top level after tools "
        "(R1 form A) or pass the stage-1 binary path in ${_v}.")
    endif()
    if(NOT EXISTS "${${_v}}")
      message(FATAL_ERROR "mcs251rt: ${_v} does not exist: ${${_v}}")
    endif()
  endforeach()
  set(MCS251RT_CLANG_EXE "${MCS251RT_CLANG}")
  set(MCS251RT_LLC_EXE "${MCS251RT_LLC}")
  # 增量失效传播（窄复审 R2-2，2026-09-07）：形态 B 把工具二进制的绝对
  # 路径加入文件级 DEPENDS——clang/llc 更新后受影响的 .ll/.o 必须重生成，
  # 不能只靠"构建者先完成第一阶段"解决先后顺序。文件依赖与身份冻结审计
  # （§6.1/§11.2-6，随构建归档）互补、不互替。
  set(MCS251RT_DEP_CLANG "${MCS251RT_CLANG}")
  set(MCS251RT_DEP_LLC "${MCS251RT_LLC}")
endif()

# 路径约束：本目录的审计/清单步骤都不经 shell 引号层（cmake -P +
# execute_process，R2），单引号因此无碍；但为防御任何后续 shell 消费方
# （SPEC §3.4 response file 语法），配置期直接拒绝含单引号的路径。
foreach(_p "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
  string(FIND "${_p}" "'" _q)
  if(NOT _q EQUAL -1)
    message(FATAL_ERROR
      "mcs251rt: path contains a single quote, refuse to wire: ${_p}")
  endif()
endforeach()

# 【R3】源列表与清单顺序一致性（配置期锁死）：长度恰 8、派生 stem 与
# MCS251RT_ORDER 逐位一致、ORDER 无重复。两份列表从此不可能静默分叉。
set(MCS251RT_STEMS "")
foreach(SRC ${MCS251RT_SOURCES})
  get_filename_component(_s "${SRC}" NAME_WE)
  string(REPLACE "mcs251rt_" "" _s "${_s}")
  list(APPEND MCS251RT_STEMS "${_s}")
endforeach()
list(LENGTH MCS251RT_ORDER _n_order)
list(LENGTH MCS251RT_STEMS _n_stems)
if(NOT _n_order EQUAL 8 OR NOT _n_stems EQUAL 8)
  message(FATAL_ERROR
    "mcs251rt: exactly 8 sources and 8 manifest entries are required "
    "(got ${_n_stems} sources, ${_n_order} manifest entries)")
endif()
set(_n_unique "${MCS251RT_ORDER}")
list(REMOVE_DUPLICATES _n_unique)
list(LENGTH _n_unique _n_unique_len)
if(NOT _n_unique_len EQUAL 8)
  message(FATAL_ERROR "mcs251rt: MCS251RT_ORDER contains duplicates")
endif()
foreach(_i RANGE 7)
  list(GET MCS251RT_ORDER ${_i} _o)
  list(GET MCS251RT_STEMS ${_i} _s)
  if(NOT _o STREQUAL _s)
    message(FATAL_ERROR
      "mcs251rt: manifest order diverges from source list at index ${_i}: "
      "${_o} vs ${_s}")
  endif()
endforeach()

# 【R2】IR 审计脚本（构建图内生成工件，配置期写出于本目录，非交付源码）。
# 三态语义：grep 退出码 0 = 命中（拒绝）；1 = 零命中（通过）；>= 2 = 命令或
# 读取失败（拒绝）。修正前草案 `if grep ...; then exit 1; fi && touch` 把
# "无命中"与"执行/读取失败"并入同一假分支后照常生成标记，审计失败会被误认证。
# 定性（R2）：这是宁可误报、不可漏报的**子串前哨检查**，不是"IR 指令计数"——
# 子串可能命中路径、标识符或注释文本（误报方向）；但四个除取余操作名若作为
# 指令出现在 .ll 中必然包含该子串，故"成功检查且零命中"可推出"无四操作
# 指令"。权威判定仍是除法设计 §6.2-5/§11.2-4 的 IR 实体口径审计（排除
# DataLayout 串与注释、按类型与指令实体检查）。
# 路径经 cmake -D 传入、execute_process 执行，不经 shell 引号层——任意路径
# 字符（含空格）安全；单引号路径已在配置期拒绝（双保险）。
file(WRITE "${MCS251RT_OUTDIR}/mcs251rt-audit-ir.cmake" [[
if(NOT EXISTS "${MCS251RT_LL}")
  message(FATAL_ERROR
    "mcs251rt IR audit: input missing: ${MCS251RT_LL}")
endif()
set(RC "")
execute_process(
  COMMAND grep -Eq -e udiv -e sdiv -e urem -e srem -- "${MCS251RT_LL}"
  RESULT_VARIABLE RC
  OUTPUT_QUIET
  ERROR_QUIET)
if(RC STREQUAL "0")
  message(FATAL_ERROR
    "mcs251rt IR audit: forbidden division/remainder opcode substring found in ${MCS251RT_LL}")
elseif(NOT RC STREQUAL "1")
  message(FATAL_ERROR
    "mcs251rt IR audit: grep failed (rc=${RC}); refusing to issue the audit stamp for ${MCS251RT_LL}")
endif()
# 标记落盘也必须检查（窄复审 R3-3，2026-09-07）：execute_process 默认不因
# 子进程非零退出而使脚本失败——touch 失败而脚本照常返回成功时，标记缺失
# 不能靠"下次构建发现输出文件缺失"补救（该次构建已带着"审计已过"的状态
# 继续）。结果非零必须显式非零退出。
set(TOUCH_RC "")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E touch "${MCS251RT_STAMP}"
  RESULT_VARIABLE TOUCH_RC)
if(NOT TOUCH_RC STREQUAL "0")
  message(FATAL_ERROR
    "mcs251rt IR audit: ${MCS251RT_LL} is clean, but creating the audit stamp ${MCS251RT_STAMP} failed (rc=${TOUCH_RC}); refusing to report a successful audit")
endif()
]])

# 【R3】清单期望表与生成/校验脚本（均为构建图内生成工件）。
# 期望表在配置期从已与源列表锁死顺序的 MCS251RT_ORDER 派生：每行一个规范化
# 绝对路径、恰八行、行序固定。校验项（修正案 §3.1/§3.2）：行数恰 8；清单与
# 期望表逐行逐字一致（顺序敏感 ⇒ 缺项、重复、换序同被拒绝）；每个对象文件
# 存在。定义符号对应（__<fn> 定义于对应 .o）属除法设计 §8.6/§11.2-4 对象级
# 审计门，本脚本不冒充该检查，只锁定数量/顺序/存在性/唯一性。
# 清单本身是 mcs251rt 的声明产物：作为 add_custom_command 的 OUTPUT 挂进
# 依赖链（DEPENDS 全部八对象，对象又依赖各自审计标记）——删除清单后重建
# target 会重新生成并重新校验；任何对象缺失或审计未过都不会写出清单。
set(MCS251RT_EXPECTED "${MCS251RT_OUTDIR}/mcs251rt-objects.expected")
set(MCS251RT_MANIFEST "${MCS251RT_OUTDIR}/mcs251rt-objects.list")
set(MCS251RT_MANIFEST_SCRIPT "${MCS251RT_OUTDIR}/mcs251rt-manifest.cmake")
file(WRITE "${MCS251RT_EXPECTED}" "")
foreach(_stem ${MCS251RT_ORDER})
  file(APPEND "${MCS251RT_EXPECTED}" "${MCS251RT_OUTDIR}/mcs251rt_${_stem}.o\n")
endforeach()
file(WRITE "${MCS251RT_MANIFEST_SCRIPT}" [[
if(NOT EXISTS "${MCS251RT_EXPECTED}")
  message(FATAL_ERROR
    "mcs251rt manifest: expected-list missing: ${MCS251RT_EXPECTED}")
endif()
set(_lines "")
file(STRINGS "${MCS251RT_EXPECTED}" _lines)  # whole-line read, no word split
list(LENGTH _lines _n)
if(NOT _n EQUAL 8)
  message(FATAL_ERROR
    "mcs251rt manifest: expected-list must have exactly 8 lines (got ${_n})")
endif()
foreach(_p ${_lines})
  if(NOT EXISTS "${_p}")
    message(FATAL_ERROR
      "mcs251rt manifest: object missing, refusing to emit manifest: ${_p}")
  endif()
endforeach()
file(WRITE "${MCS251RT_MANIFEST}" "")
foreach(_p ${_lines})
  file(APPEND "${MCS251RT_MANIFEST}" "${_p}\n")
endforeach()
]])

set(MCS251RT_OBJECTS "")
foreach(SRC ${MCS251RT_SOURCES})
  get_filename_component(STEM "${SRC}" NAME_WE)
  string(REPLACE "mcs251rt_" "" STEM "${STEM}")
  set(LL "${MCS251RT_OUTDIR}/${STEM}.ll")
  set(AUDIT "${MCS251RT_OUTDIR}/${STEM}.ll.audit")
  set(OBJ "${MCS251RT_OUTDIR}/mcs251rt_${STEM}.o")

  # 第 1 步：产 IR。冻结契约（除法设计 v4 §6.1 冻结清单第 1/4 项）：
  # triple + driver 默认 xsmall 契约(1,2,32,8,1) + 前端 -O2。
  add_custom_command(OUTPUT "${LL}"
    COMMAND "${MCS251RT_CLANG_EXE}"
            --target=mcs251-unknown-none
            -O2 -S -emit-llvm
            -o "${LL}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}"
            "${CMAKE_CURRENT_SOURCE_DIR}/mcs251rt_div.h"
            ${MCS251RT_DEP_CLANG}
    COMMENT "mcs251rt: ${SRC} -> ${STEM}.ll (frozen contract, -emit-llvm)"
    VERBATIM)

  # 第 2 步：IR 审计，作用于即将送 llc 的 .ll 原件（三态语义见上方 R2 注）。
  # i64 实体口径、IR 签名结构与 ABI 属性核对按 §11.2-4 属构建审计脚本职责，
  # 接线时挂在本步之后，不在本草案内冒充完成。
  add_custom_command(OUTPUT "${AUDIT}"
    COMMAND "${CMAKE_COMMAND}"
            "-DMCS251RT_LL=${LL}"
            "-DMCS251RT_STAMP=${AUDIT}"
            -P "${MCS251RT_OUTDIR}/mcs251rt-audit-ir.cmake"
    DEPENDS "${LL}" "${MCS251RT_OUTDIR}/mcs251rt-audit-ir.cmake"
    COMMENT "mcs251rt: IR audit (zero division/remainder opcodes) on ${STEM}.ll"
    VERBATIM)

  # 第 3 步：同一批 .ll 原样交 llc（审计不改文件；llc 读同一路径原件）。
  # 后端契约（§6.1 冻结清单第 3/4 项）：llc 侧 compat DataLayout + llc -O2
  # + ELF 对象格式。
  add_custom_command(OUTPUT "${OBJ}"
    COMMAND "${MCS251RT_LLC_EXE}"
            -mtriple=mcs251
            -O2
            -mcs251-object-format=elf
            -filetype=obj
            "${LL}"
            -o "${OBJ}"
    DEPENDS "${AUDIT}" ${MCS251RT_DEP_LLC}
    COMMENT "mcs251rt: ${STEM}.ll -> mcs251rt_${STEM}.o (ELF32BE, llc -O2)"
    VERBATIM)

  list(APPEND MCS251RT_OBJECTS "${OBJ}")
endforeach()

# 【R3】清单 = 校验通过的声明产物（生成即校验，失败不写文件）。
add_custom_command(OUTPUT "${MCS251RT_MANIFEST}"
  COMMAND "${CMAKE_COMMAND}"
          "-DMCS251RT_EXPECTED=${MCS251RT_EXPECTED}"
          "-DMCS251RT_MANIFEST=${MCS251RT_MANIFEST}"
          -P "${MCS251RT_MANIFEST_SCRIPT}"
  DEPENDS "${MCS251RT_OBJECTS}"
          "${MCS251RT_MANIFEST_SCRIPT}"
          "${MCS251RT_EXPECTED}"
  COMMENT "mcs251rt: verify and emit object manifest (8 lines, fixed order)"
  VERBATIM)

# 机器级调用图核对（§6.2-5、§11.2-4：4 个 unsigned/mod 助手 ecall 计数为
# 0；4 个 signed 包装层只调用各自对应 unsigned helper；无自环、无互递归、
# 无清单外运行时依赖）作用于上述 .o，随构建归档。接线时由验证脚本在
# mcs251rt 之后执行（反汇编/重定位核对），不在本草案内展开。
#
# 消费方契约（修正案 §3.3/§3.4）：显式依赖 mcs251rt target，并按清单行序把
# 清单中的八个对象作为独立路径参数传给 mcs251-lld / lld -flavor mcs251——
# 正式供给 = 本清单（校验通过）+ 全部八对象 + 审计成功，不是"target 构建
# 成功"的泛称；不依赖目录扫描或构建完成先后顺序。
add_custom_target(mcs251rt
  DEPENDS ${MCS251RT_OBJECTS} "${MCS251RT_MANIFEST}"
  COMMENT "mcs251rt: eight audited ELF objects + verified manifest in ${MCS251RT_OUTDIR}")
