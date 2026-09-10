#include "pit.h"
#include "cpu.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182

static volatile uint32_t ticks = 0;
static uint32_t frequency = 100;

// Programme le PIT (8253/8254) pour generer des IRQ0 a la frequence donnee
void pit_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) frequency_hz = 100;
    frequency = frequency_hz;

    uint32_t divisor = PIT_BASE_FREQ / frequency_hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    outb(PIT_COMMAND, 0x36); // Canal 0, lobyte/hibyte, mode 3 (square wave)
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    ticks = 0;
}

// Appele depuis le handler de l'IRQ0 (timer) a chaque interruption
void timer_tick(void) {
    ticks++;
}

uint32_t get_ticks(void) {
    return ticks;
}

uint32_t pit_get_frequency(void) {
    return frequency;
}
