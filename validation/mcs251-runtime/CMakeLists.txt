#===-- CMakeLists.txt -----------------------------------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===-----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 3.16)
# NONE：本 target 只编排外部工具（clang/llc），不启用 cmake 语言编译器
project(mcs251-runtime NONE)

#
# mcs251-runtime —— MCS251 软浮点运行时 + 内存原语（独立显式 CMake target）。
#
# 规范依据：照除法运行时（llvm/lib/Target/MCS251/Runtime/）配方；
#   软浮点 IEEE-754 binary32 实现 + libc 内存原语子集。
#
# 语言契约（Alice 审查 RT-3 固定）：
#   - 全部源码以 -std=c11 编译（固定语言版本，不随编译器默认漂移）。
#   - ILP32 前提：本运行时假定 int/long/指针均为 32 位、short 16 位
#     （MCS251 目标模型）。src/mcs251_float.h 内含 char/short/int/
#     int32_t/uint32_t 宽度互锁断言（宿主目标通用），long/指针宽度
#     断言经 -DMCS251_RT_TARGET 仅在目标侧构建时生效（宿主 LP64 跳过）。
#
# 红线（同除法运行时）：
#   1. 不进入任何 LLVM 库或工具的链接图（无 target_link_libraries）；
#      只用构建依赖表达两阶段顺序。
#   2. 两阶段依赖：第一阶段常规构建产出 clang 与 llc；本 target 以同一
#      构建树刚产出的工具链编译源码。
#   3. 三步管道：clang -S -emit-llvm 产 IR -> IR 审计 -> llc 产 ELF 对象。
#   4. 交付形态 = 独立 ELF32BE ET_REL .o + 显式清单（构建期产物，
#      add_custom_command(OUTPUT)，依赖全部对象，不再是配置期 file(WRITE)）。
#
# 与除法运行时的差异：
#   - 软浮点运算/比较模块用 -O2（与除法运行时一致）。
#   - mcs251_bitutil.c 按验收配方以 -O0 独立编译（独立 TU 防止 clang -O2
#     跨函数把 de Bruijn 位扫描综合为后端不支持的 ctlz）。
#   - libc 内存原语用 -O0（-O2 循环展开导致 PC-rel branch out of range）。
#   - mcs251_float_math.c 不入库（范围裁剪，保留工作树不构建）。
#   - 本目录不在 llvm/lib/Target/ 下，而在 validation/ 下（新目录零冲突）。
#
# IR 审计（实际交给 llc 的 .ll 逐文件检查）：
#   - 禁止浮点类型（float/double token）与 64 位类型（i64 token）；
#   - 禁止后端不支持的 ctlz/cttz intrinsic；
#   - 对象未解析依赖检查：.ll 中所有 declare（即对象的外部未解析符号）
#     必须属于白名单 {mcs251_msb32}（由 bitutil.o 在链接期提供）。
#   审计失败（cmake -P 非零退出）立即中断对象生成。
#===-----------------------------------------------------------------------===#

# 软浮点源文件（-O2 编译）。
# G7 S1''：加减/乘法/除法例程从 mcs251_float_arith.c 拆到独立 TU，使链接器
# 可按程序未解析符号单独拉取（MCS251 lld 无 --gc-sections，故用多 TU 而非
# -ffunction-sections 达成按例程链接）。
set(MCS251RT_FLOAT_SOURCES
  mcs251_float_arith.c
  mcs251_float_addsub.c
  mcs251_float_mul.c
  mcs251_float_div.c
  mcs251_float_cmp.c
  )

# 位工具（-O0 独立编译，独立 TU 防 ctlz 综合）
set(MCS251RT_BITUTIL_SOURCES
  mcs251_bitutil.c
  )

# libc 内存原语源文件（-O0 编译）
set(MCS251RT_LIBC_SOURCES
  mcs251_libc.c
  )

