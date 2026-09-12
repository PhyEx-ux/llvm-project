/*
 * stub-main.c - link anchor for the two byte-chain images (keil/modern).
 * The CRT always ECALLs _main; the byte-chain images exist for object/link/
 * byte assertions, not for running, so their entry just spins.  The QEMU
 * firmware (qemu-fw.c) has the real main.
 */
int main(void)
{
    for (;;)
        ;
}
