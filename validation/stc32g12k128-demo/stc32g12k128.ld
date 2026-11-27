/*===---- stc32g12k128.ld - GNU-style linker script for STC32G12K128 -------===*
 * STC32G12K128 GNU 风格链接脚本
 *
 * STC32G12K128 on-chip memory map (G1 127-slot IRQ layout-v2):
 * STC32G12K128 片上内存映射（G1 127 槽中断布局 layout-v2）：
 *   Flash : 0xFE0000 .. 0xFFFFFF (128 KiB; all demo ROM slices stay in
 *           0xFF0000..0xFFFFFF so an EEPROM partition <= 1 KiB at 0xFE0000
 *           remains completely untouched)
 *           128 KiB 片上 Flash；本示例所有 ROM 节均位于 0xFF0000..0xFFFFFF，
 *           确保 0xFE0000 起始处 <= 1 KiB 的 EEPROM 分区完全不受影响
 *   IRAM  : 0x0000   .. 0x007F   (128 B direct internal RAM / 128B 直接寻址内部 RAM)
 *   EDATA : 0x0100   .. 0x0FFF   (4 KiB internal extended RAM + stack / 4KB 扩展内部 RAM 与栈区)
 *   XDATA : 0x010000 .. 0x011FFF (8 KiB on-chip XRAM / 8KB 片上 XDATA)
 *
 * VECS (0xFF0003..0xFF03FB, 127 x 8B slots) is synthesized automatically by
 * mcs251-lld when IRQ objects are linked.
 * VECS（0xFF0003..0xFF03FB，共 127 个 8 字节向量槽）在链接含中断对象时由
 * mcs251-lld 自动合成。
 *===-----------------------------------------------------------------------===*/

MEMORY {
  FLASH (rx)  : ORIGIN = 0xFE0000, LENGTH = 128K
  IRAM  (rwx) : ORIGIN = 0x0000,   LENGTH = 128
  EDATA (rw)  : ORIGIN = 0x0100,   LENGTH = 0x0F00
  XDATA (rw)  : ORIGIN = 0x010000, LENGTH = 8K
}

/* Entry symbol (CRT reset trampoline) / 程序入口符号（CRT 复位跳板） */
ENTRY(__mcs251_reset)

/* Exported linker-script symbols / 链接脚本导出符号 */
_fw_flash_origin = ORIGIN(FLASH);
PROVIDE(_fw_version = 0x0100);

SECTIONS {
  /* Reset trampoline at 0xFF0000 / 复位跳转区（3B LJMP） */
  HOME 0xFF0000 : {
    *(.mcs251.HOME)
  } > FLASH

  /* CRT startup & init walkers / CRT 启动与初始化遍历器 */
  BOOT 0xFF0500 : {
    *(.mcs251.BOOT)
  } > FLASH

  /* Application code & read-only data / 应用程序代码与只读常量 */
  .text 0xFF0700 : {
    KEEP(*(.text._fw_info_keep))
    *(.text*)
    *(.rodata*)
  } > FLASH

  /* Initialized DATA/EDATA copy table / 内部 RAM 初值拷贝表 */
  XINIT 0xFF8000 : {
    *(.mcs251.xinit*)
  } > FLASH

  /* Initialized XDATA copy/clear table / XDATA 初值拷贝与清零表 */
  XDATA_INIT 0xFF9000 : {
    *(.mcs251.xdata_init*)
  } > FLASH

  /* XDATA global variables / XDATA 全局变量区 */
  .xdata 0x010000 : {
    *(.mcs251.XSEG*)
  } > XDATA
}
