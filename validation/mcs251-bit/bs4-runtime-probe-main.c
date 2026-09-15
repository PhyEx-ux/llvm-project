/*
 * bs4-runtime-probe-main.c - host entry shim for the B-S4 runtime probe.
 *
 * The probe TU defines its entry point as `main` on the target (the v2 crt
 * calls _main); on the host the BS4_HOST_ORACLE arm renames it to bs4_main
 * so this file can own the real host main.  Keeping the shim in its own
 * translation unit means the probe source stays one file with two builds.
 */
void bs4_main(void);

int main(void)
{
    bs4_main();
    return 0;
}
