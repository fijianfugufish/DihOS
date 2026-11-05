OUTPUT_FORMAT(elf64-littleaarch64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); ro PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); dynamic PT_DYNAMIC FLAGS(6); tls PT_TLS FLAGS(4); }
SECTIONS {
  . = 0;
  .text : { *(.text .text.*) } :text
  . = ALIGN(4096);
  .rodata : { *(.rodata .rodata.*) *(.data.rel.ro .data.rel.ro.*) } :ro
  .init_array : ALIGN(8) {
    __init_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))
    __init_array_end = .;
  } :ro
  .rela.dyn : ALIGN(8) { *(.rela.dyn) } :ro
  .dynsym : { *(.dynsym) } :ro
  .dynstr : { *(.dynstr) } :ro
  .hash : { *(.hash) } :ro
  .gnu.hash : { *(.gnu.hash) } :ro
  . = ALIGN(4096);
  .data : {
    /* WebKit uses these section names on the native target as well. They are
       written during engine initialization, before any logical config freeze. */
    *("__DATA,__wtf_config") *("__DATA,__jsc_opcodes")
    *(.data .data.*) *(.got .got.*)
  } :data
  .tdata : ALIGN(16) { *(.tdata .tdata.*) } :data :tls
  .tbss : ALIGN(16) { *(.tbss .tbss.*) } :data :tls
  .dynamic : { *(.dynamic) } :data :dynamic
  .bss : { *(.bss .bss.* COMMON) } :data
  /DISCARD/ : { *(.comment .eh_frame .note*) }
}
