#include "exec.h"
#include "screen.h"
#include "sched.h"
#include "elf.h"
#include "../drivers/fs.h"
#include <stdint.h>

// Adresses fixes de chargement des programmes utilisateur externes. Chaque
// "slot" réserve une fenêtre de USER_PROG_MAX_SIZE octets, espacées de
// SLOT_STRIDE pour éviter tout chevauchement ; tout tient dans les 4 Mo
// identity-mappés par kernel/paging.c.
//
// userprogs/user.ld lie les programmes utilisateur pour une exécution à
// LINK_BASE = SLOT_BASE(0) (0x300000). Grâce au vrai chargeur ELF
// (kernel/elf.c, qui applique les relocations R_386_32 conservées par
// `ld --emit-relocs`), le MÊME fichier peut être chargé correctement à
// n'importe quel autre slot (1..3) : ce n'est plus une coïncidence liée à
// la simplicité du programme de démo, mais une vraie relogeabilité.
#define USER_PROG_MAX_SIZE 0x20000 // 128 Ko par slot
#define SLOT_STRIDE         0x30000 // 192 Ko : marge au-dela de USER_PROG_MAX_SIZE
#define SLOT_BASE(i)        (0x300000 + (uint32_t)(i) * SLOT_STRIDE)
#define LINK_BASE            SLOT_BASE(0)
#define MAX_EXEC_SLOTS 4 // Doit rester <= MAX_ADDR_SPACES (kernel/paging.c)

// Pile dédiée à chaque slot d'exécution (une par emplacement, pour permettre
// plusieurs programmes chargés simultanément sans qu'ils partagent leur pile).
// Les premiers ARGV_AREA_SIZE octets (adresses basses) sont réservés au
// tableau argv/argc construit par exec_run() ; le reste sert de pile réelle
// (qui croît vers le bas depuis le sommet, donc s'éloigne de cette zone).
#define ARGV_AREA_SIZE 512
static uint8_t exec_stacks[MAX_EXEC_SLOTS][4096] __attribute__((aligned(16)));

// Tampon de réception temporaire pour le fichier ELF brut lu depuis le
// disque, avant que kernel/elf.c ne recopie ses segments PT_LOAD à leur
// emplacement définitif (qui peut différer de LINK_BASE, cf. ci-dessus).
static uint8_t elf_staging[USER_PROG_MAX_SIZE];

// PID de la tâche occupant chaque slot (-1 si jamais utilisé). Un slot est
// considéré libre s'il vaut -1 OU si la tâche qu'il référence s'est terminée
// entretemps (task_is_active() == 0) : l'espace est alors réutilisé.
static int slot_pid[MAX_EXEC_SLOTS] = { -1, -1, -1, -1 };

// Symboles générés par `objcopy -I binary` (cf. Makefile) à partir du
// fichier ELF compilé/lié userprogs/hello_user.elf : bornes du blob
// embarqué dans l'image du noyau (le fichier .elf complet, PAS un binaire
// plat : conserve les en-têtes de programme et les relocations).
extern uint8_t _binary_hello_user_elf_start[];
extern uint8_t _binary_hello_user_elf_end[];

void exec_seed_programs(void) {
    if (fs_file_exists("hello")) return;

    uint32_t size = (uint32_t)(_binary_hello_user_elf_end - _binary_hello_user_elf_start);
    if (size == 0) return;

    fs_write_file("hello", _binary_hello_user_elf_start, size);
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

static int m_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

// Construit dans la zone réservée [stack_base, stack_base+ARGV_AREA_SIZE)
// un tableau de `argc` pointeurs (+ un NULL final) suivi des chaînes elles-
// mêmes, puis renvoie l'adresse du tableau de pointeurs (à passer en ECX à
// la tâche). Cette zone appartient à la fenêtre de pile de la tâche : elle
// est donc accessible en ring3 depuis SON espace d'adressage, exactement
// comme le reste de sa pile (cf. kernel/paging.c).
static uint32_t build_argv(uint32_t stack_base, int argc, char **argv) {
    uint32_t *ptr_table = (uint32_t*)stack_base;
    uint8_t *str_area = (uint8_t*)(stack_base + (uint32_t)(argc + 1) * sizeof(uint32_t));
    uint32_t used = (uint32_t)(argc + 1) * sizeof(uint32_t);

    for (int i = 0; i < argc; i++) {
        int len = m_strlen(argv[i]) + 1; // inclut le zero terminal
        if (used + (uint32_t)len > ARGV_AREA_SIZE) {
            // Argument(s) trop long(s) pour la petite zone reservee : on
            // tronque silencieusement la liste ici (cas limite educatif).
            ptr_table[i] = 0;
            for (int j = i; j < argc; j++) ptr_table[j] = 0;
            break;
        }
        for (int k = 0; k < len; k++) str_area[k] = (uint8_t)argv[i][k];
        ptr_table[i] = stack_base + used;
        str_area += len;
        used += (uint32_t)len;
    }
    ptr_table[argc] = 0; // sentinelle NULL, par convention façon argv[argc]

    return stack_base;
}

int exec_run(char *name, int argc, char **argv) {
    int slot = find_free_slot();
    if (slot < 0) {
        kprint("Trop de programmes charges simultanement (limite atteinte).\n");
        return -1;
    }

    uint32_t base = SLOT_BASE(slot);

    // Efface entierement la zone de chargement : les eventuelles variables
    // non-initialisees (.bss) du programme seront remises a zero par
    // elf_load() lui-meme (memsz > filesz), mais on efface aussi le reste
    // de la fenetre par prudence (ex: relocations qui deborderaient d'un
    // segment par erreur de format).
    for (uint32_t i = 0; i < USER_PROG_MAX_SIZE; i++) {
        ((uint8_t*)base)[i] = 0;
    }

    uint32_t elf_size = 0;
    if (!fs_load_file(name, elf_staging, sizeof(elf_staging), &elf_size)) {
        return -1;
    }

    uint32_t entry = 0;
    if (!elf_load(elf_staging, elf_size, base, USER_PROG_MAX_SIZE, LINK_BASE, &entry)) {
        return -1;
    }

    uint32_t stack_top = (uint32_t)exec_stacks[slot] + sizeof(exec_stacks[slot]);
    uint32_t argv_base = (uint32_t)exec_stacks[slot]; // bas de la pile reservee (cf. ARGV_AREA_SIZE)
    uint32_t argv_ptr = build_argv(argv_base, argc, argv);

    int pid = create_user_task_argv((void (*)())entry, name,
                                     stack_top, sizeof(exec_stacks[slot]),
                                     base, USER_PROG_MAX_SIZE,
                                     (uint32_t)argc, argv_ptr);
    if (pid < 0) {
        kprint("Impossible de creer la tache (limite atteinte).\n");
        return -1;
    }

    slot_pid[slot] = pid;
    return pid;
}
