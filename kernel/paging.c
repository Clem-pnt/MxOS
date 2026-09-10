#include "paging.h"
#include "screen.h"

uint32_t page_directory[1024] __attribute__((aligned(4096)));
uint32_t first_page_table[1024] __attribute__((aligned(4096)));

void paging_init() {
    // Initialise le répertoire de pages avec des entrées non présentes
    for(int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002; // Read/Write, Not Present
    }

    // Identité map les 4 premiers Mo (0x00000000 - 0x003FFFFF)
    // Bit U/S (0x4) inclus : nécessaire pour que du code exécuté en ring3
    // (espace utilisateur) puisse lire/écrire/exécuter cette région.
    // NOTE: il n'y a pas encore d'isolation mémoire par tâche (pas de
    // pagination par processus) — c'est une simplification pédagogique.
    for(int i = 0; i < 1024; i++) {
        // i * 4096 est l'adresse physique
        // 0x7 : Present, Read/Write, User
        first_page_table[i] = (i * 4096) | 7;
    }

    // Met la première table dans le répertoire (0x7 : Present, R/W, User)
    page_directory[0] = ((uint32_t)first_page_table) | 7;

    // Active la pagination
    __asm__ volatile (
        "mov %0, %%cr3 \n"
        "mov %%cr0, %%eax \n"
        "or $0x80000000, %%eax \n"
        "mov %%eax, %%cr0 \n"
        : : "r"(page_directory) : "eax"
    );

    kprint("Paging enabled (Identity mapping 4MB).\n");
}
