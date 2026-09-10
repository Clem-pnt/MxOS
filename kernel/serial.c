#include "serial.h"
#include "cpu.h"
#include <stdint.h>

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); // Désactive les interruptions
    outb(COM1 + 3, 0x80); // Active DLAB pour régler le diviseur de bauds
    outb(COM1 + 0, 0x03); // Diviseur = 3 -> 38400 bauds
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); // 8 bits, pas de parité, 1 bit de stop
    outb(COM1 + 2, 0xC7); // Active + vide les FIFO, seuil 14 octets
    outb(COM1 + 4, 0x0B); // IRQs activées, RTS/DSR set
}

static int serial_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_write_char(char c) {
    while (!serial_transmit_empty()) { }
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s) {
    while (*s) serial_write_char(*s++);
}
