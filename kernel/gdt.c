#include "gdt.h"

// --- Structures GDT/TSS (format x86 standard) ---

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed));

#define GDT_ENTRIES 6
// Sélecteurs (index * 8), fixés par l'ordre de création ci-dessous.
#define SEL_KERNEL_CODE 0x08
#define SEL_KERNEL_DATA 0x10
#define SEL_USER_CODE   0x18
#define SEL_USER_DATA   0x20
#define SEL_TSS         0x28

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr gp;
static struct tss_entry tss;

// Pile noyau dédiée utilisée par le CPU (via TSS.esp0) lors d'une transition
// ring3 -> ring0 (interruption ou syscall depuis l'espace utilisateur).
static uint8_t tss_kernel_stack[4096] __attribute__((aligned(16)));

extern void gdt_flush(uint32_t gdt_ptr_addr);
extern void tss_flush(void);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access = access;
}

static void write_tss(int num) {
    uint32_t base = (uint32_t)&tss;
    uint32_t limit = base + sizeof(tss);
    gdt_set_gate(num, base, limit, 0x89, 0x00); // Present, ring0, TSS 32 bits disponible

    for (uint32_t i = 0; i < sizeof(tss); i++) {
        ((uint8_t*)&tss)[i] = 0;
    }
    tss.ss0 = SEL_KERNEL_DATA;
    tss.esp0 = (uint32_t)tss_kernel_stack + sizeof(tss_kernel_stack);
    // Champs de segment par défaut si jamais utilisés via une tâche matérielle (non utilisé ici)
    tss.cs = SEL_KERNEL_CODE | 0x3;
    tss.ss = tss.ds = tss.es = tss.fs = tss.gs = SEL_KERNEL_DATA | 0x3;
    tss.iomap_base = sizeof(tss);
}

void gdt_init(void) {
    gp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gp.base = (uint32_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                  // Descripteur nul obligatoire
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);    // Code noyau   (ring0) -> 0x08
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);    // Donnees noyau(ring0) -> 0x10
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);    // Code utilisateur (ring3) -> 0x18
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);    // Donnees utilisateur (ring3) -> 0x20
    write_tss(5);                                   // TSS -> 0x28

    gdt_flush((uint32_t)&gp);
    tss_flush();
}

// Appelée par l'ordonnanceur pour que TSS.esp0 pointe vers la pile noyau
// correcte de la tâche en cours, afin qu'une interruption survenant pendant
// l'exécution en ring3 bascule vers une pile ring0 valide.
void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

// Recharge le GDTR puis tous les registres de segment avec les nouveaux
// sélecteurs (0x10 = données noyau), et effectue un far jump pour recharger CS.
__attribute__((naked)) void gdt_flush(uint32_t gdt_ptr_addr) {
    __asm__ volatile (
        "movl 4(%esp), %eax\n"
        "lgdt (%eax)\n"
        "movw $0x10, %ax\n"
        "movw %ax, %ds\n"
        "movw %ax, %es\n"
        "movw %ax, %fs\n"
        "movw %ax, %gs\n"
        "movw %ax, %ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        "ret\n"
    );
}

// Charge le sélecteur du TSS (index 5 de la GDT, RPL=0) dans le registre de tâche.
__attribute__((naked)) void tss_flush(void) {
    __asm__ volatile (
        "movw $0x28, %ax\n"
        "ltr %ax\n"
        "ret\n"
    );
}
