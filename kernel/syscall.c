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
static void syscall_dispatch(struct registers *regs) {
    switch (regs->eax) {
        case SYS_PRINT:
            kprint((char*)regs->ebx);
            break;
        case SYS_EXIT:
            task_exit_current();
            break;
        case SYS_SLEEP:
            task_sleep(regs->ebx);
            break;
        default:
            break;
    }
}

__attribute__((naked)) static void syscall_handler(void) {
    __asm__ volatile (
        "pushal\n"
        "movl %esp, %eax\n"
        "pushl %eax\n"
        "call syscall_dispatch\n"
        "addl $4, %esp\n"
        "popal\n"
        "iret\n"
    );
}

void syscall_init(void) {
    // DPL=3 (0xEE) pour être appelable via `int 0x80` depuis un futur espace utilisateur.
    set_idt_gate_flags(0x80, (uint32_t)syscall_handler, 0xEE);
}
