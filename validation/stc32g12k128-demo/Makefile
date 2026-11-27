# STC32G12K128 Modern LLVM/Clang MCS-251 Demo Makefile (Linux / WSL)
# STC32G12K128 现代 LLVM/Clang MCS-251 固件示例 Makefile（Linux / WSL）
TOOLCHAIN_BIN ?= ../../../build-mcs251/bin
CLANG         := $(TOOLCHAIN_BIN)/clang
YAML2OBJ      := $(TOOLCHAIN_BIN)/yaml2obj
OBJCOPY       := $(TOOLCHAIN_BIN)/llvm-objcopy
OBJDUMP       := $(TOOLCHAIN_BIN)/llvm-objdump
READOBJ       := $(TOOLCHAIN_BIN)/llvm-readobj
DWARFDUMP     := $(TOOLCHAIN_BIN)/llvm-dwarfdump

BUILD         ?= build
CRT_YAML      := ../mcs251-elf/runtime/crt-irq-v2.yaml
CRT_OBJ       := $(BUILD)/crt-irq-v2.o
LD_SCRIPT     := stc32g12k128.ld

SRCS          := src/uart.c src/timer.c src/main.c
OBJS          := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

# Compile with built-in <stc32.h> (zero -I flags), -Os, -g DWARF, and GC sections.
# 使用内置 <stc32.h>（无需 -I）、-Os 尺寸优化、-g DWARF 调试信息及节级拆分编译。
CFLAGS        := --target=mcs251-unknown-none -std=c11 -Os -g \
                 -ffunction-sections -fdata-sections -Wall -Wextra -Werror
ifdef QEMU_MODE
CFLAGS        += -DQEMU_MODE
endif

ELF           := $(BUILD)/stc32g12k128-demo.elf
HEX           := $(BUILD)/stc32g12k128-demo.hex
MAP           := $(BUILD)/stc32g12k128-demo.map
MAP_JSON      := $(BUILD)/stc32g12k128-demo.map.json
PLACEMENT     := $(BUILD)/stc32g12k128-demo.placement
FACTS         := $(BUILD)/stc32g12k128-demo.facts
LST           := $(BUILD)/stc32g12k128-demo.lst

.PHONY: all clean verify

all: $(HEX) $(LST) verify

$(BUILD):
	mkdir -p $(BUILD)

# Materialize v2 IRQ CRT object and set e_flags=0x00000102.
# 生成 v2 中断模式 CRT 目标文件并将 ELF 头 e_flags 置为 0x00000102。
$(CRT_OBJ): $(CRT_YAML) | $(BUILD)
	$(YAML2OBJ) $< -o $@
	python3 -c 'import pathlib; p=pathlib.Path("$@"); b=bytearray(p.read_bytes()); b[36:40]=bytes.fromhex("00000102"); p.write_bytes(bytes(b))'

$(BUILD)/%.o: src/%.c src/board.h | $(BUILD)
	$(CLANG) $(CFLAGS) -c $< -o $@

# Link with GNU script -T, --gc-sections, JSON map, and zero-trust G11 verification.
# 使用 -T 链接脚本、--gc-sections、JSON Map 及 G11 独立零信任校验完成链接。
$(ELF): $(CRT_OBJ) $(OBJS) $(LD_SCRIPT)
	$(CLANG) --target=mcs251-unknown-none -T $(LD_SCRIPT) \
	  -Wl,--gc-sections -Wl,--keep-symbols \
	  -Wl,--map=$(MAP) -Wl,--map-json=$(MAP_JSON) \
	  -Wl,--placement-report=$(PLACEMENT) -Wl,--verify-placement \
	  -Wl,--link-facts=$(FACTS) \
	  $(CRT_OBJ) $(OBJS) -o $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

$(LST): $(ELF)
	$(OBJDUMP) -d $< > $@

# Verify ELF v2 flags, DWARF5 integrity, GC pruning, KEEP, and ISR priority init.
# 校验 ELF v2 标志、DWARF5 完整性、GC 死代码裁剪、KEEP 保留与 ISR 优先级合成。
verify: $(ELF) $(HEX)
	$(READOBJ) -h $(ELF) | grep -q EF_MCS251_ABI_V2
	$(DWARFDUMP) --verify $(ELF)
	grep -q _fw_info_keep $(MAP)
	grep -q ___mcs251_isr_priority_init $(MAP)
	! grep -q _dead_unused_helper $(MAP)

clean:
	rm -rf $(BUILD)
