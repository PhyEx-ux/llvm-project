# EXTRACT — t1/pilot-delay-smoke

## 出处
- demo 00-端口模式设置/C语言/main.c:118 `void delay_ms(u16 ms)`
  （忙等延时：do { i = MAIN_Fosc/6000; while(--i); } while(--ms)）

## 定位
- DESIGN.md §2-T1：delay 类只做"能编译+能终止"冒烟，无行为 oracle
- 目录无 host-main.c（runner 约定：缺 host-main 即 smoke 用例）
- 同时回归"空循环不被优化死"：clang 全用例 -O0（-O1 会删除空延时循环
  ——实测 delay_ms 在 -O1 下被删成空函数），循环必须真实执行并返回才
  能到达 PASS

## 改写清单
1. u16 typedef；MAIN_Fosc/6000 = 48000000/6000 = 8000 折算字面量
   （-O0 下 u32/u32 除法会 lower 成运行时 helper 调用，裸链无运行时库；
   常量折叠是任何优化前端的必做变换，循环结构逐字保持）

## 判定
- smoke：DUT 链到达 PASS（QEMU 内 delay_ms(2)≈16000 次循环迭代真实执行）
