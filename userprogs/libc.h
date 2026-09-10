/* Mini bibliothèque C pour les programmes utilisateur MxOS : regroupe les
 * enrobages d'appels système (int 0x80) et quelques fonctions de chaînes de
 * base, pour éviter de dupliquer ce code dans chaque programme (comme
 * c'était le cas dans hello_user.c à l'origine). Header-only + libc.c à
 * compiler/lier avec chaque programme utilisateur (cf. Makefile).
 */
#ifndef MXOS_LIBC_H
#define MXOS_LIBC_H

#include <stdint.h>

/* Enrobages des appels système (cf. kernel/syscall.h pour les numéros). */
void sys_print(const char *msg);
void sys_exit(void) __attribute__((noreturn));
void sys_sleep(uint32_t ticks);
/* Renvoie le nombre d'octets effectivement envoyés (0 = boîte aux lettres
 * du destinataire pleine, à réessayer plus tard). */
uint32_t sys_send(int pid, const void *msg, uint32_t len);
/* Non bloquant : renvoie 1 si un message a été reçu (copié dans buf, taille
 * réelle dans *out_len si non NULL, PID expéditeur dans *out_sender si non
 * NULL), 0 sinon. */
int sys_recv(void *buf, uint32_t *out_len, int *out_sender);

/* Petites fonctions de chaînes, semblables à celles de <string.h>, pour ne
 * pas dépendre de la libc du noyau (les programmes utilisateur sont liés à
 * part, cf. userprogs/user.ld). */
uint32_t u_strlen(const char *s);
int u_strcmp(const char *a, const char *b);
char *u_strcpy(char *dst, const char *src);

/* Lit argc/argv transmis par le noyau via EBX/ECX au tout premier
 * instant de user_main() (cf. kernel/sched.c create_user_task_argv).
 * DOIT être appelée en tout premier dans user_main(), avant que le
 * prologue du compilateur ne touche EBX/ECX, exactement comme le faisait
 * l'inline-asm de hello_user.c à l'origine. Ceci est donc une macro et non
 * une fonction (un appel de fonction romprait la convention registre à
 * registre dès l'entrée). */
#define MXOS_READ_ARGV(argc, argv) \
    __asm__ volatile("" : "=b"(argc), "=c"(argv))

#endif
