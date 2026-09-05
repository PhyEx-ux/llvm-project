/*
 * T1 pilot kernel: String_length -- bounded strlen walk.
 *
 * Source: STC32G144K246 demo 37 (scientific calculator),
 *   37-科学计算器使用CDC虚拟液晶屏显示/keyboard.c:8
 *   original signature: u8 String_length(char* str)
 *
 * Rewrite per DESIGN.md section 2-T1 plus the approved "argument packing"
 * route: the pointer PARAMETER becomes a global input buffer, and the walk
 * uses array indexing.  Measured reason (SDCC probe, 2026-09-05, recorded in
 * RESULTS.md): SDCC lowers unqualified `char*` locals/parameters to 3-byte
 * generic pointers and emits __gptrget calls for them -- the strict link has
 * no libc, so Oracle-B cannot even link; the clang/llm pointer argument ABI
 * vs SDCC generic pointers is separately unmeasured.  Array indexing over a
 * global (`p[i]`, which C defines as *(p+i)) keeps the walk shape while
 * compiling to plain direct addressing on all three compilers.
 * Restore the original one-pointer-parameter signature once the
 * generic-pointer call ABI lands and is QEMU-verified; tracked in RESULTS.md.
 *
 * Other rewrites: u8 typedef only; 30-element bound and 0xff sentinel kept.
 */

typedef unsigned char u8;

/*
 * Storage note: the buffer is declared, not defined, here.  The MCS251
 * backend currently rejects defined global data ("defined global data is
 * not supported yet ... data-area support is Step 2 of the Phase 12 plan",
 * llc 24, 2026-09-05), so mutable kernel state follows the matrix.ll shape:
 * extern declaration in the module under test, definition in the driver
 * (wrapper.c on target, host-main.c on host).  Zero-init semantics are
 * identical on both sides.
 */
extern char g_str_buf[64];   /* harness-filled input, NUL-terminated (or not) */

u8 String_length(void)
{
    u8 i;
    for (i = 0; i < 30; i++) {
        if (g_str_buf[i] == '\0')
            return i;
    }
    return 0xff;
}
