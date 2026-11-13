ENTRY(Reset_Handler)
MEMORY {
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 0x7F00
  DESC (rx) : ORIGIN = 0x08007F00, LENGTH = 0x100
  RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 0xBC40
  MAILBOX (xrw) : ORIGIN = 0x2000BC40, LENGTH = 0x14
}
_estack = 0x20010000;
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH
  .ARM.extab : { *(.ARM.extab*) } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  _heap_start = _ebss;
  _heap_end = ORIGIN(RAM) + LENGTH(RAM);
  .noinit (NOLOAD) : { KEEP(*(.noinit)) } > MAILBOX
  .boot_descriptor : { KEEP(*(.boot_descriptor)) } > DESC
  /DISCARD/ : { *(.note.GNU-stack) }
}
ASSERT(SIZEOF(.boot_descriptor) == 256, "missing Boot descriptor")
ASSERT(SIZEOF(.noinit) <= 0x14, "mailbox exceeds reserved RAM")
/* A 2 KiB page at offset 0x7000 is reserved for controlled bench testing. */
ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(FLASH) + 0x7000, "Boot overlaps reserved test page")
