/* Premier "vrai" programme utilisateur MxOS : compilé/lié séparément du
 * noyau en un véritable exécutable ELF32 i386 (cf. userprogs/user.ld et
 * les règles Makefile dédiées, qui conservent les relocations via
 * `ld --emit-relocs`), chargé et RELOGÉ par kernel/elf.c avant exécution,
 * plutôt que compilé dans l'image du noyau comme l'était UserDemo. Il
 * n'utilise que l'interface syscall INT 0x80 (via userprogs/libc.h),
 * exactement comme le ferait un programme utilisateur réel.
 */
#include "libc.h"

/* Placé dans .text.start (cf. user.ld) pour être le tout premier octet du
 * binaire : e_entry pointera dessus, et c'est cette adresse (une fois
 * relogée par kernel/elf.c) qui sert d'EIP initial de la tâche ring3. */
void user_main(void) __attribute__((section(".text.start")));

void user_main(void) {
    // argc/argv sont transmis par kernel/exec.c via EBX/ECX (cf.
    // create_user_task_argv, kernel/sched.c) : à lire ICI en tout premier,
    // avant que le prologue du compilateur ne puisse réutiliser ces
    // registres. Convention "à la main" (pas de vrai ABI ici, c'est un OS
    // éducatif) : EBX = argc, ECX = argv (tableau de argc pointeurs + NULL).
    uint32_t argc;
    char **argv;
    MXOS_READ_ARGV(argc, argv);

    sys_print("\n[UserProg] Hello depuis un programme charge du disque (exec) !\n");

    if (argc > 0) {
        sys_print("[UserProg] Arguments recus : ");
        for (uint32_t i = 0; i < argc; i++) {
            sys_print(argv[i]);
            sys_print(" ");
        }
        sys_print("\n");
    }

    sys_exit();
}
