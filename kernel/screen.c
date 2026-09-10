#include "screen.h"

#define VIDEO_ADDRESS 0xb8000
#define MAX_ROWS 25
#define MAX_COLS 80

// Import des fonctions de cpu.c
extern void outb(uint16_t port, uint8_t data);
extern void serial_write_char(char c); // kernel/serial.c

static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = 0x1F; // Blanc sur Bleu

// Déplace le curseur matériel (le petit trait qui clignote)
void update_cursor() {
    uint16_t pos = (cursor_y * MAX_COLS) + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

// Fonction de scrolling : décale tout l'écran d'une ligne vers le haut
static void scroll() {
    uint8_t *vidmem = (uint8_t *)VIDEO_ADDRESS;
    // Copie chaque ligne sur la ligne précédente
    for (int i = 0; i < (MAX_ROWS - 1) * MAX_COLS * 2; i++) {
        vidmem[i] = vidmem[i + MAX_COLS * 2];
    }
    // Efface la dernière ligne
    for (int i = (MAX_ROWS - 1) * MAX_COLS * 2; i < MAX_ROWS * MAX_COLS * 2; i += 2) {
        vidmem[i] = ' ';
        vidmem[i+1] = current_color;
    }
    cursor_y = MAX_ROWS - 1;
}

void clear_screen() {
    uint8_t *vidmem = (uint8_t *)VIDEO_ADDRESS;
    for (int i = 0; i < MAX_ROWS * MAX_COLS * 2; i += 2) {
        vidmem[i] = ' ';
        vidmem[i+1] = current_color;
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

// Version coordonnée (renommée pour éviter le conflit)
void kprint_char_at(char c, int x, int y) {
    uint8_t *vidmem = (uint8_t *)VIDEO_ADDRESS;
    int offset = (y * MAX_COLS + x) * 2;
    vidmem[offset] = c;
    vidmem[offset + 1] = current_color;
}

// Version standard utilisée par kprint et le shell
void kprint_char(char c) {
    serial_write_char(c); // Miroir vers COM1 pour les tests automatisés

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        // Recule le curseur puis efface le caractère qui s'y trouvait
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = MAX_COLS - 1;
        }
        kprint_char_at(' ', cursor_x, cursor_y);
        update_cursor();
        return;
    } else {
        kprint_char_at(c, cursor_x, cursor_y);
        cursor_x++;
    }

    if (cursor_x >= MAX_COLS) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= MAX_ROWS) {
        scroll();
    }
    
    update_cursor();
}

void kprint(char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        kprint_char(str[i]);
    }
}

void kprint_at(char *str, int x, int y) {
    cursor_x = x;
    cursor_y = y;
    kprint(str);
}

// Affiche un entier non signé en base 10 (pas de printf disponible)
void kprint_dec(uint32_t value) {
    char buffer[11];
    int i = 10;
    buffer[10] = '\0';

    if (value == 0) {
        kprint_char('0');
        return;
    }

    while (value > 0 && i > 0) {
        buffer[--i] = '0' + (value % 10);
        value /= 10;
    }
    kprint(&buffer[i]);
}

// Affiche un entier non signé en hexadecimal, prefixe par 0x
void kprint_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[11];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buffer[2 + i] = digits[(value >> ((7 - i) * 4)) & 0xF];
    }
    buffer[10] = '\0';
    kprint(buffer);
}