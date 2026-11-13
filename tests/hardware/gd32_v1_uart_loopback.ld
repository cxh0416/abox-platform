ENTRY(Reset_Handler)
MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0xE000 }
_estack = 0x20010000;
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > RAM
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > RAM
  .data : { *(.data*) . = ALIGN(4); } > RAM
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note.GNU-stack) }
}
