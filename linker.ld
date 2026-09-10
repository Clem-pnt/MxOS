ENTRY(main_entry)
SECTIONS
{
    . = 0x8000;
    .text : {
        __text_start = .;
        *(.text.main)  /* ICI : On force l'entrée en premier */
        *(.text)
    }
    .rodata : { *(.rodata*) *(.rdata*) }
    .data : { *(.data) }
    __ro_end = .; /* Fin de la zone code+data partagee (accessible en ring3) */
    .bss  : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
    /DISCARD/ : { *(.eh_frame) *(.comment) }
}