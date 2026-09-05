/*
 * T1 pilot kernel: sum_sfn -- FAT short-filename checksum (shift + add).
 *
 * Source: STC32G144K246 demo 84 (MP3 player), bundled FatFs,
 *   84-MP3播放器/3rd/ff/ff.c:2076  (static BYTE sum_sfn(const BYTE* dir))
 *
 * Rewrite per DESIGN.md section 2-T1 plus the approved "argument packing"
 * route: the pointer PARAMETER becomes a global input buffer, the walk uses
 * array indexing.  Measured reason (SDCC probe, 2026-09-05, recorded in
 * RESULTS.md): unqualified pointer locals/parameters lower to 3-byte generic
 * pointers with __gptrget calls that the libc-free strict link cannot
 * resolve; array indexing compiles to plain direct addressing on all three
 * compilers.  Original signature to be restored once the generic-pointer
 * call ABI is QEMU-verified.
 *
 * Semantics unchanged: 11 iterations of
 *     sum = (sum >> 1) + (sum << 7) + dir_byte++;
 * (sum>>1)+(sum<<7)+byte can exceed a 16-bit signed int; the low 8 bits of a
 * wrapped two's-complement sum still equal the mathematical value mod 256,
 * so the u8 result is identical across gcc/SDCC/clang.  Recorded here
 * because it is exactly the "real front-end shape" T1 exists to pin.
 */

typedef unsigned char u8;
typedef unsigned short u16;

/* Storage note: extern declaration only -- the MCS251 backend rejects
 * defined global data (Phase 12 Step 2 pending); the driver defines it
 * (wrapper.c on target, host-main.c on host).  Zero-init on both sides. */
extern u8 g_sfn_dir[11];      /* harness-filled SFN entry */

u8 sum_sfn(void)
{
    u8 sum = 0;
    u16 n = 11;
    u16 i = 0;

    do {
        sum = (sum >> 1) + (sum << 7) + g_sfn_dir[i];
        i++;
    } while (--n);
    return sum;
}
