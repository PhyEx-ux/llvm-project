# =============================================================================
# Makefile — 现代 C 特性演示固件（Clang + LLVM -> STC32 HEX）
#
# 生产链（零 SDCC 生成代码；sdas251 仅汇编手写启动模块）：
#   fork-clang (--target=mcs251-unknown-none -std=c11)
#     -> llc (MCS251 后端, .rel)
#     -> mcs251_ld.py (严格 ABI 链接, Intel-HEX)
#     -> qemu-system-mcs251（由 BOARD 片段选择机器）
#
# 常用目标：
#   make          构建 build/demo.hex
#   make run      QEMU 运行固件（串口输出 build/demo.serial）
#   make check    黄金验收：宿主机与目标机同源运行、逐行比对输出
#   make clean    清理构建产物
#
# 所有工具路径可用环境变量覆盖，例如：
#   make LLC=/path/to/llc check
# 本 Makefile 在 WSL 内执行（工具链均为 Linux 侧）。
# =============================================================================

# ---------- 工具链 ----------
CLANG ?= /home/liu/build-clang/bin/clang
LLC   ?= /home/liu/build-mcs251/bin/llc
SDAS  ?= /home/liu/build-sdcc/bin/sdas251
# 不使用 GNU make 内置 LD（默认值 ld 会遮住 ?=）。
LINKER := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../mcs251-ld/mcs251_ld.py)
MCS251_LD ?= python3 $(LINKER)
PYTHON ?= python3
QEMU_TIMEOUT ?= 30
QEMU  ?= /home/liu/mcs251-clang/bin-frozen/6b9edfd0/qemu-system-mcs251
HOSTCC ?= cc

# ---------- 路径 ----------
HERE   := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
FW     := $(HERE)../mcs251-firmware
BUILD  ?= $(HERE)build
SRC    := $(HERE)src

