#ifndef SHELL_H
#define SHELL_H

void shell_init();
void shell_update(char c); // Appelé par le keyboard_handler
void shell_execute();      // Interprète la commande
void shell_poll();
#endif