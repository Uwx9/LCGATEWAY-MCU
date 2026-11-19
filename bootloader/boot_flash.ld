ENTRY(Reset_Handler)

MEMORY
{
  /* bootloader 代码占前 26K；0x08006800/0x08006C00 两页留给 metadata */
  FLASH    (rx): ORIGIN = 0x08000000, LENGTH = 0x6800
  RAM      (xrw): ORIGIN = 0x20000000, LENGTH = 16K
}

SECTIONS
{
  __stack_size = DEFINED(__stack_size) ? __stack_size : 0x400;

  .vectors :
  {
    . = ALIGN(4);
    KEEP(*(.vectors))
    . = ALIGN(4);
    __Vectors_End = .;
    __Vectors_Size = __Vectors_End - __gVectors;
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
    __rodata_end = .;
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  .heap_stack :
  {
    . = ALIGN(8);
    . = . + __stack_size;
    PROVIDE(_sp = .);
    . = ALIGN(8);
  } >RAM

  ASSERT(_sidata + SIZEOF(.data) <= 0x08006800,
         "bootloader code exceeds 26KB")
}
