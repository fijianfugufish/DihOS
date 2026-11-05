OUTPUT_FORMAT(elf64-littleaarch64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = 0;
  .text : { *(.text .text.*) } :text
  .rodata : { *(.rodata .rodata.*) } :text
  . = ALIGN(4096);
  .data : { *(.data .data.*) *(.got .got.*) } :data
  .bss : { *(.bss .bss.* COMMON) } :data
  .rela.dyn : { *(.rela .rela.*) } :data
  ASSERT(SIZEOF(.rela.dyn) == 0, "User runtime prototype does not support relocations")
  /DISCARD/ : { *(.dynamic .dynsym .dynstr .hash .gnu.hash .comment .eh_frame .note*) }
}
