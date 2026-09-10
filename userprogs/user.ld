/* Script de link pour les programmes utilisateur "flat binary".
 * Doit rester cohérent avec USER_PROG_BASE défini dans kernel/exec.c :
 * l'adresse de chargement 0x300000 est fixe (pas de chargeur relogeable),
 * exactement comme le noyau lui-même est fixé à 0x8000 par linker.ld. */
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
