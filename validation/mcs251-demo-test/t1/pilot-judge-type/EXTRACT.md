# EXTRACT — t1/pilot-judge-type

## 出处
- demo 37-科学计算器使用CDC虚拟液晶屏显示/Resource/alg_caculate.c:101
  `TYPE_FLAG alg_judge_type(char c)`（输入字符分类状态机：普通/复数两
  模式，OPERATOR/NUMBER/CONST_NUM/FUNCTION/INVALID_TYPE）
- 依赖一并提取：alg_const_handle（alg_caculate.c:176）——真实 demo 调用
  链保留（judge_type→const_handle）
- 枚举 TYPE_FLAG：Resource/alg_linearlist.h:19（NUMBER=0..INVALID_TYPE=4）
- 全局 g_chCalcStatus（原 extern uchar，菜单层设置）保留全局角色

## 改写清单（详见 kernel.c 头注释）
1. uchar→u8；两个枚举→u8 字面宏（成员序保持）；PI_FONT/DEGREE_FONT 折入
2. char 形参→u8（两个函数）：等价论证同 pilot-const-handle（全 ASCII
   字面量分支；>=0x80 字节两个方向同落默认分支）
3. switch→if/else 链 ×2（br_jt 缺口）
4. g_chCalcStatus extern 化（defined global 缺口）；驱动侧两模式切换
   + 未定义模式值（7）落入最终 INVALID_TYPE 的穿透路径

## 判定
- 15 个 checkpoint：普通模式 9（算符含 'd' DEGREE_FONT、数字、'.'、
  常数 'e'/'p'、函数首字母 's'、'i' 与杂字符 INVALID）+ 复数模式 5
  （'i' 变算符、'd' 不再是算符、'x' 常数、's' INVALID、'('）+ 未定义
  模式 1
