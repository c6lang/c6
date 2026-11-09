/* 引导扇区：整个东西就是 512 字节，装在 0x7C00 */
ENTRY(_start)
SECTIONS
{
  . = 0x7C00;
  .boot : { *(.boot) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
