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
uint32_t schedule(uint32_t last_esp);
void timer_handler(); // Le handler naked
void task_sleep(uint32_t ms);
void task_exit_current(void); // Appelable via syscall SYS_EXIT

#endif