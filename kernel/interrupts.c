#include "interrupts.h"
#include "cpu.h"

#define IDT_ENTRIES 256

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

void set_idt_gate(int n, uint32_t handler) {
    idt[n].base_low = handler & 0xFFFF;
    idt[n].sel = 0x08;
    idt[n].always0 = 0;
    idt[n].flags = 0x8E; // Present, ring 0, 32-bit interrupt gate
    idt[n].base_high = (handler >> 16) & 0xFFFF;
}

// Variante permettant de préciser les drapeaux (ex: DPL=3 pour un syscall
// accessible depuis l'espace utilisateur via `int 0x80`).
void set_idt_gate_flags(int n, uint32_t handler, uint8_t flags) {
    idt[n].base_low = handler & 0xFFFF;
    idt[n].sel = 0x08;
    idt[n].always0 = 0;
    idt[n].flags = flags;
    idt[n].base_high = (handler >> 16) & 0xFFFF;
}

void load_idt() {
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;
    __asm__ __volatile__("lidt (%0)" : : "r"(&idtp));
}

void remap_pic() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}
