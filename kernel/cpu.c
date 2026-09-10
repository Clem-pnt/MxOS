#include "cpu.h"

/* --- COMMUNICATIONS PORTS --- */

void outb(unsigned short port, unsigned char data) {
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

/* --- REDEMARRAGE via le controleur clavier PS/2 (methode "pulse reset") --- */
void reboot(void) {
    uint8_t tmp;
    __asm__ volatile("cli");

    // Attend que le buffer d'entree du controleur soit vide
    do {
        tmp = inb(0x64);
        if (tmp & 0x01) {
            inb(0x60); // Vide le buffer de sortie si besoin
        }
    } while (tmp & 0x02);

    outb(0x64, 0xFE); // Impulsion de reset

    // Si le reset n'a pas fonctionne, on boucle indefiniment
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* --- CONFIGURATION IDT ---
   Maintenant séparée dans kernel/interrupts.c */