#ifndef KEYBOARD_H
#define KEYBOARD_H

// Codes spéciaux injectés dans la file au même titre que les caractères
// imprimables, pour les touches flèches (utilisées par l'historique de
// commandes du shell, cf. kernel/shell.c). Choisis dans la plage des
// caractères de contrôle ASCII inutilisés par le shell (DC1/DC2).
#define KEY_ARROW_UP   0x11
#define KEY_ARROW_DOWN 0x12

void keyboard_handler(void* frame);
int keyboard_read_char(char *out);

#endif