#include "exceptions.h"
#include "interrupts.h"
#include "screen.h"

// --- Stubs bas niveau : chaque exception pousse (si nécessaire) un faux
// code d'erreur puis son numéro de vecteur, avant de sauter vers le
// gestionnaire commun. Les vecteurs 8,10,11,12,13,14,17 reçoivent déjà un
// vrai code d'erreur de la part du CPU (EXC_ERR), les autres non (EXC_NOERR).

#define EXC_NOERR(N) \
__attribute__((naked)) static void isr##N(void) { \
    __asm__ volatile ( \
        "pushl $0\n" \
        "pushl $" #N "\n" \
        "jmp isr_common_stub\n" \
    ); \
}

#define EXC_ERR(N) \
__attribute__((naked)) static void isr##N(void) { \
    __asm__ volatile ( \
        "pushl $" #N "\n" \
        "jmp isr_common_stub\n" \
    ); \
}

static void isr_common_stub(void);

EXC_NOERR(0)
EXC_NOERR(1)
EXC_NOERR(2)
EXC_NOERR(3)
EXC_NOERR(4)
EXC_NOERR(5)
EXC_NOERR(6)
EXC_NOERR(7)
EXC_ERR(8)
EXC_NOERR(9)
EXC_ERR(10)
EXC_ERR(11)
EXC_ERR(12)
EXC_ERR(13)
EXC_ERR(14)
EXC_NOERR(15)
EXC_NOERR(16)
EXC_ERR(17)
EXC_NOERR(18)
EXC_NOERR(19)
EXC_NOERR(20)
EXC_NOERR(21)
EXC_NOERR(22)
EXC_NOERR(23)
EXC_NOERR(24)
EXC_NOERR(25)
EXC_NOERR(26)
EXC_NOERR(27)
EXC_NOERR(28)
EXC_NOERR(29)
EXC_NOERR(30)
EXC_NOERR(31)

// Reçoit un pointeur vers le bloc pushal ; au-dessus se trouvent le numéro
// de vecteur, le code d'erreur puis le cadre d'interruption (EIP, CS, EFLAGS).
static void exception_report(uint32_t *regs) {
    uint32_t vector = regs[8];
    uint32_t error_code = regs[9];
    uint32_t eip = regs[10];

    kprint("\n*** EXCEPTION ");
    kprint_dec(vector);
    kprint(" (code ");
    kprint_hex(error_code);
    kprint(") at EIP=");
    kprint_hex(eip);
    kprint(" - System Halted ***\n");

    for (;;) {
        __asm__ volatile("cli\n\thlt");
    }
}

__attribute__((naked)) static void isr_common_stub(void) {
    __asm__ volatile (
        "pushal\n"
        "movl %esp, %eax\n"
        "pushl %eax\n"
        "call exception_report\n"
        "addl $4, %esp\n"
        "popal\n"
        "addl $8, %esp\n" // retire vector_num + error_code
        "iret\n"
    );
}

void install_exception_handlers(void) {
    set_idt_gate(0, (uint32_t)isr0);
    set_idt_gate(1, (uint32_t)isr1);
    set_idt_gate(2, (uint32_t)isr2);
    set_idt_gate(3, (uint32_t)isr3);
    set_idt_gate(4, (uint32_t)isr4);
    set_idt_gate(5, (uint32_t)isr5);
    set_idt_gate(6, (uint32_t)isr6);
    set_idt_gate(7, (uint32_t)isr7);
    set_idt_gate(8, (uint32_t)isr8);
    set_idt_gate(9, (uint32_t)isr9);
    set_idt_gate(10, (uint32_t)isr10);
    set_idt_gate(11, (uint32_t)isr11);
    set_idt_gate(12, (uint32_t)isr12);
    set_idt_gate(13, (uint32_t)isr13);
    set_idt_gate(14, (uint32_t)isr14);
    set_idt_gate(15, (uint32_t)isr15);
    set_idt_gate(16, (uint32_t)isr16);
    set_idt_gate(17, (uint32_t)isr17);
    set_idt_gate(18, (uint32_t)isr18);
    set_idt_gate(19, (uint32_t)isr19);
    set_idt_gate(20, (uint32_t)isr20);
    set_idt_gate(21, (uint32_t)isr21);
    set_idt_gate(22, (uint32_t)isr22);
    set_idt_gate(23, (uint32_t)isr23);
    set_idt_gate(24, (uint32_t)isr24);
    set_idt_gate(25, (uint32_t)isr25);
    set_idt_gate(26, (uint32_t)isr26);
    set_idt_gate(27, (uint32_t)isr27);
    set_idt_gate(28, (uint32_t)isr28);
    set_idt_gate(29, (uint32_t)isr29);
    set_idt_gate(30, (uint32_t)isr30);
    set_idt_gate(31, (uint32_t)isr31);
}
