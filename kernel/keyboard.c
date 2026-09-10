#include "keyboard.h"
#include "cpu.h"
#include <stdint.h>

static char key_queue[128];
static volatile uint8_t queue_read = 0;
static volatile uint8_t queue_write = 0;

// État des touches Shift (gauche 0x2A, droite 0x36) et du préfixe 0xE0 qui
// annonce les touches "étendues" (flèches, etc.) sur un clavier PS/2 standard.
static volatile uint8_t shift_pressed = 0;
static volatile uint8_t extended_prefix = 0;

static void queue_push(char c) {
    uint8_t next = (uint8_t)(queue_write + 1);
    if (next != queue_read) {
        key_queue[queue_write] = c;
        queue_write = next;
    }
}

__attribute__((interrupt, target("general-regs-only")))
void keyboard_handler(void* frame) {
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) {
        // Préfixe annonçant que le prochain octet est un scancode étendu
        // (flèches, Ctrl/Alt droits, etc.) : on l'attend simplement.
        extended_prefix = 1;
        outb(0x20, 0x20);
        return;
    }

    uint8_t make = !(sc & 0x80);
    uint8_t code = sc & 0x7F;

    if (extended_prefix) {
        extended_prefix = 0;
        if (make) {
            if (code == 0x48) queue_push(KEY_ARROW_UP);        // Flèche haut
            else if (code == 0x50) queue_push(KEY_ARROW_DOWN); // Flèche bas
            // Les autres touches étendues (gauche/droite, suppr, etc.) sont
            // ignorées pour l'instant : non utilisées par le shell.
        }
        outb(0x20, 0x20);
        return;
    }

    // Touches Shift : on ne fait que suivre leur état, sans les mettre en file.
    if (sc == 0x2A || sc == 0x36) {
        shift_pressed = 1;
        outb(0x20, 0x20);
        return;
    }
    if (sc == 0xAA || sc == 0xB6) {
        shift_pressed = 0;
        outb(0x20, 0x20);
        return;
    }

    if (make) {
        // Table de scan simple (QWERTY) : minuscules et version majuscule/
        // symbole "shiftée" correspondante (disposition US, cf. commentaire
        // historique "AZERTY simplifié" qui décrivait déjà en réalité un
        // agencement QWERTY basique).
        static const char kbd[] =      {0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '};
        static const char kbd_shift[] = {0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S','D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '};

        if (code < sizeof(kbd)) {
            char c = shift_pressed ? kbd_shift[code] : kbd[code];
            if (c != 0) queue_push(c);
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