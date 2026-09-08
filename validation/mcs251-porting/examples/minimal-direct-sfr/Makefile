# Minimal direct-SFR + intrins.h ELF production-chain proof.
# The CRT fixture is generated from its tracked YAML source into BUILD. It
# supplies reset/vector/startup only; no prebuilt crt.o is an input.
.DEFAULT_GOAL := all
.NOTPARALLEL:
HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PORTING_ROOT := $(abspath $(HERE)/../..)
BUILD ?= $(HERE)/build

# Tools default to hash-pinned frozen binaries; the s1 build tree is rebuilt
# in place and is not a stable identity. See
# /home/liu/mcs251-demo-test/bin-frozen/porting-v1-tools.identity.txt
CLANG ?= /home/liu/build-clang/bin/clang
LLC ?= /home/liu/mcs251-demo-test/bin-frozen/llc
LLD ?= /home/liu/build-mcs251-lld/bin/lld
OBJCOPY ?= /home/liu/mcs251-demo-test/bin-frozen/llvm-objcopy
YAML2OBJ ?= /home/liu/mcs251-demo-test/bin-frozen/yaml2obj
QEMU ?= /home/liu/build-qemu/qemu-system-mcs251
ELF_NOP_YAML ?= $(PORTING_ROOT)/runtime/elf-nop.yaml
CRT_YAML ?= $(PORTING_ROOT)/../mcs251-elf/runtime/crt-selfstart.yaml
CRT := $(BUILD)/crt-selfstart.o
PYTHON ?= python3

# The ELF streamer has no MCS251 text-inline-asm parser. This selects the
# documented ELF NOP helper path in include/intrins.h.
CFLAGS := --target=mcs251-unknown-none -std=c11 -O2 -Wall -Wextra -Werror \
  -DMCS251_PORTING_ELF_NOP_HELPER
LDFLAGS := --edata-end 0x3fff \
  --area-start=HOME=0xff0000 --area-start=VECS=0xff0003 \
  --area-start=BOOT=0xff0100 --area-start=CSEG=0xff0200 \
  --area-start=XINIT=0xff8000

.PHONY: all run clean
.DELETE_ON_ERROR:

all: $(BUILD)/minimal.hex

$(BUILD):
	mkdir -p "$@"

$(BUILD)/main.ll: main.c ../../generated/stc32g-v1.h ../../include/intrins.h | $(BUILD)
	$(CLANG) $(CFLAGS) -S -emit-llvm "$<" -o "$@"

$(BUILD)/main.o: $(BUILD)/main.ll
	$(LLC) -mtriple=mcs251-unknown-none -verify-machineinstrs \
	  -mcs251-object-format=elf -filetype=obj "$<" -o "$@"

# YAML is used only for this 00 AA MCS251 NOP/ERET helper because the target
# has no ELF-safe assembler parser. The helper is a normal ELF relocatable input.
$(BUILD)/elf-nop.o: $(ELF_NOP_YAML) | $(BUILD)
	$(YAML2OBJ) "$<" -o "$@"

$(CRT): $(CRT_YAML) | $(BUILD)
	$(YAML2OBJ) "$<" -o "$@"

$(BUILD)/minimal.elf: $(BUILD)/main.o $(BUILD)/elf-nop.o $(CRT)
	$(LLD) -flavor mcs251 $(LDFLAGS) -o "$@" $^

# $< is deliberately the linked ELF, never a .rel or raw binary.
$(BUILD)/minimal.hex: $(BUILD)/minimal.elf
	$(OBJCOPY) -O ihex "$<" "$@"

run: $(BUILD)/minimal.hex run-qemu.py
	$(PYTHON) run-qemu.py --qemu "$(QEMU)" --image "$<" --serial "$(BUILD)/minimal.serial"

clean:
	@case "$(abspath $(BUILD))" in "$(abspath $(HERE))"/*) rm -rf -- "$(BUILD)" ;; *) \
	  printf '%s\n' 'refusing to clean BUILD outside minimal-direct-sfr: $(BUILD)' >&2; exit 2 ;; esac
