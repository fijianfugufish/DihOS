OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5); /* R | X */
  data PT_LOAD FLAGS(6); /* R | W */
  /* Mesa's generated device table contains ET_DYN relative pointers.  Make
   * the dynamic metadata discoverable to DihOS's verified EL0 loader so it
   * can resolve precisely those pointers before mapping the image. */
  dynamic PT_DYNAMIC FLAGS(4); /* R */
}

SECTIONS
{
  . = 0;
  .text : { *(.text .text.*) } :text
  .rodata : { *(.rodata .rodata.*) } :text
  .dynamic : { *(.dynamic) } :text :dynamic
  /* The verified EL0 loader maps every PT_LOAD at a page boundary.  Mesa's
   * generated tables enlarged rodata enough to expose the old accidental
   * unaligned data segment; keep writable relocations in a separate page. */
  . = ALIGN(0x1000);
  .data : { *(.data .data.*) } :data
  .bss : { *(.bss .bss.* COMMON) } :data
  /DISCARD/ : { *(.comment*) *(.note*) }
}
