#include "sched.h"
#include "cpu.h"
#include "screen.h"
#include "mem.h"
#include "pit.h"
#include "gdt.h"

#define MAX_TASKS 16
// Taille de la pile allouée pour chaque tâche (noyau ou utilisateur).
#define TASK_STACK_SIZE 4096

static Task tasks[MAX_TASKS];
static int current_task = 0;
static int multitasking_enabled = 0;
static int task_count = 0;

// Sélecteurs de segment ring3 (doivent correspondre à kernel/gdt.c)
#define SEL_USER_CODE 0x1B // index 3, RPL 3
#define SEL_USER_DATA 0x23 // index 4, RPL 3

// Cherche la prochaine tâche à exécuter. Ne renvoie JAMAIS l'ESP d'une tâche
// inactive (ex: qui vient de se terminer et dont la pile a été libérée) :
// - Passe 1 : tâche active et prête (ni endormie, ni réveil pas encore échu)
// - Passe 2 (repli) : n'importe quelle tâche active, même encore endormie
static int find_next_task(void) {
    int next_task = -1;

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

    if (next_task < 0) {
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].active) {
                next_task = i;
                break;
            }
        }
    }

    return next_task;
}

uint32_t schedule(uint32_t last_esp) {
    timer_tick();

    if (!multitasking_enabled) return last_esp;

    tasks[current_task].esp = last_esp;

    int next_task = find_next_task();
    if (next_task < 0) {
        // Ne devrait arriver que si plus aucune tâche n'est active du tout.
        for (;;) __asm__ volatile("cli\n\thlt");
    }

    current_task = next_task;

    // Si la prochaine tâche est interrompue depuis le ring3, le CPU aura
    // besoin d'une pile ring0 valide pour la prochaine transition -> on
    // pointe TSS.esp0 vers le haut de la pile noyau de cette tâche.
    tss_set_kernel_stack(tasks[current_task].stack_base + TASK_STACK_SIZE);

    return tasks[current_task].esp;
}

// Termine proprement la tâche courante (cas "ring0 tombe en fin de fonction") :
// libère sa pile puis boucle en hlt. Sûr uniquement si IF=1 dans ce contexte
// (i.e. appelé directement par la tâche, pas depuis un gestionnaire d'interruption),
// puisqu'il attend d'être préempté par le prochain tick pour ne jamais être réélu.
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

// Variante appelable depuis un gestionnaire d'interruption (ex: syscall
// SYS_EXIT). Comme int 0x80 utilise une porte d'interruption, IF=0 durant
// son exécution : on NE PEUT PAS attendre un tick pour être repris, il faut
// resélectionner explicitement une autre tâche via schedule() et renvoyer
// son ESP, que le gestionnaire chargera avant l'iret (comme timer_handler).
uint32_t task_exit_and_reschedule(uint32_t current_esp) {
    if (tasks[current_task].active) {
        tasks[current_task].active = 0;
        tasks[current_task].sleeping = 0;
        if (task_count > 0) task_count--;
        if (tasks[current_task].stack_base) {
            kfree((void*)tasks[current_task].stack_base);
            tasks[current_task].stack_base = 0;
        }
    }

    if (task_count == 0) {
        kprint("Toutes les taches sont terminees. Arret du systeme.\n");
        for (;;) __asm__ volatile("cli\n\thlt");
    }

    return schedule(current_esp);
}

// Met la tâche courante en sommeil pendant au moins `ms` millisecondes,
// en bloquant sur `hlt` (IF doit être à 1 dans ce contexte : appel direct
// depuis une tâche, jamais depuis un gestionnaire d'interruption).
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

// Variante appelable depuis un gestionnaire d'interruption (ex: syscall
// SYS_SLEEP) : marque la tâche endormie puis rebascule immédiatement vers
// une autre tâche via schedule(), sans jamais attendre en `hlt` avec IF=0.
uint32_t task_sleep_and_reschedule(uint32_t ms, uint32_t current_esp) {
    uint32_t freq = pit_get_frequency();
    uint32_t ticks_to_wait = (ms * freq) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    tasks[current_task].wake_tick = get_ticks() + ticks_to_wait;
    tasks[current_task].sleeping = 1;

    return schedule(current_esp);
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
    if (i == MAX_TASKS) return -1;

    uint32_t stack_address = (uint32_t)kmalloc(TASK_STACK_SIZE);
    if (stack_address == 0) return -1;

    uint32_t *stack = (uint32_t*)(stack_address + TASK_STACK_SIZE);
    tasks[i].stack_base = (uint32_t)stack - TASK_STACK_SIZE;
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

// Crée une tâche destinée à s'exécuter en ring3 (espace utilisateur).
// Contrairement à create_task(), la pile initiale contient une trame iret
// complète à 5 mots (SS, ESP, EFLAGS, CS, EIP) car le passage ring0->ring3
// est un changement de niveau de privilège : le CPU exige que SS:ESP soient
// également empilés pour savoir où basculer la pile utilisateur.
// `user_stack_top` doit pointer vers le sommet d'une pile dédiée, mappée
// avec le bit U/S (cf. kernel/paging.c) et distincte de la pile noyau ci-dessous
// (qui elle ne sert qu'aux interruptions survenant pendant l'exécution ring3).
int create_user_task(void (*entry)(), char* name, uint32_t user_stack_top) {
    if (task_count >= MAX_TASKS) return -1;

    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].active) break;
    }
    if (i == MAX_TASKS) return -1;

    uint32_t stack_address = (uint32_t)kmalloc(TASK_STACK_SIZE);
    if (stack_address == 0) return -1;

    uint32_t *stack = (uint32_t*)(stack_address + TASK_STACK_SIZE);
    tasks[i].stack_base = (uint32_t)stack - TASK_STACK_SIZE;
    tasks[i].sleeping = 0;
    tasks[i].wake_tick = 0;

    *(--stack) = SEL_USER_DATA;      // SS (pile utilisateur)
    *(--stack) = user_stack_top;     // ESP (pile utilisateur)
    *(--stack) = 0x202;              // EFLAGS (IF=1)
    *(--stack) = SEL_USER_CODE;      // CS ring3
    *(--stack) = (uint32_t)entry;    // EIP

    // Simulation du pushal initial
    for (int j = 0; j < 8; j++) *(--stack) = 0;

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
