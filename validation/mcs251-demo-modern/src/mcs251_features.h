/*
 * features.h — 各现代 C 特性展示函数的声明。
 *
 * 约定：每个 feat_* 函数返回一个计算结果，main() 用它与会期望值比对；
 * 期望值来自 C 语言语义本身（宿主机 gcc 跑同一份源码生成），与编译器无关。
 * 函数全部返回 u32 以便统一打印比对。
 */
#ifndef MCS251_DEMO_MCS251_FEATURES_H
#define MCS251_DEMO_MCS251_FEATURES_H

#include <stdint.h>

/* C99：块内声明、for 内声明、// 行注释 */
uint32_t feat_c99_decl(void);

/* stdint 精确宽度 + u32 乘法（原生 MUL）+ u16 无符号除法（udiv libcall） */
uint32_t feat_stdint_math(void);

/* 变量移位与循环移位（rotate）：移位次数来自运行时输入 */
uint32_t feat_shift_var(uint32_t seed);

/* bool 类型与短路求值（验证求值顺序的副作用） */
uint32_t feat_bool_logic(void);

/* C99 指定初始化器：结构体 .field 与数组 [index] 两种形态 */
uint32_t feat_designated_init(void);

/* const 全局查表（rodata CSEG）+ 字符串字面量：CRC8 查表算法 */
uint32_t feat_const_table(void);

/* 可变全局：非零初值（XINIT 拷贝）+ BSS 零初始化 + 修改读回 */
uint32_t feat_mutable_globals(void);

/* 多参数函数（OSEG/DSEG 静态参数槽）：4 参混合宽度，内部再嵌套调用 */
uint32_t feat_multiparam(void);

/* 递归：斐波那契与阶乘（u32 域内） */
uint32_t feat_recursion(void);

/* 函数指针表 + 间接调用（运行时构造表，单参签名） */
uint32_t feat_funcptr(uint8_t sel);

/* 指针算术：遍历数组求和与最大值 */
uint32_t feat_pointer_walk(void);

/* 嵌套结构体全局变量（聚合非零初值，走 XINIT）+ 字段修改 */
uint32_t feat_struct_nest(void);

/* C99 inline：小函数内联后参与计算 */
uint32_t feat_inline(void);

/* C11 _Static_assert 的运行时侧写（断言本体在 features.c 文件级） */
uint32_t feat_static_assert(void);

/* extern 可变全局：供 main() 统计失败数（本身也是 XINIT/BSS 能力的展示） */
extern uint8_t g_fail_count;

#endif /* MCS251_DEMO_MCS251_FEATURES_H */
