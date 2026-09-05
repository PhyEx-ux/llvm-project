# wildcard-recursive

- 来源：`84-MP3播放器/3rd/ff/ff.c:2820-2864`
- 原函数签名：`static int pattern_match(const TCHAR *pat, const TCHAR *nam, UINT skip, UINT recur)`
- 选案：指针遍历、通配符移位/跳过和递归分支；覆盖递归路线。
- 改写：显式 `u8/u16/u32` typedef；`TCHAR` 归一为 u8、`UINT` 归一为 u16；去掉 FatFs/SD/printf 依赖；保留 `?`/`*` 分支、递归计数和最终布尔返回。
- 参数打包降维：原生四参数函数改为 `match_args *` 单参数上下文；pattern/name 改为驱动定义的全局数组，递归帧仅传位置与控制字段，遍历使用索引，递归分支显式构造下一帧；原生多参签名保留在本文件说明中，避免把此用例误作 OSEG 正向测试。
- 可观察输出：返回匹配/不匹配结果。
- 编译自检：GCC `-c -Wall -Wextra -std=c89` 通过（无 warning）；SDCC `-mmcs251 --model-small -c` 通过。SDCC driver 输出两条环境 warning：`__has_builtin` 与 `__STDC_HOSTED__` redefined；kernel 无 error；SDCC 另有一条 optimizer warning：`kernel.c:47: warning 110: conditional flow changed by optimizer`，不影响 rc=0。
- 宿主死循环根因与修复：原始 `pattern_match` 每轮只重置扫描指针 `nptr`，保留重试基址 `nam` 的前移；提取版把同一 `name_pos` 同时当作两者，并在 `do` 开头重置为 `args->name_pos`，`skip=1` 的 `ABC`/`XABC` 因而永远重复首轮。新增 `scan_name_pos` 分离扫描指针，保留 `name_pos` 作为重试基址；每轮结束仅前移基址一次，恢复原始递归匹配终止语义。
- 目标链路等价改写：输入/递归上下文显式放入 `__xdata`，测试向量显式放入 `__code`，避免 SDCC 生成未提供的 `__gptrget` helper；这只改变 MCS-251 地址空间注解，不改变数组字节、递归参数或匹配结果。wrapper 的 `match_args` 使用 `unsigned short` 与 kernel 的 `u16` ABI 对齐，kernel 仍保持 `u16`。
- Oracle-A 真值：`Ba01b01c01d00e01f01g01PASS`；字面期望依次为 `a=1,b=1,c=1,d=0,e=1,f=1,g=1`，其中 `g` 的 `ABC/ABC, skip=0, recurse=0` 是 `1`，不是原 wrapper 的 `0`。
