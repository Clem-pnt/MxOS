#ifndef MEM_H
#define MEM_H
#include <stdint.h>
#include <stddef.h>

void heap_init(uint32_t start, uint32_t size);
void* kmalloc(uint32_t size);
void kfree(void* ptr);
void heap_stats(uint32_t *used, uint32_t *free);

#endif