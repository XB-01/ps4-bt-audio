ENTRY(_start)
SECTIONS {
 . = 0;
 __image_start = .;
 .text : {
    __payload_jump = .;
    BYTE(0xe9);
    LONG(_start - __payload_jump - 5);
    *(.text.launcher)
    *(.text*)
  }
 .rodata : { *(.rodata*) }
 .data : { *(.data*) }
 .bss : { *(.bss*) *(COMMON) }
 .tail : { BYTE(0); }
 __image_end = .;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
