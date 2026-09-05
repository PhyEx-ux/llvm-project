# wildcard-recursive

- 来源：`84-MP3播放器/3rd/ff/ff.c:2820-2864`
- 原函数签名：`static int pattern_match(const TCHAR *pat, const TCHAR *nam, UINT skip, UINT recur)`
- 选案：指针遍历、通配符移位/跳过和递归分支；覆盖递归路线。
- 改写：显式 `u8/u16/u32` typedef；`TCHAR` 归一为 u8、`UINT` 归一为 u16；去掉 FatFs/SD/printf 依赖；保留 `?`/`*` 分支、递归计数和最终布尔返回。
- 参数打包降维：原生四参数函数改为 `match_args *` 单参数上下文，递归分支显式构造下一帧；原生多参签名保留在本文件说明中，避免把此用例误作 OSEG 正向测试。
- 可观察输出：返回匹配/不匹配结果。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；无 kernel error/warning。
