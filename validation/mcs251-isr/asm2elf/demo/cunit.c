/* cunit.c - E4 acceptance demo, C-side object.
 *
 * Compiled with the MCS251 memory contract used by the realhw-demo build
 * (-mcs251-memory-contract=1,1,32,8,1).  The object carries its own
 * .note.mcs251.abi and the static parameter slot sections, so the link
 * also exercises asm-object / C-object coexistence in one link.
 */
volatile unsigned char c_flag;

int c_add(int a, int b) { return a + b; }
