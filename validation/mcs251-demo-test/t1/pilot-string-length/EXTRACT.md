# EXTRACT — t1/pilot-string-length

## 出处
- demo 37-科学计算器使用CDC虚拟液晶屏显示/keyboard.c:8
  `u8 String_length(char* str)`（30 上限有界 strlen，0xff 哨兵）

## 改写清单（详见 kernel.c 头注释）
1. u8 typedef；循环体逐字保持
2. **指针参数降维（授权路线 2 的指针变体）**：`char* str` 形参改为全局
   缓冲 g_str_buf（驱动填写），遍历用数组下标。实测依据（2026-09-05
   probe）：SDCC 对未限定指针形参/局部指针遍历生成 __gptrget（strict 链
   无 libc，Oracle-B 链接失败）；数组下标遍历在三个编译器都生成直接
   寻址。C 语义 p[i]==*(p+i)，遍历形态保留在内核内。
   通用指针跨编译器调用约定（SDCC 3 字节 gptr vs clang/llc 指针）落地
   并 QEMU 实测后恢复原签名。
3. kernel 全局改 extern 声明（后端 defined global data 缺口），定义在
   wrapper.c / host-main.c
4. wrapper 向量显式初始化（c1mode 丢字符串字面量初始化，probe 实测）

## 判定
- 8 个 checkpoint：空串/1/2/12 字符/28/29/恰好 30 无 NUL（0xff 哨兵）×2
