# EXTRACT — t1/pilot-sum-sfn

## 出处
- demo 84-MP3播放器/3rd/ff/ff.c:2076 `static BYTE sum_sfn(const BYTE* dir)`
  （FatFs LFN 的 SFN 11 字节校验和：sum = (sum>>1)+(sum<<7)+byte 循环
  右移-加法，即 ROR(sum)+byte）

## 改写清单（详见 kernel.c 头注释）
1. BYTE→u8、UINT→u16（n 只计 11 次，宿主更宽 int 不改变行为）
2. 指针参数降维同 pilot-string-length（g_sfn_dir 全局缓冲 + 下标遍历；
   __gptrget probe 依据同前）
3. kernel 全局 extern 化（defined global 缺口）
4. 移位算子保持 demo 原形——由 runner 临时移位 lowering 展开（垫片，
   退役条件见 TEMPLATE.md）
5. 记录：(sum>>1)+(sum<<7)+byte 在 16 位 signed int 可溢出（SDCC），
   补码环绕低 8 位仍等于数学值 mod 256，u8 结果三编译器一致——这正是
   T1 要钉住的"前端真实形态"之一；C 整数提升使 `u8>>1` 变 ashr（同
   值域等价，lowering 已处理）

## 判定
- 6 个 checkpoint：真实 8.3 文件名形状（"FILE       "/"TEST    TXT"）
  + 全 'A' + 高位字节混合 + 全零 + 11 字母
