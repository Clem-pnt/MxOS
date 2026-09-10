/* Script de link pour les programmes utilisateur ELF32 relogeables.
 * L'adresse 0x300000 (LINK_BASE dans kernel/exec.c) est l'adresse de LIEN
 * (link-time), pas nécessairement l'adresse de CHARGEMENT : grâce à
 * `ld --emit-relocs` (cf. Makefile) et au chargeur kernel/elf.c, ce même
 * binaire peut être chargé et relogé à une autre adresse (les autres
 * emplacements `exec` concurrents). */
ENTRY(user_main)
SECTIONS
{
    . = 0x300000;
    .text : {
        *(.text.start)  /* Force user_main en tout premier (= point d'entree) */
        *(.text)
    }
    .rodata : { *(.rodata*) *(.rdata*) }
    .data : { *(.data) }
    .bss : { *(.bss) *(COMMON) }
    /DISCARD/ : { *(.eh_frame) *(.comment) }
}
