#ifndef SHELL_H
#define SHELL_H

void shell_init();
void shell_update(char c); // Appelé par le keyboard_handler
void shell_execute();      // Interprète la commande
void shell_poll();
void shell_poll_ipc(); // Vérifie la boîte aux lettres IPC du shell (PID 0)
#endif