#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

void set_idt_gate(int n, uint32_t handler);
void set_idt_gate_flags(int n, uint32_t handler, uint8_t flags);
void load_idt();
void remap_pic();

#endif
