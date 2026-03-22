/* PIE kernel for AArch64 */
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(kmain)

/* For ET_DYN, put VMA at 0; loader will relocate by adding a base */
SECTIONS
{
  . = 0;

  .text : { *(.text .text.*) }

  .rodata : { *(.rodata .rodata.*) }

  /* keep reloc sections so the loader can process RELATIVE relocs if needed */
  .rela.dyn : { *(.rela.dyn) }
  .rela.plt : { *(.rela.plt) }

  .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) }
  .data : { *(.data .data.*) }

  .bss : { *(.bss .bss.* COMMON) }

  /* useful tables */
  .got : { *(.got .got.*) }
  .got.plt : { *(.got.plt) }
  .plt : { *(.plt .iplt) }

  /DISCARD/ : { *(.comment*) }
}
