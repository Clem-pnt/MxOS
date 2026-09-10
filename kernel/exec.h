#ifndef EXEC_H
#define EXEC_H

// Écrit sur le disque (une seule fois, si absent) le programme utilisateur
// de démonstration embarqué dans l'image du noyau, afin que `exec` ait un
// fichier réel à charger dès le premier boot. Doit être appelé après fs_init().
void exec_seed_programs(void);

// Charge le fichier `name` depuis le système de fichiers, en tant qu'ELF32
// i386 relogeable (cf. kernel/elf.h), et crée une tâche ring3 isolée qui
// l'exécute avec les arguments `argv[0..argc-1]` (argv[0] est conventionnellement
// le nom du programme). Renvoie le PID (>=0) en cas de succès, -1 sinon
// (fichier introuvable, trop volumineux, format invalide, ou limite de
// tâches/espaces d'adressage atteinte).
int exec_run(char *name, int argc, char **argv);

#endif