# ---------- 板级配置 ----------
BOARD ?= stc32g12k128
BOARD_FILE := $(HERE)boards/$(BOARD).mk
AVAILABLE_BOARDS := $(basename $(notdir $(wildcard $(HERE)boards/*.mk)))
ifeq ($(filter $(BOARD),$(AVAILABLE_BOARDS)),)
$(error 未知 BOARD=$(BOARD)；可用型号：$(AVAILABLE_BOARDS))
endif
include $(BOARD_FILE)

BOARD_VARIABLES := FLASH_BASE FLASH_SIZE EDATA_END CSEG_BASE XINIT_BASE \
                   EEPROM_MAX HRIC BAUD UART_RELOAD LED_PORT LED_BIT \
                   QEMU_MACHINE QEMU_NOTE
$(foreach var,$(BOARD_VARIABLES),\
  $(if $($(var)),,$(error $(BOARD_FILE) 缺少板级变量 $(var))))
BOARD_CONFIG := $(BUILD)/board_config.h
BOARD_STAMP := $(BUILD)/.board-$(BOARD)

# ---------- 编译参数 ----------
# -std=c11：本 demo 展示的 C99/C11 特性集合
# -O2：本 demo 验收配置；常量特性允许优化折叠，不代表逐项指令覆盖。
# 前端 -O0 可能保留尚未支持的 const 聚合/重定位，不承诺整工程可用。
CFLAGS  ?= --target=mcs251-unknown-none -std=c11 -O2 -Wall -Wextra
HCFLAGS ?= -std=c11 -O2 -Wall -Wextra -DHOST_BUILD
override CFLAGS  += -I$(BUILD)
override HCFLAGS += -I$(BUILD)

# ---------- 目标文件 ----------
MODULES = main features
RELS    = $(patsubst %,$(BUILD)/%.rel,$(MODULES))

.PHONY: all run check host board-config test-runner clean
.DELETE_ON_ERROR:
# 保留 IR 便于复验，不让 make 将模式规则中间产物自动删除。
.SECONDARY: $(patsubst %,$(BUILD)/%.ll,$(MODULES))

all: $(BUILD)/demo.hex
host: $(BUILD)/host.expected
board-config: $(BOARD_CONFIG)

test-runner:
	$(PYTHON) $(HERE)test-run-qemu.py

# ---------- 板级头文件 ----------

# 仅由 boards/*.mk 生成；型号选择停留在构建层，C 代码只消费数值宏。
# 型号戳确保同一 BUILD 切换 BOARD 时不会复用另一块板的旧头文件。
$(BOARD_STAMP): | $(BUILD)
	@rm -f $(BUILD)/.board-*
	@touch $@

$(BOARD_CONFIG): $(BOARD_STAMP) $(BOARD_FILE) $(HERE)Makefile | $(BUILD)
	@{ \
	  printf '%s\n' '#ifndef MCS251_DEMO_BOARD_CONFIG_H'; \
	  printf '%s\n' '#define MCS251_DEMO_BOARD_CONFIG_H'; \
	  printf '%s\n' '#define FLASH_BASE $(FLASH_BASE)u'; \
	  printf '%s\n' '#define FLASH_SIZE $(FLASH_SIZE)u'; \
	  printf '%s\n' '#define EDATA_END $(EDATA_END)u'; \
	  printf '%s\n' '#define CSEG_BASE $(CSEG_BASE)u'; \
	  printf '%s\n' '#define XINIT_BASE $(XINIT_BASE)u'; \
	  printf '%s\n' '#define EEPROM_MAX $(EEPROM_MAX)u'; \
	  printf '%s\n' '#define HRIC $(HRIC)u'; \
	  printf '%s\n' '#define BAUD $(BAUD)u'; \
	  printf '%s\n' '#define UART_RELOAD $(UART_RELOAD)u'; \
	  printf '%s\n' '#define LED_PORT $(LED_PORT)'; \
	  printf '%s\n' '#define LED_BIT $(LED_BIT)u'; \
	  printf '%s\n' '#endif'; \
	} > $@.tmp
	@if test -r $@ && cmp -s $@.tmp $@; then rm -f $@.tmp; \
	else mv -f $@.tmp $@; fi

# ---------- 目标机链 ----------

# C -> 目标 IR（fork-clang 前端，mcs251 数据模型 int=32/long=32/ptr=32）
$(BUILD)/%.ll: $(SRC)/%.c $(SRC)/mcs251_features.h $(SRC)/uart.h $(BOARD_CONFIG) $(CLANG) $(HERE)Makefile
	$(CLANG) $(CFLAGS) -S -emit-llvm $< -o $@

# 目标 IR -> ASxxxx .rel 对象（MCS251 后端对象发射）
$(BUILD)/%.rel: $(BUILD)/%.ll $(LLC)
	$(LLC) -mtriple=mcs251-unknown-none -verify-machineinstrs -filetype=obj $< -o $@

# 手写启动模块：sdas251 汇编（复位向量/向量表/稀疏 XINIT 初始化循环）
$(BUILD)/crt.rel: $(FW)/crt-selfstart.asm | $(BUILD)
	$(SDAS) -plosgffw -o $@ $<

# 链接命令文件：替换模板占位符（sed 保持行结构，空行会被解析器当 EOF）
$(BUILD)/demo.lk: $(HERE)link/demo.lk.in $(RELS) $(BUILD)/crt.rel $(BOARD_STAMP) $(BOARD_FILE) $(HERE)Makefile
	sed -e "s|@OUTPUT_STEM@|$(BUILD)/demo|" \
	    -e "s|@CSEG_BASE@|$(CSEG_BASE)|" \
	    -e "s|@XINIT_BASE@|$(XINIT_BASE)|" \
	    -e "s|@CRT_REL@|$(BUILD)/crt.rel|" \
	    -e "s|@MAIN_REL@|$(BUILD)/main.rel|" \
	    -e "s|@FEATURES_REL@|$(BUILD)/features.rel|" $< | grep -v '^;' | sed '/^$$/d' > $@

# 链接：板片段把实际 EDATA 末地址显式传给无型号知识的链接器。
# FLASH_BASE/FLASH_SIZE 是 Code ROM 越界门禁（E3 lld --flash-base/--flash-size，
# 纯数值、型号知识留在构建层）：链接器据此拒绝任何越出板上 Flash 窗口的
# CODE 区布局（HOME/VECS/BOOT/CSEG/XINIT）。当前 mcs251_ld.py 对未知
# --opt=value 选项仅告警忽略，接线保持前向兼容；换用 mcs251-lld 后门禁即生效。
$(BUILD)/demo.hex: $(BUILD)/demo.lk $(RELS) $(BUILD)/crt.rel $(LINKER)
	$(MCS251_LD) --mcs251-abi --edata-end $(EDATA_END) \
	  --flash-base=$(FLASH_BASE) --flash-size=$(FLASH_SIZE) \
	  -f $(BUILD)/demo.lk

# ---------- 运行与验收 ----------

# 每次运行都新建串口文件；检测完整终止行后回收 QEMU，超时/提前退出均失败。
run: $(BUILD)/demo.hex
	@printf 'BOARD=%s: %s\n' '$(BOARD)' '$(QEMU_NOTE)'
	$(PYTHON) $(HERE)run-qemu.py --qemu $(QEMU) --machine $(QEMU_MACHINE) \
	  --image $< --serial $(BUILD)/demo.serial --timeout $(QEMU_TIMEOUT)

# 宿主机侧：同一份源码经 gcc 运行，输出即"期望 transcript"（真值来源）
$(BUILD)/host-demo: $(SRC)/main.c $(SRC)/features.c $(SRC)/mcs251_features.h $(SRC)/uart.h $(BOARD_CONFIG)
	$(HOSTCC) $(HCFLAGS) -I$(SRC) $(SRC)/main.c $(SRC)/features.c -o $@

$(BUILD)/host.expected: $(BUILD)/host-demo
	$< > $@

# 黄金验收：输出必须完整且以 DEMO-PASS 结束，再与宿主机逐字比对。
# 两边同样 FAIL 不能算通过；QEMU stderr 单独归档，不过滤串口内容。
check: $(BUILD)/host.expected $(BUILD)/demo.hex
	@printf 'BOARD=%s: %s\n' '$(BOARD)' '$(QEMU_NOTE)'
	$(PYTHON) $(HERE)run-qemu.py --qemu $(QEMU) --machine $(QEMU_MACHINE) \
	  --image $(BUILD)/demo.hex --serial $(BUILD)/demo.serial \
	  --timeout $(QEMU_TIMEOUT) --expected $(BUILD)/host.expected

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
