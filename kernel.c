// Necessaire pour uint32_t
#include <stdint.h>
#include "kernel/sched.h"

// Tache secondaire placee APRES
void task_clock() {
    unsigned char *vidmem = (unsigned char *)0xb8000;
    uint32_t t = 0;
    char symbols[] = "-\\|/";
    // Coin haut-droit (colonne 79, ligne 0) pour ne pas gener le shell
    const int offset = (79 * 2);
    while (1) {
        vidmem[offset] = symbols[t++ % 4];
        vidmem[offset + 1] = 0x0E;
        task_sleep(200);
    }
}
