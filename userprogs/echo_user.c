/* Second programme utilisateur de démonstration MxOS : illustre l'usage de
 * la mini-libc (userprogs/libc.h/.c) pour deux choses que hello_user.c ne
 * montrait pas ensemble : (1) recomposer les arguments reçus en UNE seule
 * ligne "echo"-like plutôt que de les afficher un par un, et (2) envoyer un
 * message IPC au Shell (PID 0, cf. kernel/sched.c : la tâche Shell est
 * toujours créée en premier) résumant ce qui a été reçu, pour vérifier bout
 * en bout `exec` + argv + IPC dans un seul programme.
 */
#include "libc.h"

#define SHELL_PID 0
#define LINE_MAX 128

void user_main(void) __attribute__((section(".text.start")));

void user_main(void) {
    uint32_t argc;
    char **argv;
    MXOS_READ_ARGV(argc, argv);

    static char line[LINE_MAX];
    uint32_t len = 0;

    sys_print("[echo] ");
    for (uint32_t i = 0; i < argc && len < LINE_MAX - 2; i++) {
        uint32_t alen = u_strlen(argv[i]);
        for (uint32_t j = 0; j < alen && len < LINE_MAX - 2; j++) {
            line[len++] = argv[i][j];
        }
        if (i + 1 < argc && len < LINE_MAX - 2) line[len++] = ' ';
    }
    line[len] = 0;

    sys_print(line);
    sys_print("\n");

    // Envoie le résultat au Shell par IPC (quelques essais au cas où sa
    // boîte aux lettres serait momentanément pleine, cf. MAILBOX_QUEUE_SIZE
    // dans kernel/sched.h) : démonstration de bout en bout exec+argv+IPC.
    for (int attempt = 0; attempt < 4; attempt++) {
        if (sys_send(SHELL_PID, line, len)) break;
        sys_sleep(1);
    }

    sys_exit();
}
