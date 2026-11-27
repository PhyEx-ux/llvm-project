/*===-- mcs251_atomic.c ---------------------------------------------------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/*
 * MCS-251 单核 MCU 通用原子运行时（generic __atomic_* 与 fence 函数兜底）。
 *
 * 1/2/4 字节标量原子操作已由后端 MCS251AtomicExpand 内联展开为基于硬件
 * 全局中断位 EA（IE.7，位地址 0xAF）的 `jbc 0xAF` / `setb 0xAF` 临界区；
 * 本模块提供任意宽度聚合（如 >4B 的 _Atomic(struct) 或非 2 的幂宽度）所需
 * 的标准 C11/GNU 通用 libcall，以及非内联 fence/is_lock_free 入口：
 *   - __atomic_load
 *   - __atomic_store
 *   - __atomic_exchange
 *   - __atomic_compare_exchange
 *   - __atomic_is_lock_free
 *   - atomic_thread_fence / atomic_signal_fence
 *
 * 临界区语义：
 *   - 进入：__builtin_mcs251_ea_save_disable() 原子测试并清除 EA（单条
 *     jbc 0xAF 指令，无竞态窗口），返回进入前 EA 状态（0 或 1）。
 *   - 退出：__builtin_mcs251_ea_restore(saved) 仅当 saved != 0 时置位 EA
 *     （setb 0xAF），不触碰 IE.0..6 且严格保持嵌套/ISR 内已关中断状态。
 *
 * 红线：纯 C11；无 64 位类型；无浮点；无内联汇编；完全可重入（Route B 栈 ABI）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Clang forbids redeclaring the builtin names __atomic_load/store/exchange/
 * compare_exchange/is_lock_free directly in C. `#pragma redefine_extname`
 * emits a verbatim `\01` asm name (bypassing the `m:s` `_` prefix), so on
 * MCS-251 target we export the exact mangled ELF names `___atomic_*` that
 * callers of `@__atomic_*` reference. */
#ifdef MCS251_RT_TARGET
#pragma redefine_extname mcs251_atomic_load ___atomic_load
#pragma redefine_extname mcs251_atomic_store ___atomic_store
#pragma redefine_extname mcs251_atomic_exchange ___atomic_exchange
#pragma redefine_extname mcs251_atomic_compare_exchange ___atomic_compare_exchange
#pragma redefine_extname mcs251_atomic_is_lock_free ___atomic_is_lock_free
#else
#pragma redefine_extname mcs251_atomic_load __atomic_load
#pragma redefine_extname mcs251_atomic_store __atomic_store
#pragma redefine_extname mcs251_atomic_exchange __atomic_exchange
#pragma redefine_extname mcs251_atomic_compare_exchange __atomic_compare_exchange
#pragma redefine_extname mcs251_atomic_is_lock_free __atomic_is_lock_free
#endif

static uint8_t mcs251_ea_enter(void)
{
#ifdef MCS251_RT_TARGET
    return __builtin_mcs251_ea_save_disable();
#else
    return 0u;
#endif
}

static void mcs251_ea_exit(uint8_t saved)
{
#ifdef MCS251_RT_TARGET
    __builtin_mcs251_ea_restore(saved);
#else
    (void)saved;
#endif
}

void mcs251_atomic_load(size_t size, const void* src, void* dst, int order)
{
    uint8_t saved = mcs251_ea_enter();
    const volatile uint8_t* sp = (const volatile uint8_t*)src;
    uint8_t* dp = (uint8_t*)dst;
    size_t i;
    (void)order;
    for (i = 0u; i < size; ++i) {
        dp[i] = sp[i];
    }
    mcs251_ea_exit(saved);
}

void mcs251_atomic_store(size_t size, void* dst, const void* src, int order)
{
    uint8_t saved = mcs251_ea_enter();
    volatile uint8_t* dp = (volatile uint8_t*)dst;
    const uint8_t* sp = (const uint8_t*)src;
    size_t i;
    (void)order;
    for (i = 0u; i < size; ++i) {
        dp[i] = sp[i];
    }
    mcs251_ea_exit(saved);
}

void mcs251_atomic_exchange(size_t size, void* ptr, const void* val, void* ret,
                            int order)
{
    uint8_t saved = mcs251_ea_enter();
    volatile uint8_t* mp = (volatile uint8_t*)ptr;
    const uint8_t* vp = (const uint8_t*)val;
    uint8_t* rp = (uint8_t*)ret;
    size_t i;
    (void)order;
    for (i = 0u; i < size; ++i) {
        uint8_t old_b = mp[i];
        rp[i] = old_b;
        mp[i] = vp[i];
    }
    mcs251_ea_exit(saved);
}

bool mcs251_atomic_compare_exchange(size_t size, void* ptr, void* expected,
                                    const void* desired, int success_order,
                                    int failure_order)
{
    uint8_t saved = mcs251_ea_enter();
    volatile uint8_t* mp = (volatile uint8_t*)ptr;
    uint8_t* ep = (uint8_t*)expected;
    const uint8_t* dp = (const uint8_t*)desired;
    size_t i;
    bool eq = true;
    (void)success_order;
    (void)failure_order;
    for (i = 0u; i < size; ++i) {
        if (mp[i] != ep[i]) {
            eq = false;
            break;
        }
    }
    if (eq) {
        for (i = 0u; i < size; ++i) {
            mp[i] = dp[i];
        }
    } else {
        for (i = 0u; i < size; ++i) {
            ep[i] = mp[i];
        }
    }
    mcs251_ea_exit(saved);
    return eq;
}

bool mcs251_atomic_is_lock_free(size_t size, const volatile void* ptr)
{
    (void)ptr;
    return size == 1u || size == 2u || size == 4u;
}

void atomic_thread_fence(int order)
{
    (void)order;
    __c11_atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void atomic_signal_fence(int order)
{
    (void)order;
    __c11_atomic_signal_fence(__ATOMIC_SEQ_CST);
}
