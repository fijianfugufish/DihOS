ENTRY(_start)
SECTIONS { . = 0x40080000; .text : { *(.text.boot) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } . = ALIGN(4096); __bss_start = .; .bss (NOLOAD) : { *(.bss*) *(COMMON) } . = ALIGN(16); __bss_end = .; /DISCARD/ : { *(.eh_frame*) *(.comment*) } }
