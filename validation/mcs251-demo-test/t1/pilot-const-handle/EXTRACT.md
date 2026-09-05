# EXTRACT — t1/pilot-const-handle

## 出处
- demo 37-科学计算器使用CDC虚拟液晶屏显示/Resource/alg_caculate.c:176
  `uchar alg_const_handle(char c)`（常数字符判定，switch 查找）
- 枚举 CONST_FLAG：Resource/alg_caculate.h:14（E_FLAG=0..NO_CONST=6）
- 宏 PI_FONT='p' / DEGREE_FONT='d'：alg_caculate.c:12-13

## 改写清单（类型层，语义不变；详见 kernel.c 头注释）
1. uchar→u8；enum→u8 字面宏（成员序保持）
2. char 形参→u8：后端无 8→16 符号扩展（llc 报错实测）；本函数全部分支
   比较 ASCII 字面量，>=0x80 字节在 signed char（负）与 u8（>=128）下
   同样落入默认分支 NO_CONST，等价论证记录于 kernel.c
3. switch→if/else 链：后端无 br_jt 选择（llc Cannot select 实测）；
   稀疏 switch 的同构控制流
4. 无 SFR/中断/bit 构造，无多参，保持单参

## 判定
- checkpoint 10 个：'e'/'p'/'x'/'y'/'z'/'A' 六常数 + 'q'/'+’/'0'/'P' 四个
  NO_CONST 路径（含大小写敏感）
- 期望值冻结自 Oracle-A（gcc host 真值），三方 diff 兜底
