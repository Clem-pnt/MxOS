#include "syscall.h"
#include "interrupts.h"
#include "screen.h"
#include "sched.h"

// Correspond à l'ordre inverse de "pushal" : premier poussé (EAX) en haut de
// la pile, dernier poussé (EDI) au sommet -> pointé par esp après pushal.
struct registers {
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
};

// Convention : eax = numéro d'appel système, ebx/ecx/edx = arguments.
// Renvoie l'ESP à charger avant l'iret : identique à l'ESP courant dans le
// cas général, mais différent si SYS_EXIT/SYS_SLEEP ont fait basculer vers
// une autre tâche (cf. task_exit_and_reschedule/task_sleep_and_reschedule).
// C'est indispensable car `int 0x80` utilise une porte d'interruption (IF=0
// pendant son exécution) : on ne peut pas attendre un tick d'horloge pour
// être repris, il faut resélectionner explicitement la tâche suivante ici.
static uint32_t syscall_dispatch(struct registers *regs) {
    uint32_t current_esp = (uint32_t)regs;

    switch (regs->eax) {
        case SYS_PRINT:
            kprint((char*)regs->ebx);
            return current_esp;
        case SYS_EXIT:
            return task_exit_and_reschedule(current_esp);
        case SYS_SLEEP:
            return task_sleep_and_reschedule(regs->ebx, current_esp);
        case SYS_SEND:
            // ebx = pid cible, ecx = pointeur message (mémoire de l'appelant), edx = longueur
            regs->eax = (uint32_t)ipc_send((int)regs->ebx, (const void*)regs->ecx, regs->edx);
            return current_esp;
        case SYS_RECV: {
            // ebx = tampon de sortie, ecx = pointeur uint32_t* longueur (optionnel),
            // edx = pointeur int* PID expéditeur (optionnel) ; non bloquant.
            uint32_t len = 0;
            int sender = -1;
            int got = ipc_recv((void*)regs->ebx, &len, &sender);
            if (got) {
                if (regs->ecx) *(uint32_t*)regs->ecx = len;
                if (regs->edx) *(int*)regs->edx = sender;
            }
            regs->eax = (uint32_t)got;
            return current_esp;
        }
        default:
            return current_esp;
    }
}

__attribute__((naked)) static void syscall_handler(void) {
    __asm__ volatile (
        "pushal\n"
        "movl %esp, %eax\n"
        "pushl %eax\n"
        "call syscall_dispatch\n"
        "addl $4, %esp\n"
        "movl %eax, %esp\n"  // Recharge l'ESP (potentiellement celui d'une autre tâche)
        "popal\n"
        "iret\n"
    );
}

void syscall_init(void) {
    // DPL=3 (0xEE) pour être appelable via `int 0x80` depuis un futur espace utilisateur.
    set_idt_gate_flags(0x80, (uint32_t)syscall_handler, 0xEE);
}
