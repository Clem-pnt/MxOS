#ifndef CPU_H
#define CPU_H

#include <stdint.h>

/* --- PORTS I/O --- */
void outb(uint16_t port, uint8_t data);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port); // Lecture 16 bits (utile pour l'ATA)

/* --- CONTROLE MATERIEL --- */
void reboot(void);

/* --- GESTION DES INTERRUPTIONS (IDT) --- */
// Les prototypes spécifiques sont dans kernel/interrupts.h

#endif