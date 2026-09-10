#include "cpu.h"
#include <stdint.h>

static char key_queue[128];
static volatile uint8_t queue_read = 0;
static volatile uint8_t queue_write = 0;

__attribute__((interrupt, target("general-regs-only")))
void keyboard_handler(void* frame) {
    uint8_t sc = inb(0x60);
    if (!(sc & 0x80)) { // Touche pressée
        // Table de scan simple (AZERTY simplifié pour l'exemple)
        char kbd[] = {0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '};
        
        if (sc < sizeof(kbd) && kbd[sc] != 0) {
            uint8_t next = (uint8_t)(queue_write + 1);
            if (next != queue_read) {
                key_queue[queue_write] = kbd[sc];
                queue_write = next;
            }
        }
    }

    outb(0x20, 0x20); // EOI
}

int keyboard_read_char(char *out) {
    if (queue_read == queue_write) return 0;
    *out = key_queue[queue_read];
    queue_read = (uint8_t)(queue_read + 1);
    return 1;
}