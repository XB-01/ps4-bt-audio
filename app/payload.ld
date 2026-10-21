ENTRY(_start)
SECTIONS {
 . = 0;
 __image_start = .;
 .text : { *(.text.launcher) *(.text*) }
 .rodata : { *(.rodata*) }
 .data : { *(.data*) }
 .bss : { *(.bss*) *(COMMON) }
 .tail : { BYTE(0); }
 __image_end = .;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
