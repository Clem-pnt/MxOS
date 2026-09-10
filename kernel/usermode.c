#include "usermode.h"
#include "syscall.h"
#include <stdint.h>

// Pile dédiée à la tâche utilisateur de démonstration (distincte de sa pile
// noyau, qui elle est allouée par create_user_task et utilisée uniquement
// pour les transitions ring3 -> ring0 lors des interruptions/syscalls).
static uint8_t user_demo_stack[4096] __attribute__((aligned(16)));

unsigned int user_demo_stack_top(void) {
    return (unsigned int)user_demo_stack + sizeof(user_demo_stack);
}

unsigned int user_demo_stack_size(void) {
    return sizeof(user_demo_stack);
}

// Petits enrobages appelant directement `int 0x80` : ce sont ces deux seules
// instructions (mov + int) qui font toute l'interaction avec le noyau depuis
// le ring3, exactement comme le ferait un vrai programme utilisateur.
static void sys_print(const char *msg) {
    __asm__ volatile("int $0x80" : : "a"(SYS_PRINT), "b"(msg) : "memory");
}

static void sys_exit(void) {
    __asm__ volatile("int $0x80" : : "a"(SYS_EXIT));
}

// Tâche de démonstration : s'exécute entièrement en ring3 (CPL=3), ne peut
// exécuter aucune instruction privilégiée (hlt, cli, in/out...), et ne
// communique avec le noyau qu'au travers de l'interface syscall INT 0x80.
void user_task_demo(void) {
    sys_print("\n[Ring3] Hello depuis l'espace utilisateur (via int 0x80) !\n");
    sys_exit();

    // Ne devrait jamais être atteint : sys_exit() ne revient pas ici.
    for (;;) { }
}
