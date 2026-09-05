# EXTRACT — t1/pilot-led8-nibble

## 出处
- demo 27-红外遥控接收程序(NEC码)-数码管显示用户地址和键值/C语言/main.c:106-116
  main() 的 while(1) 内 B_IR_Press 分支（用户码/键码拆 nibble 填 LED8
  显示缓冲）
- **授权路线 3（主循环体提取）**：DisplayScan()/SFR 写留在 demo，纯计算
  段提取为零参内核（读全局输入、写全局输出缓冲）

## 改写清单（详见 kernel.c 头注释）
1. bit→u8（B_IR_Press）；LED8/UserCode/IR_code 全局角色不变（extern 化）
2. **UserCode 保持原生 u16 形态（基线重置 2026-09-05）**：早先的 "拆双 u8
   (UserCodeH/UserCodeL)" 是绕过 trunc(load i16) 旧端序误编译
   (0x1234→0x1212)，该 bug 已被 commit 1c59ad49f 修复；新冻结 llc
   (md5 67a17057) 正确读取 SDCC 写入的 u16。故恢复 demo 原生 u16 形态，
   kernel.c 已用 `extern u16 UserCode`。若在旧工具上跑需回退拆分版。
3. 移位保持 demo 原形（runner 临时 lowering 展开）

## 判定
- 21 个 checkpoint：0xABCD/0xEF、全 0、0xFFFF/0x99、0x1234/0x56 四组
  正常路径 + B_IR_Press=0 不动作路径（缓冲哨兵 0x5A/0xA5 保持）
- **三方一致 PASS（忠实 u16 形态）**：新基线 llc(67a17057, post-端序
  修复) 下 DUT 串口
  `Ba0Ab0Bc0Cd0De0Ef0Fg00h00i00j00k0Fl09m01n02o03p04q05r06s5AtA5u00PASS`，
  与 Oracle-A/B 一致。旧工具上需回退拆分版（见改写清单 2）。
