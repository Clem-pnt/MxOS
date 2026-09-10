#include "sched.h"
#include "cpu.h"
#include "screen.h"
#include "mem.h"
#include "pit.h"

#define MAX_TASKS 16

static Task tasks[MAX_TASKS];
static int current_task = 0;
static int multitasking_enabled = 0;
static int task_count = 0;

// Termine proprement la tâche courante : libère sa pile puis boucle indéfiniment.
// Appelable directement (valeur de retour des tâches) ou via le syscall SYS_EXIT.
void task_exit_current(void) {
    if (tasks[current_task].active) {
        tasks[current_task].active = 0;
        tasks[current_task].sleeping = 0;
        if (task_count > 0) task_count--;
        if (tasks[current_task].stack_base) {
            kfree((void*)tasks[current_task].stack_base);
            tasks[current_task].stack_base = 0;
        }
    }
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void task_exit() {
    task_exit_current();
}

uint32_t schedule(uint32_t last_esp) {
    timer_tick();

    if (!multitasking_enabled) return last_esp;

    tasks[current_task].esp = last_esp;

    // Cherche la prochaine tâche active et prête (non endormie, ou réveil échu)
    int next_task = current_task;
    for (int i = 1; i <= MAX_TASKS; i++) {
        int candidate = (current_task + i) % MAX_TASKS;
        if (!tasks[candidate].active) continue;

        if (tasks[candidate].sleeping) {
            if (get_ticks() >= tasks[candidate].wake_tick) {
                tasks[candidate].sleeping = 0;
            } else {
                continue;
            }
        }

        next_task = candidate;
        break;
    }

    current_task = next_task;
    return tasks[current_task].esp;
}

// Met la tâche courante en sommeil pendant au moins `ms` millisecondes.
void task_sleep(uint32_t ms) {
    uint32_t freq = pit_get_frequency();
    uint32_t ticks_to_wait = (ms * freq) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    tasks[current_task].wake_tick = get_ticks() + ticks_to_wait;
    tasks[current_task].sleeping = 1;

    while (tasks[current_task].sleeping) {
        __asm__ volatile("hlt");
    }
}

// Le handler de l'horloge en assembleur pur (naked)
__attribute__((naked)) void timer_handler() {
    __asm__ volatile (
        "pushal \n"          // Sauvegarde EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
        "movl %esp, %eax \n"
        "pushl %eax \n"      // On passe l'ESP actuel à schedule()
        "call schedule \n"
        "movl %eax, %esp \n" // On récupère le nouvel ESP renvoyé par schedule()
        "movb $0x20, %al \n"
        "outb %al, $0x20 \n" // Envoi de l'EOI au PIC
        "popal \n"           // Restauration des registres de la nouvelle tâche
        "iret \n"            // Retour d'interruption
    );
}

int create_task(void (*entry)(), char* name) {
    if (task_count >= MAX_TASKS) return -1;

    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].active) break;
    }

    uint32_t stack_address = (uint32_t)kmalloc(4096);
    if (stack_address == 0) return -1;

    uint32_t *stack = (uint32_t*)(stack_address + 4096);
    tasks[i].stack_base = (uint32_t)stack - 4096;
    tasks[i].sleeping = 0;
    tasks[i].wake_tick = 0;
    
    *(--stack) = (uint32_t)task_exit;
    *(--stack) = 0x202; // EFLAGS
    *(--stack) = 0x08;  // CS
    *(--stack) = (uint32_t)entry;
    
    // Simulation du pushal initial
    for(int j = 0; j < 8; j++) *(--stack) = 0;

    tasks[i].esp = (uint32_t)stack;
    tasks[i].active = 1;
    tasks[i].name = name;
    task_count++;

    return i;
}

void init_multitasking() {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].active = 0;
        tasks[i].sleeping = 0;
        tasks[i].wake_tick = 0;
    }

    // Tâche 0 (Shell / Main) - l'état sera sauvegardé lors de la première interruption
    tasks[0].name = "Shell";
    tasks[0].active = 1;
    task_count = 1;
    current_task = 0;

    multitasking_enabled = 1;
}
