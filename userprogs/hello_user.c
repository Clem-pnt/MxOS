/* Premier "vrai" programme utilisateur MxOS : un binaire plat, autonome,
 * compilé/lié séparément du noyau (cf. userprogs/user.ld et les règles
 * Makefile dédiées), puis chargé dynamiquement depuis le système de
 * fichiers par `exec` (kernel/exec.c) plutôt que compilé dans l'image du
 * noyau comme l'était UserDemo. Il n'utilise que l'interface syscall
 * INT 0x80, exactement comme le ferait un programme utilisateur réel.
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
 * binaire plat : c'est son adresse de chargement (USER_PROG_BASE) qui sert
 * de point d'entrée EIP lors de la création de la tâche ring3. */
void user_main(void) __attribute__((section(".text.start")));

void user_main(void) {
    sys_print("\n[UserProg] Hello depuis un programme charge du disque (exec) !\n");
    sys_exit();

    /* Jamais atteint : sys_exit() ne revient pas. */
    for (;;) { }
}
