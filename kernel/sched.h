#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

typedef struct {
    uint32_t esp;        // Pointeur de pile (sauvegarde l'état du CPU)
    uint32_t stack_base; 
    int active;
    char* name;
    int sleeping;
    uint32_t wake_tick;
} Task;

/* --- PROTOTYPES --- */
void init_multitasking();
int create_task(void (*entry)(), char* name);
int create_user_task(void (*entry)(), char* name, uint32_t user_stack_top);
uint32_t schedule(uint32_t last_esp);
void timer_handler(); // Le handler naked
void task_sleep(uint32_t ms);
void task_exit_current(void); // Appelable directement (contexte tâche, IF=1)
uint32_t task_exit_and_reschedule(uint32_t current_esp); // Appelable depuis un gestionnaire d'interruption (ex: syscall)
uint32_t task_sleep_and_reschedule(uint32_t ms, uint32_t current_esp); // Idem, pour SYS_SLEEP

#endif