set(MCS251RT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(MCS251RT_OUTDIR ${CMAKE_CURRENT_BINARY_DIR})

# 工具供给（同除法运行时 R1 两形态）
set(MCS251RT_CLANG ""
  CACHE FILEPATH "Stage-1 clang for mcs251-runtime")
set(MCS251RT_LLC ""
  CACHE FILEPATH "Stage-1 llc for mcs251-runtime")

if(TARGET clang AND TARGET llc)
  set(MCS251RT_CLANG_EXE "$<TARGET_FILE:clang>")
  set(MCS251RT_LLC_EXE "$<TARGET_FILE:llc>")
  set(MCS251RT_DEP_CLANG "clang")
  set(MCS251RT_DEP_LLC "llc")
else()
  foreach(_v MCS251RT_CLANG MCS251RT_LLC)
    if("${${_v}}" STREQUAL "")
      message(FATAL_ERROR
        "mcs251-runtime: ${_v} is empty and in-tree clang/llc targets "
        "are not declared. Pass the stage-1 binary path in ${_v}.")
    endif()
    if(NOT EXISTS "${${_v}}")
      message(FATAL_ERROR "mcs251-runtime: ${_v} does not exist: ${${_v}}")
    endif()
  endforeach()
  set(MCS251RT_CLANG_EXE "${MCS251RT_CLANG}")
  set(MCS251RT_LLC_EXE "${MCS251RT_LLC}")
  set(MCS251RT_DEP_CLANG "${MCS251RT_CLANG}")
  set(MCS251RT_DEP_LLC "${MCS251RT_LLC}")
endif()

# --------------------------------------------------------------------------- 
# IR 审计脚本（构建期辅助工具，写入构建目录；非交付产物）
# 用法：cmake -DLL_FILE=<path> -P mcs251rt-ir-audit.cmake
# --------------------------------------------------------------------------- 
set(MCS251RT_IR_AUDIT "${MCS251RT_OUTDIR}/mcs251rt-ir-audit.cmake")
file(WRITE "${MCS251RT_IR_AUDIT}" [[
# mcs251rt-ir-audit.cmake — 审计实际交给 llc 的 LLVM IR
if(NOT DEFINED LL_FILE)
  message(FATAL_ERROR "LL_FILE not set")
endif()
file(READ "${LL_FILE}" IR)
set(NL "\n")
# 只检查代码区：剔除注释行(';')、元数据行('!')与 target datalayout 行
#（datalayout 必然枚举 i64/f32/f64 地址空间描述，元数据偏移量也用 i64，
#  均不构成代码依赖；红线针对的是代码中的 64 位类型使用）。
string(REPLACE "${NL}" ";" IR_LINES "${IR}")
set(CODE "")
foreach(line IN LISTS IR_LINES)
  if(line MATCHES "^[;!]")
    continue()
  endif()
  if(line MATCHES "^target datalayout")
    continue()
  endif()
  string(APPEND CODE "${line}${NL}")
endforeach()
set(ERRORS "")
# 1) 代码区禁止浮点/64 位类型 token（标识符边界，_floatsisf 等不误报）
foreach(tok float double i64)
  string(REGEX MATCH "[^A-Za-z0-9_$.]${tok}([^A-Za-z0-9_$.]|$)" _m "${CODE}")
  string(REGEX MATCH "^${tok}([^A-Za-z0-9_$.]|$)" _m2 "${CODE}")
  if(NOT _m STREQUAL "" OR NOT _m2 STREQUAL "")
    list(APPEND ERRORS "forbidden type token '${tok}'")
  endif()
endforeach()
# 2) 后端不支持的 intrinsic
foreach(pat ctlz cttz)
  string(FIND "${CODE}" "${pat}" _pos)
  if(NOT _pos LESS 0)
    list(APPEND ERRORS "forbidden intrinsic '${pat}'")
  endif()
endforeach()
# 3) 未解析依赖：符号级精确匹配（Alice 复审裁定，杜绝子串绕过）。
#    代码区所有 @符号引用必须满足其一：
#    a) 本模块 define 定义（define 行提取，全名相等比较）；
#    b) 白名单精确符号 mcs251_msb32（bitutil.o 链接期提供，全名相等）；
#    c) 已验证当前 llc 可下译的 clang -O2 常规 intrinsic（lifetim/umax/
#       abs/ucmp/fshl/fshr 模式），按"前缀 + 点分重载后缀"严格正则匹配——
#       @mcs251_msb32_evil / @llvm.umax_evil / @llvm.umaxx 均拒绝。
#    fshl/fshr（G7 S1'' 追加）：clang -O2 会把 16 位 `(a<<n)|(b>>(16-n))`
#       识别成 funnel-shift。后端**有意**支持它：MCS251ISelLowering.cpp:240-241
#       对 FSHL/FSHR 设 Expand 走通用合法化，llvm/test/CodeGen/MCS251/
#       rotate-expand.ll 是专门回归，且实测下译后对象无任何未解析符号
#       （llvm-readelf -sW 的 UND 集为空）——故它与 umax/abs 同类，属
#       "已下译、不产生链接期外部依赖"的常规 intrinsic，纳入白名单。
#    d) G13a-S3 追加：printf 家族跨 TU 提供者，全名相等——
#       putchar（mcs251_putchar.o 提供，S2 切片）、out_char/out_str
#       （mcs251_printf.o 提供，S3 起外部链接）、out_float
#       （mcs251_printf_float.o 提供，S3 拆分）。与 mcs251_msb32 同为
#       "运行时族内对象在链接期闭合"的精确符号；对不在 printf 家族
#       闭包内的模块同样只是符号级放行，不放宽类型/intrinsic 红线。
set(_allow_desc "mcs251_msb32(精确), putchar/out_char/out_str/out_float(精确, G13a-S3 printf 族跨 TU 提供者), llvm.{lifetime.start,lifetime.end,umax,abs,ucmp,fshl,fshr}(前缀+重载后缀)")
# b/c/d 白名单：全名相等或 intrinsic 正则
set(_sym_allow_exact "mcs251_msb32;putchar;out_char;out_str;out_float")
set(_intr_re "^llvm\\.(lifetime\\.start|lifetime\\.end|umax|abs|ucmp|fshl|fshr)(\\.[A-Za-z0-9]+)*$")
# 模块内定义的符号：define 行（函数）+ 行首 @name = 行（全局定义）
set(_defined "")
string(REGEX MATCHALL "define[^${NL}]*" _deflines "${CODE}")
foreach(dl ${_deflines})
  string(REGEX MATCH "@[A-Za-z0-9_.$]+" _ds "${dl}")
  if(NOT _ds STREQUAL "")
    string(SUBSTRING "${_ds}" 1 -1 _dn)
    list(APPEND _defined "${_dn}")
  endif()
endforeach()
foreach(gl IN LISTS IR_LINES)
  if(gl MATCHES "^@[A-Za-z0-9_.$]+[ \t]*=")
    string(REGEX MATCH "^@[A-Za-z0-9_.$]+" _gs "${gl}")
    string(SUBSTRING "${_gs}" 1 -1 _gn)
    list(APPEND _defined "${_gn}")
  endif()
endforeach()
# 代码区全部 @引用（含 declare 与 call）逐一校验
string(REGEX MATCHALL "@[A-Za-z0-9_.$]+" _refs "${CODE}")
foreach(r ${_refs})
  string(SUBSTRING "${r}" 1 -1 _rn)
  list(FIND _defined "${_rn}" _idx)
  if(NOT _idx LESS 0)
    continue()
  endif()
  # G13a-S3：精确符号白名单由单符号改列表成员判定（语义不变，全名相等）
  list(FIND _sym_allow_exact "${_rn}" _allow_idx)
  if(NOT _allow_idx LESS 0)
    continue()
  endif()
  if(_rn MATCHES "${_intr_re}")
    continue()
  endif()
  list(APPEND ERRORS "unresolved symbol not in allowlist {${_allow_desc}}: @${_rn}")
endforeach()
if(ERRORS)
  message(FATAL_ERROR "mcs251-runtime IR audit FAILED for ${LL_FILE}: ${ERRORS}")
endif()
message(STATUS "mcs251-runtime IR audit OK: ${LL_FILE}")
]])

set(MCS251RT_OBJECTS "")
set(MCS251RT_MANIFEST "${MCS251RT_OUTDIR}/mcs251-runtime-objects.list")

# 通用两步管道：clang -std=c11 -S -emit-llvm -> [IR 审计] -> llc -> ELF .o
# （EXTRA_DEPS 经 ARGN 可选：无头依赖的模块（G13a-S2 mcs251_putchar.c）
#   省略第三参调用，ARGN 为空时 ${EXTRA_DEPS} 展开为空。）
# G13a-S3 追加式扩展（既有调用点零改动）：
#   - ARGN 中以 "-" 开头的项作为额外 clang 参数（如 -DMCS251_PRINTF_
#     NO_FLOAT、-fno-builtin），其余仍为头依赖；
#   - ARGN 中 "STEM=<name>" 项覆盖输出基名（同源不同宏的双形态构建：
#     mcs251_printf.c 完整形态与无浮点形态需要互不冲突的 .ll/.o 输出）。
function(mcs251rt_add_module SRC OPT_LEVEL)
  set(EXTRA_DEPS "")
  set(EXTRA_FLAGS "")
  set(STEM_OVERRIDE "")
  foreach(_arg IN LISTS ARGN)
    if(_arg MATCHES "^STEM=")
      string(SUBSTRING "${_arg}" 5 -1 STEM_OVERRIDE)
    elseif(_arg MATCHES "^-")
      list(APPEND EXTRA_FLAGS "${_arg}")
    else()
      list(APPEND EXTRA_DEPS "${_arg}")
    endif()
  endforeach()
  if(STEM_OVERRIDE STREQUAL "")
    get_filename_component(STEM "${SRC}" NAME_WE)
  else()
    set(STEM "${STEM_OVERRIDE}")
  endif()
  set(LL "${MCS251RT_OUTDIR}/${STEM}.ll")
  set(OBJ "${MCS251RT_OUTDIR}/${STEM}.o")

  add_custom_command(OUTPUT "${LL}"
    COMMAND "${MCS251RT_CLANG_EXE}"
            --target=mcs251-unknown-none
            -std=c11
            -DMCS251_RT_TARGET
            -${OPT_LEVEL} -I"${MCS251RT_SRC_DIR}"
            ${EXTRA_FLAGS}
            -S -emit-llvm
            -o "${LL}"
            "${MCS251RT_SRC_DIR}/${SRC}"
    DEPENDS "${MCS251RT_SRC_DIR}/${SRC}" ${EXTRA_DEPS}
            ${MCS251RT_DEP_CLANG}
    COMMENT "mcs251-runtime: ${SRC} -> ${STEM}.ll (-std=c11, -${OPT_LEVEL}, -emit-llvm${EXTRA_FLAGS})"
    VERBATIM)

  add_custom_command(OUTPUT "${OBJ}"
    # 先审计将交给 llc 的 IR（类型/intrinsic/未解析符号白名单），失败即中断
    COMMAND "${CMAKE_COMMAND}" "-DLL_FILE=${LL}" -P "${MCS251RT_IR_AUDIT}"
    COMMAND "${MCS251RT_LLC_EXE}"
            -mtriple=mcs251
            -${OPT_LEVEL}
            -mcs251-object-format=elf
            -filetype=obj
            "${LL}"
            -o "${OBJ}"
    # 审计脚本自身变更也要触发重审重编（Alice 复审裁定）
    DEPENDS "${LL}" "${MCS251RT_IR_AUDIT}" ${MCS251RT_DEP_LLC}
    COMMENT "mcs251-runtime: [IR audit] ${STEM}.ll -> ${STEM}.o (ELF32BE, llc -${OPT_LEVEL})"
    VERBATIM)

  list(APPEND MCS251RT_OBJECTS "${OBJ}")
  set(MCS251RT_OBJECTS "${MCS251RT_OBJECTS}" PARENT_SCOPE)
endfunction()

# 软浮点模块：-O2 管道（依赖浮点头；算术单元另依赖 limb 头）
# G7 S1''：按例程拆分，各自独立对象，便于链接期按符号拉取。
mcs251rt_add_module(mcs251_float_arith.c O2
  "${MCS251RT_SRC_DIR}/mcs251_float.h;${MCS251RT_SRC_DIR}/mcs251_bitutil.h")
mcs251rt_add_module(mcs251_float_addsub.c O2
  "${MCS251RT_SRC_DIR}/mcs251_float.h;${MCS251RT_SRC_DIR}/mcs251_float_limb.h")
mcs251rt_add_module(mcs251_float_mul.c O2
  "${MCS251RT_SRC_DIR}/mcs251_float.h;${MCS251RT_SRC_DIR}/mcs251_float_limb.h")
mcs251rt_add_module(mcs251_float_div.c O2
  "${MCS251RT_SRC_DIR}/mcs251_float.h;${MCS251RT_SRC_DIR}/mcs251_float_limb.h;${MCS251RT_SRC_DIR}/mcs251_bitutil.h")
mcs251rt_add_module(mcs251_float_cmp.c O2
  "${MCS251RT_SRC_DIR}/mcs251_float.h")

# bitutil：-O0 独立编译（验收配方；独立 TU 防 ctlz 综合）
mcs251rt_add_module(mcs251_bitutil.c O0
  "${MCS251RT_SRC_DIR}/mcs251_bitutil.h")

# libc 模块：-O0 管道（-O2 导致 PC-rel branch out of range）
mcs251rt_add_module(mcs251_libc.c O0
  "${MCS251RT_SRC_DIR}/mcs251_libc.h")

# G13a-S2 切片模块（G13A-CODE-DESIGN-draft.md rev-2 §3-S2 / PM D4）：
# strlen 自 mcs251_libc.c 原样拆出为独立 TU（对象级按需闭包的最小粒度；
# lld 无 --gc-sections，只有独立对象才能单独拉入），putchar 提供者补齐
# （demo 41 类程序引用 printf 且未自带 putchar 时闭包所需；丢弃型 sink，
# 语义边界见源文件头）。两者同 libc 模块用 -O0 管道；strlen 定义唯一
# （mcs251_libc.c 已同步移除），签名协议与 Tag 28 语义零改动。
mcs251rt_add_module(mcs251_str.c O0
  "${MCS251RT_SRC_DIR}/mcs251_libc.h")
mcs251rt_add_module(mcs251_putchar.c O0)

# G13a-S3 切片模块（G13A-CODE-DESIGN-draft.md rev-2 §3-c）：printf 运行时
# 拆出 _out_float + 无浮点引擎条件编译。三对象（同 libc 模块 -O0 管道；
# 额外传 -ffreestanding -fno-builtin 对齐 drive.py 冻结的 B-S4 对象配方
# ——printf/sprintf/putchar 是 libc 内建名，防 clang 以内建语义改写本
# 家族的声明/调用）：
#   mcs251_printf.o         完整形态（默认）：UND 含 _out_float，闭包须
#                           再拉 mcs251_printf_float.o（浮点格式化可用）；
#   mcs251_printf_float.o   out_float 完整实现（S3 自 mcs251_printf.c 原样
#                           迁入，仅 static → 外部链接），UND 含
#                           _out_char/_out_str（由 mcs251_printf.o 提供）；
#   mcs251_printf_nofloat.o 无浮点引擎形态（-DMCS251_PRINTF_NO_FLOAT）：
#                           %f/%F、%g/%G、%e/%E 落入未知格式符分支，对象
#                           无 UND _out_float；仅当按
#                           mcs251_printf_internal.h 冻结判定规则可证明
#                           程序无浮点格式时方可选用（默认永远完整形态）。
mcs251rt_add_module(mcs251_printf.c O0
  STEM=mcs251_printf
  -ffreestanding -fno-builtin
  "${MCS251RT_SRC_DIR}/mcs251_libc.h;${MCS251RT_SRC_DIR}/mcs251_printf_internal.h")
mcs251rt_add_module(mcs251_printf.c O0
  STEM=mcs251_printf_nofloat
  -ffreestanding -fno-builtin -DMCS251_PRINTF_NO_FLOAT
  "${MCS251RT_SRC_DIR}/mcs251_libc.h;${MCS251RT_SRC_DIR}/mcs251_printf_internal.h")
mcs251rt_add_module(mcs251_printf_float.c O0
  -ffreestanding -fno-builtin
  "${MCS251RT_SRC_DIR}/mcs251_libc.h;${MCS251RT_SRC_DIR}/mcs251_printf_internal.h")

# ---------------------------------------------------------------------------
# 清单：构建期产物（add_custom_command(OUTPUT)），依赖全部对象。
# 写入器脚本在配置期生成到构建目录，内容在对象清单确定后固定；
# 触碰任一源文件 -> .ll/.o 重生成 -> 清单重写（依赖链完整）。
# ---------------------------------------------------------------------------
set(MCS251RT_MANIFEST_WRITER "${MCS251RT_OUTDIR}/mcs251rt-write-manifest.cmake")
set(MCS251RT_MANIFEST_TEXT "# mcs251-runtime object manifest (generated at build time)\n")
foreach(OBJ ${MCS251RT_OBJECTS})
  string(APPEND MCS251RT_MANIFEST_TEXT "${OBJ}\n")
endforeach()
file(WRITE "${MCS251RT_MANIFEST_WRITER}"
  "file(WRITE \"${MCS251RT_MANIFEST}\" \"${MCS251RT_MANIFEST_TEXT}\")\n"
  "message(STATUS \"mcs251-runtime: manifest written: ${MCS251RT_MANIFEST}\")\n")

add_custom_command(OUTPUT "${MCS251RT_MANIFEST}"
  COMMAND "${CMAKE_COMMAND}" -P "${MCS251RT_MANIFEST_WRITER}"
  DEPENDS ${MCS251RT_OBJECTS} "${MCS251RT_MANIFEST_WRITER}"
  COMMENT "mcs251-runtime: write object manifest (build-time OUTPUT)"
  VERBATIM)

add_custom_target(mcs251-runtime ALL
  DEPENDS ${MCS251RT_OBJECTS} "${MCS251RT_MANIFEST}"
  COMMENT "mcs251-runtime: soft-float + libc objects in ${MCS251RT_OUTDIR}")
