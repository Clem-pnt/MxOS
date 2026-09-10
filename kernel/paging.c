#include "paging.h"
#include "screen.h"

// Bornes fournies par linker.ld : tout ce qui va de __text_start à __ro_end
// (code + rodata + data) doit rester exécutable/lisible depuis le ring3,
// car les tâches utilisateur actuelles sont des fonctions C compilées
// directement dans l'image noyau (il n'y a pas encore de vrai chargeur qui
// placerait un binaire utilisateur dans une zone séparée). Tout le reste
// (BSS : structures noyau, tas, piles...) est marqué "supervisor only" par
// défaut : une tâche ring3 ne peut donc plus lire/écrire arbitrairement la
// mémoire noyau (table des tâches, cache du système de fichiers, etc.),
// sauf sur la fenêtre explicite de SA PROPRE pile utilisateur.
extern uint8_t __text_start;
extern uint8_t __ro_end;

uint32_t page_directory[1024] __attribute__((aligned(4096)));
uint32_t first_page_table[1024] __attribute__((aligned(4096)));

// Nombre maximal d'espaces d'adressage isolés (un par tâche ring3 vivante) :
// MAX_EXEC_SLOTS (kernel/exec.c) + la tâche UserDemo compilée en dur + marge.
#define MAX_ADDR_SPACES 6

static uint32_t task_directories[MAX_ADDR_SPACES][1024] __attribute__((aligned(4096)));
static uint32_t task_tables[MAX_ADDR_SPACES][1024] __attribute__((aligned(4096)));
static int task_dir_used[MAX_ADDR_SPACES];

// Remplit une table de pages en identity-map sur 4 Mo. `user_start`/`user_end`
// délimitent la fenêtre de pile utilisateur, et `code_start`/`code_end` une
// fenêtre supplémentaire optionnelle (programme chargé depuis le disque par
// `exec`, cf. kernel/exec.c) ; toutes deux marquées U/S=1. En dehors de ces
// fenêtres et de la zone code/rodata/data partagée du noyau, tout reste
// Supervisor-only.
static void fill_identity_table(uint32_t *table, uint32_t user_start, uint32_t user_end,
                                 uint32_t code_start, uint32_t code_end) {
    uint32_t ro_start = (uint32_t)&__text_start;
    uint32_t ro_stop  = (uint32_t)&__ro_end;

    for (int i = 0; i < 1024; i++) {
        uint32_t phys = (uint32_t)(i * 4096);
        uint32_t flags = 3; // Present, Read/Write, Supervisor par defaut

        if (phys + 4096 > ro_start && phys < ro_stop) {
            flags = 7; // Present, R/W, User (code/rodata/data partages)
        }
        if (user_start != user_end && phys + 4096 > user_start && phys < user_end) {
            flags = 7; // Present, R/W, User (pile utilisateur de cette tache)
        }
        if (code_start != code_end && phys + 4096 > code_start && phys < code_end) {
            flags = 7; // Present, R/W, User (code utilisateur charge depuis le disque)
        }

        table[i] = phys | flags;
    }
}

void paging_init() {
    // Initialise le répertoire de pages avec des entrées non présentes
    for(int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002; // Read/Write, Not Present
    }

    fill_identity_table(first_page_table, 0, 0, 0, 0);

    // Bit U/S (0x4) mis sur la PDE : indispensable pour que les PTE
    // individuelles marquées "User" soient réellement accessibles en ring3
    // (le CPU applique un ET logique entre les permissions du répertoire et
    // celles de la table). La restriction fine se fait donc au niveau PTE.
    page_directory[0] = ((uint32_t)first_page_table) | 7;

    // Active la pagination
    __asm__ volatile (
        "mov %0, %%cr3 \n"
        "mov %%cr0, %%eax \n"
        "or $0x80000000, %%eax \n"
        "mov %%eax, %%cr0 \n"
        : : "r"(page_directory) : "eax"
    );

    kprint("Paging enabled (per-task isolation ready).\n");
}

uint32_t paging_kernel_directory_phys(void) {
    return (uint32_t)page_directory;
}

// Crée un espace d'adressage isolé pour une tâche ring3 : clone de la carte
// noyau, mais où seule la fenêtre [user_stack_base, user_stack_base+size)
// est accessible en User en plus de la zone code/rodata/data partagée.
// Toute autre mémoire (BSS noyau, tas, piles des AUTRES tâches) est
// Supervisor-only dans CET espace d'adressage : une tâche ring3 qui tente d'y
// accéder déclenche un #PF (actuellement fatal, cf. kernel/exceptions.c).
int paging_create_task_directory(uint32_t user_stack_base, uint32_t user_stack_size) {
    return paging_create_task_directory_ex(0, 0, user_stack_base, user_stack_size);
}

// Variante acceptant en plus une fenêtre de code utilisateur (programme
// chargé depuis le disque par `exec`, cf. kernel/exec.c) distincte de la
// zone code/rodata/data partagée du noyau. Passer code_size=0 équivaut à
// paging_create_task_directory (tâche dont le code est déjà dans la zone
// partagée, ex: UserDemo compilée dans l'image du noyau).
int paging_create_task_directory_ex(uint32_t code_base, uint32_t code_size,
                                     uint32_t user_stack_base, uint32_t user_stack_size) {
    for (int i = 0; i < MAX_ADDR_SPACES; i++) {
        if (task_dir_used[i]) continue;

        task_dir_used[i] = 1;
        for (int j = 0; j < 1024; j++) task_directories[i][j] = 0x00000002;
        fill_identity_table(task_tables[i],
                             user_stack_base, user_stack_base + user_stack_size,
                             code_base, code_base + code_size);
        task_directories[i][0] = ((uint32_t)task_tables[i]) | 7;
        return i;
    }
    return -1;
}

uint32_t paging_task_directory_phys(int idx) {
    if (idx < 0 || idx >= MAX_ADDR_SPACES) return paging_kernel_directory_phys();
    return (uint32_t)task_directories[idx];
}

void paging_free_task_directory(int idx) {
    if (idx >= 0 && idx < MAX_ADDR_SPACES) task_dir_used[idx] = 0;
}

void paging_switch_directory(uint32_t phys_addr) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys_addr) : "memory");
}
