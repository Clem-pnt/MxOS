ENTRY(main_entry)
SECTIONS
{
    . = 0x8000;
    .text : {
        *(.text.main)  /* ICI : On force l'entrée en premier */
        *(.text)
    }
    .rodata : { *(.rodata*) *(.rdata*) }
    .data : { *(.data) }
    .bss  : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
    /DISCARD/ : { *(.eh_frame) *(.comment) }
}