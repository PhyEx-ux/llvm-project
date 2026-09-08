# Tiny/XTiny M9 demo — 分 TU 指针参数保留验证

验证 Tiny 模型下 O2 优化后静态指针参数不被消除、_PARM_2 槽保留。

## 源文件
- `src/demo.c`：主 demo（SBUF 串口输出、链表遍历、指针参数传递）
- `src/abi_helpers.c`：ABI 助手（跨调用 spill、静态指针参数）

## 构建命令（WSL Debian）

```bash
# Tiny 模型
clang --target=mcs251 -mcs251-memory-model=tiny -O2 \
  -mcs251-object-format=elf \
  -c src/demo.c -o demo.tiny.o
clang --target=mcs251 -mcs251-memory-model=tiny -O2 \
  -c src/abi_helpers.c -o abi_helpers.tiny.o

# XTiny 模型（将 tiny 换成 xtiny）

# 链接
mcs251-lld --area-start HOME=0xFF0100 --area-start VECS=0xFF0003 \
  --area-start BOOT=0xFF0200 --area-start CSEG=0xFC2800 \
  --area-start XINIT=0xFB0000 --map \
  demo.tiny.o abi_helpers.tiny.o .../crt.o -o demo.elf

# 转 HEX
llvm-objcopy -O ihex demo.elf demo.hex
```

## 验证要点
- O2 后调用侧和定义侧均保留第二指针参数
- `.ds 2`（2B 静态槽）存在
- 跨调用 spill 保留
