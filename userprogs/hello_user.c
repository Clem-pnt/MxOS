/* Premier "vrai" programme utilisateur MxOS : compilé/lié séparément du
 * noyau en un véritable exécutable ELF32 i386 (cf. userprogs/user.ld et
 * les règles Makefile dédiées, qui conservent les relocations via
 * `ld --emit-relocs`), chargé et RELOGÉ par kernel/elf.c avant exécution,
 * plutôt que compilé dans l'image du noyau comme l'était UserDemo. Il
 * n'utilise que l'interface syscall INT 0x80, exactement comme le ferait
 * un programme utilisateur réel.
 */
#include <stdint.h>

#define SYS_PRINT 1
#define SYS_EXIT  2

static void sys_print(const char *msg) {
    __asm__ volatile("int $0x80" : : "a"(SYS_PRINT), "b"(msg) : "memory");
}

static void sys_exit(void) {
    __asm__ volatile("int $0x80" : : "a"(SYS_EXIT));
}

/* Placé dans .text.start (cf. user.ld) pour être le tout premier octet du
 * binaire : e_entry pointera dessus, et c'est cette adresse (une fois
 * relogée par kernel/elf.c) qui sert d'EIP initial de la tâche ring3. */
void user_main(void) __attribute__((section(".text.start")));

void user_main(void) {
    // argc/argv sont transmis par kernel/exec.c via EBX/ECX (cf.
    // create_user_task_argv, kernel/sched.c), lus ICI en tout premier avant
    // que le prologue du compilateur ne puisse réutiliser ces registres.
    // Convention "à la main" (pas de vrai ABI ici, c'est un OS educatif) :
    // EBX = argc, ECX = argv (tableau de argc pointeurs + NULL final).
    uint32_t argc;
    char **argv;
    __asm__ volatile("" : "=b"(argc), "=c"(argv));

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

    /* Jamais atteint : sys_exit() ne revient pas. */
    for (;;) { }
}
