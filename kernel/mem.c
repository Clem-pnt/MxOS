#include "mem.h"

typedef struct header {
    uint32_t size;
    struct header *next;
    uint8_t is_free;
} header_t;

static header_t *heap_start = NULL;

void heap_init(uint32_t start, uint32_t size) {
    heap_start = (header_t*)start;
    heap_start->size = size - sizeof(header_t);
    heap_start->next = NULL;
    heap_start->is_free = 1;
}

void* kmalloc(uint32_t size) {
    if (!heap_start) return NULL;

    size = (size + 3) & ~3; // Alignement 4 octets
    header_t *curr = heap_start;

    while (curr) {
        if (curr->is_free && curr->size >= size) {
            // Split block if large enough
            if (curr->size > size + sizeof(header_t) + 4) {
                header_t *next = (header_t*)((uint8_t*)curr + sizeof(header_t) + size);
                next->size = curr->size - size - sizeof(header_t);
                next->next = curr->next;
                next->is_free = 1;
                
                curr->size = size;
                curr->next = next;
            }
            curr->is_free = 0;
            return (void*)((uint8_t*)curr + sizeof(header_t));
        }
        curr = curr->next;
    }
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;

    header_t *header = (header_t*)((uint8_t*)ptr - sizeof(header_t));
    header->is_free = 1;

    // Coalesce with next block if free
    if (header->next && header->next->is_free) {
        header->size += header->next->size + sizeof(header_t);
        header->next = header->next->next;
    }
    
    // Note: To coalesce with previous, we'd need a doubly linked list or scan from start
    // For now, simple coalesce with next is a good start.
}

// Calcule les octets utilisés/libres en parcourant la liste chainee du heap
void heap_stats(uint32_t *used, uint32_t *free) {
    uint32_t used_total = 0;
    uint32_t free_total = 0;
    header_t *curr = heap_start;

    while (curr) {
        if (curr->is_free) {
            free_total += curr->size;
        } else {
            used_total += curr->size;
        }
        curr = curr->next;
    }

    if (used) *used = used_total;
    if (free) *free = free_total;
}
