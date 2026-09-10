#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

/* --- CONSTANTES DE COULEUR --- */
#define WHITE_ON_BLUE 0x1F
#define YELLOW_ON_BLUE 0x1E
#define RED_ON_BLUE    0x1C

/* --- PROTOTYPES --- */
void kprint_char(char c);
void kprint_char_at(char c, int x, int y);
void kprint(char *str);
void kprint_at(char *str, int x, int y);
void clear_screen();
void kprint_dec(uint32_t value);
void kprint_hex(uint32_t value);

#endif