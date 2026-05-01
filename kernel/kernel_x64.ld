/* PIE kernel for x86_64 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kmain)

/* For ET_DYN, put VMA at 0; loader will relocate by adding a base */
SECTIONS
{
  . = 0;

  .text : { *(.text .text.*) *(.plt .iplt) }

  .rodata : { *(.rodata .rodata.*) }

  /* keep reloc sections so the loader can process RELATIVE relocs if needed */
  .rela.dyn : { *(.rela.dyn) }
  .rela.plt : { *(.rela.plt) }

  /*
   * Keep all RELRO sections contiguous for ld.lld when linking as PIE.
   */
  .relro : ALIGN(16) {
    *(.data.rel.ro .data.rel.ro.*)
    *(.init_array .init_array.*)
    *(.fini_array .fini_array.*)
  }

  .dynamic : { *(.dynamic) }

  .got : {
    *(.got .got.*)
    *(.got.plt)
  }

  .data : { *(.data .data.*) }

  .bss : { *(.bss .bss.* COMMON) }

  /DISCARD/ : { *(.comment*) }
}
