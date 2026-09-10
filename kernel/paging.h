#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

void paging_init();

// Adresse physique (= linéaire, identity map) du répertoire de pages noyau,
// utilisé par toutes les tâches qui n'ont pas d'espace d'adressage dédié
// (tâches ring0 pures comme le Shell ou la tâche Clock).
uint32_t paging_kernel_directory_phys(void);

// Crée un espace d'adressage isolé pour une tâche ring3 : renvoie un index
// (>=0) à passer à paging_task_directory_phys()/paging_free_task_directory(),
// ou -1 si la limite MAX_ADDR_SPACES est atteinte.
int paging_create_task_directory(uint32_t user_stack_base, uint32_t user_stack_size);
uint32_t paging_task_directory_phys(int idx);
void paging_free_task_directory(int idx);

// Recharge CR3 avec l'adresse physique de répertoire de pages donnée.
void paging_switch_directory(uint32_t phys_addr);

#endif
