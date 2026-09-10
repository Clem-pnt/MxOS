#include "exec.h"
#include "screen.h"
#include "sched.h"
#include "../drivers/fs.h"
#include <stdint.h>

// Adresse fixe de chargement des programmes utilisateur externes (doit
// correspondre à `. = 0x300000` dans userprogs/user.ld). Se trouve dans les
// 4 Mo identity-mappés par kernel/paging.c, au-dela du tas noyau (1-2 Mo),
// donc sans collision avec kmalloc/kfree.
#define USER_PROG_BASE 0x300000
#define USER_PROG_MAX_SIZE 0x20000 // 128 Ko

// Pile dédiée à la tâche exec'ée. Comme la zone de code ci-dessus, elle est
// partagée par tous les `exec` : une seule tâche chargée par ce mécanisme
// peut donc être active à la fois (cf. exec_active_pid ci-dessous).
static uint8_t user_prog_stack[4096] __attribute__((aligned(16)));

// PID de la dernière tâche lancée par exec_run, ou -1. Sert de garde-fou :
// tant qu'elle est encore active, on refuse un nouvel exec pour ne pas
// écraser la zone de code/pile qu'elle utilise encore.
static int exec_active_pid = -1;

// Symboles générés par `objcopy -I binary` (cf. Makefile) à partir du
// binaire compilé userprogs/hello_user.bin : bornes du blob embarqué dans
// l'image du noyau.
extern uint8_t _binary_hello_user_bin_start[];
extern uint8_t _binary_hello_user_bin_end[];

void exec_seed_programs(void) {
    if (fs_file_exists("hello")) return;

    uint32_t size = (uint32_t)(_binary_hello_user_bin_end - _binary_hello_user_bin_start);
    if (size == 0) return;

    fs_write_file("hello", _binary_hello_user_bin_start, size);
}

int exec_run(char *name) {
    if (exec_active_pid >= 0 && task_is_active(exec_active_pid)) {
        kprint("Un programme charge par exec tourne deja (attendez sa fin).\n");
        return -1;
    }

    // Efface entierement la zone de chargement : les eventuelles variables
    // non-initialisees (.bss) du programme ne sont pas presentes dans le
    // fichier sur disque (objcopy -O binary ne serialise pas le .bss), donc
    // doivent deja valoir zero en memoire avant l'execution.
    for (uint32_t i = 0; i < USER_PROG_MAX_SIZE; i++) {
        ((uint8_t*)USER_PROG_BASE)[i] = 0;
    }

    uint32_t size = 0;
    if (!fs_load_file(name, (uint8_t*)USER_PROG_BASE, USER_PROG_MAX_SIZE, &size)) {
        return -1;
    }

    uint32_t stack_top = (uint32_t)user_prog_stack + sizeof(user_prog_stack);
    int pid = create_user_task_ex((void (*)())USER_PROG_BASE, name,
                                   stack_top, sizeof(user_prog_stack),
                                   USER_PROG_BASE, USER_PROG_MAX_SIZE);
    if (pid < 0) {
        kprint("Impossible de creer la tache (limite atteinte).\n");
        return -1;
    }

    exec_active_pid = pid;
    return pid;
}
