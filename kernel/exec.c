#include "exec.h"
#include "screen.h"
#include "sched.h"
#include "../drivers/fs.h"
#include <stdint.h>

// Adresses fixes de chargement des programmes utilisateur externes. Chaque
// "slot" réserve une fenêtre de USER_PROG_MAX_SIZE octets, espacées de
// SLOT_STRIDE pour éviter tout chevauchement ; tout tient dans les 4 Mo
// identity-mappés par kernel/paging.c.
// ATTENTION : userprogs/user.ld lie le binaire pour une exécution à
// l'adresse SLOT_BASE(0) (0x300000). Charger ce même binaire à une autre
// adresse (slots 1..3) ne fonctionne que parce que hello_user.c est un
// programme trivial, sans adresse absolue vers ses propres données globales
// (uniquement des appels relatifs). Un programme plus complexe nécessiterait
// soit d'être toujours chargé au slot 0, soit un vrai chargeur avec
// relocation. Limitation documentée dans le README.
#define USER_PROG_MAX_SIZE 0x20000 // 128 Ko par slot
#define SLOT_STRIDE         0x30000 // 192 Ko : marge au-dela de USER_PROG_MAX_SIZE
#define SLOT_BASE(i)        (0x300000 + (uint32_t)(i) * SLOT_STRIDE)
#define MAX_EXEC_SLOTS 4 // Doit rester <= MAX_ADDR_SPACES (kernel/paging.c)

// Pile dédiée à chaque slot d'exécution (une par emplacement, pour permettre
// plusieurs programmes chargés simultanément sans qu'ils partagent leur pile).
static uint8_t exec_stacks[MAX_EXEC_SLOTS][4096] __attribute__((aligned(16)));

// PID de la tâche occupant chaque slot (-1 si jamais utilisé). Un slot est
// considéré libre s'il vaut -1 OU si la tâche qu'il référence s'est terminée
// entretemps (task_is_active() == 0) : l'espace est alors réutilisé.
static int slot_pid[MAX_EXEC_SLOTS] = { -1, -1, -1, -1 };

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

// Trouve un slot libre (jamais utilisé, ou dont la tâche précédente est
// terminée). Renvoie l'index, ou -1 si les MAX_EXEC_SLOTS emplacements sont
// tous occupés par une tâche encore active.
static int find_free_slot(void) {
    for (int i = 0; i < MAX_EXEC_SLOTS; i++) {
        if (slot_pid[i] < 0 || !task_is_active(slot_pid[i])) {
            return i;
        }
    }
    return -1;
}

int exec_run(char *name) {
    int slot = find_free_slot();
    if (slot < 0) {
        kprint("Trop de programmes charges simultanement (limite atteinte).\n");
        return -1;
    }

    uint32_t base = SLOT_BASE(slot);

    // Efface entierement la zone de chargement : les eventuelles variables
    // non-initialisees (.bss) du programme ne sont pas presentes dans le
    // fichier sur disque (objcopy -O binary ne serialise pas le .bss), donc
    // doivent deja valoir zero en memoire avant l'execution.
    for (uint32_t i = 0; i < USER_PROG_MAX_SIZE; i++) {
        ((uint8_t*)base)[i] = 0;
    }

    uint32_t size = 0;
    if (!fs_load_file(name, (uint8_t*)base, USER_PROG_MAX_SIZE, &size)) {
        return -1;
    }

    uint32_t stack_top = (uint32_t)exec_stacks[slot] + sizeof(exec_stacks[slot]);
    int pid = create_user_task_ex((void (*)())base, name,
                                   stack_top, sizeof(exec_stacks[slot]),
                                   base, USER_PROG_MAX_SIZE);
    if (pid < 0) {
        kprint("Impossible de creer la tache (limite atteinte).\n");
        return -1;
    }

    slot_pid[slot] = pid;
    return pid;
}
