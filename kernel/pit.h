#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void pit_init(uint32_t frequency_hz);
void timer_tick(void);
uint32_t get_ticks(void);
uint32_t pit_get_frequency(void);

#endif
