#include "shell.h"
#include "screen.h"
#include "keyboard.h"
#include "mem.h"
#include "pit.h"
#include "cpu.h"
#include "sched.h"
#include "exec.h"
#include "../drivers/fs.h"

static char input_buffer[256];
static int buffer_idx = 0;

// Historique de commandes (tampon circulaire) pour les flèches haut/bas,
// et persistance sur disque (cf. history_save/history_load_from_disk) sous
// forme de simple texte (une commande par ligne, séparateur '\n') dans le
// fichier HISTORY_FILE : survit donc aux redémarrages.
#define HISTORY_SIZE 8
#define HISTORY_FILE "history.dat"
#define MOTD_FILE "motd"
#define EXEC_MAX_ARGS 8
static char history[HISTORY_SIZE][256];
static int history_len[HISTORY_SIZE];
static int history_count = 0; // Nombre total de commandes jamais enregistrées
static int history_nav = -1;  // -1 = pas en navigation ; 0 = commande la plus récente, etc.
static char saved_line[256];  // Ligne en cours de frappe, sauvegardée en entrant en navigation
static int saved_line_len = 0;

// Déclaration anticipée : history_push() est définie plus bas (utilisée
// aussi par shell_execute()), mais history_load_from_disk() en a besoin ici.
static void history_push(char *cmd, int len);

// Sérialise l'historique actuellement en mémoire (les commandes les plus
// récentes, jusqu'à HISTORY_SIZE) vers HISTORY_FILE, une commande par ligne.
// Appelé après chaque commande (cf. shell_execute) : fs_delete_file_silent
// + fs_write_file_silent ne produisent aucune sortie, pour ne pas polluer
// le shell à chaque frappe de touche "Entrée".
static void history_save(void) {
    static char buf[HISTORY_SIZE * 256];
    uint32_t len = 0;

    int max_steps = history_count < HISTORY_SIZE ? history_count : HISTORY_SIZE;
    for (int steps_back = max_steps - 1; steps_back >= 0; steps_back--) {
        int slot = (history_count - 1 - steps_back + HISTORY_SIZE) % HISTORY_SIZE;
        int l = history_len[slot];
        for (int i = 0; i < l && len < sizeof(buf) - 1; i++) buf[len++] = history[slot][i];
        if (len < sizeof(buf) - 1) buf[len++] = '\n';
    }

    if (len == 0) return;
    fs_delete_file_silent(HISTORY_FILE);
    fs_write_file_silent(HISTORY_FILE, (const uint8_t*)buf, len);
}

// Charge l'historique persisté par une session précédente (s'il existe) :
// relit HISTORY_FILE et repousse chaque ligne dans le tampon circulaire, en
// préservant l'ordre chronologique. Appelé une seule fois par shell_init().
//
// fs_load_file() renvoie une taille ARRONDIE au secteur de 512 octets (cf.
// drivers/fs.c : size_sect * 512), pas la longueur réelle écrite : le reste
// du dernier secteur est du padding à zéro (fs_write_file_impl le remplit
// de zéros au-delà du contenu). Comme une commande shell ne contient jamais
// d'octet nul, on tronque donc `size` au premier octet 0 rencontré pour ne
// traiter que le contenu réellement écrit.
static uint32_t trim_to_first_nul(uint8_t *buf, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        if (buf[i] == 0) return i;
    }
    return size;
}

static void history_load_from_disk(void) {
    static uint8_t buf[HISTORY_SIZE * 256];
    if (!fs_file_exists(HISTORY_FILE)) return;

    uint32_t size = 0;
    if (!fs_load_file(HISTORY_FILE, buf, sizeof(buf), &size)) return;
    size = trim_to_first_nul(buf, size);

    uint32_t start = 0;
    for (uint32_t i = 0; i <= size; i++) {
        if (i == size || buf[i] == '\n') {
            if (i > start) history_push((char*)&buf[start], (int)(i - start));
            start = i + 1;
        }
    }
}

// Affiche le "message du jour" persisté dans MOTD_FILE s'il existe (créé
// simplement via `write motd <texte>`, la commande générique existante) :
// démontre que la persistance de configuration n'a besoin d'aucun format
// spécial, juste du système de fichiers déjà en place.
static void shell_show_motd(void) {
    static uint8_t buf[512];
    if (!fs_file_exists(MOTD_FILE)) return;

    uint32_t size = 0;
    if (!fs_load_file(MOTD_FILE, buf, sizeof(buf), &size)) return;
    size = trim_to_first_nul(buf, size);

    kprint("\n--- ");
    kprint(MOTD_FILE);
    kprint(" ---\n");
    for (uint32_t i = 0; i < size; i++) kprint_char((char)buf[i]);
    kprint("\n");
}

void shell_init() {
    kprint("\nMxOS Shell v1.0\nTapez 'help' pour la liste des commandes.\n");
    history_load_from_disk();
    shell_show_motd();
    kprint("> ");
    buffer_idx = 0;
}

// Utile pour comparer les commandes sans avoir de bibliothèque standard C (string.h)
int m_strcmp(char *s1, char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int m_strncmp(char *s1, char *s2, int n) {
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int m_strlen(char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

// Enregistre `cmd` (longueur `len`) dans le tampon circulaire d'historique.
static void history_push(char *cmd, int len) {
    if (len > 255) len = 255;
    int slot = history_count % HISTORY_SIZE;
    for (int i = 0; i < len; i++) history[slot][i] = cmd[i];
    history_len[slot] = len;
    history_count++;
}

// Efface visuellement la ligne de commande affichée (autant de backspaces
// que de caractères déjà tapés) puis réaffiche le nouveau contenu de
// `input_buffer` (longueur `new_len`) : utilisé par la navigation flèches.
static void redraw_input_line(int new_len) {
    while (buffer_idx > 0) {
        kprint_char('\b');
        buffer_idx--;
    }
    for (int i = 0; i < new_len; i++) {
        kprint_char(input_buffer[i]);
    }
    buffer_idx = new_len;
}

// Charge dans `input_buffer` la commande d'historique à `steps_back` crans
// dans le passé (0 = la plus récente) et redessine la ligne.
static void history_load(int steps_back) {
    int slot = (history_count - 1 - steps_back + HISTORY_SIZE) % HISTORY_SIZE;
    int len = history_len[slot];
    for (int i = 0; i < len; i++) input_buffer[i] = history[slot][i];
    redraw_input_line(len);
}

// Flèche haut : remonte vers une commande plus ancienne. À la première
// pression, sauvegarde la ligne en cours de frappe pour pouvoir y revenir.
static void history_up(void) {
    if (history_count == 0) return;
    int max_steps = history_count < HISTORY_SIZE ? history_count : HISTORY_SIZE;

    if (history_nav == -1) {
        saved_line_len = buffer_idx;
        for (int i = 0; i < buffer_idx; i++) saved_line[i] = input_buffer[i];
        history_nav = 0;
    } else if (history_nav + 1 < max_steps) {
        history_nav++;
    }
    history_load(history_nav);
}

// Flèche bas : redescend vers une commande plus récente, puis restaure la
// ligne en cours de frappe qui avait été sauvegardée.
static void history_down(void) {
    if (history_nav == -1) return;

    if (history_nav == 0) {
        history_nav = -1;
        for (int i = 0; i < saved_line_len; i++) input_buffer[i] = saved_line[i];
        redraw_input_line(saved_line_len);
    } else {
        history_nav--;
        history_load(history_nav);
    }
}

void shell_execute() {
    kprint("\n");
    input_buffer[buffer_idx] = '\0'; // Termine la chaîne

    if (buffer_idx > 0) {
        history_push(input_buffer, buffer_idx);
        history_save();
    }
    history_nav = -1;

    if (m_strcmp(input_buffer, "help") == 0) {
        kprint("Commandes : help, clear, ver, ls, cat <file>, write <file> <contenu>,\nrm <file>, exec <file> [args...], ps, mem, uptime, reboot");
    } 
    else if (m_strcmp(input_buffer, "clear") == 0) {
        clear_screen();
    } 
    else if (m_strcmp(input_buffer, "ver") == 0) {
        kprint("MxOS Kernel v1.1.2 (Preemptive/ATA-PIO)");
    } 
    else if (m_strcmp(input_buffer, "ls") == 0) {
        fs_list();
    }
    else if (m_strncmp(input_buffer, "cat ", 4) == 0) {
        fs_read_file(input_buffer + 4);
    }
    else if (m_strncmp(input_buffer, "rm ", 3) == 0) {
        fs_delete_file(input_buffer + 3);
    }
    else if (m_strncmp(input_buffer, "exec ", 5) == 0) {
        // Découpe "exec <fichier> [arg1] [arg2] ..." en un tableau argv[]
        // (argv[0] = nom du fichier, comme la convention argc/argv usuelle),
        // en remplaçant les espaces par des zéros directement dans
        // input_buffer (même technique que la commande "write" ci-dessous).
        char *argv[EXEC_MAX_ARGS];
        int argc = 0;
        char *p = input_buffer + 5;
        while (*p == ' ') p++;
        while (*p && argc < EXEC_MAX_ARGS) {
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p == ' ') {
                *p = '\0';
                p++;
                while (*p == ' ') p++;
            }
        }
        if (argc == 0) {
            kprint("Usage : exec <fichier> [arguments...]");
        } else {
            exec_run(argv[0], argc, argv);
        }
    }
    else if (m_strcmp(input_buffer, "ps") == 0) {
        sched_dump_tasks();
    }
    else if (m_strncmp(input_buffer, "write ", 6) == 0) {
        char *args = input_buffer + 6;
        while (*args == ' ') args++;
        char *name = args;
        char *sep = args;
        while (*sep && *sep != ' ') sep++;
        if (*sep == ' ') {
            *sep = '\0';
            char *content = sep + 1;
            fs_write_file(name, (const uint8_t*)content, m_strlen(content));
        } else {
            kprint("Usage : write <nom> <contenu>");
        }
    }
    else if (m_strcmp(input_buffer, "mem") == 0) {
        uint32_t used = 0, free_bytes = 0;
        heap_stats(&used, &free_bytes);
        kprint("Heap - utilisee : ");
        kprint_dec(used);
        kprint(" octets, libre : ");
        kprint_dec(free_bytes);
        kprint(" octets");
    }
    else if (m_strcmp(input_buffer, "uptime") == 0) {
        uint32_t ticks = get_ticks();
        uint32_t freq = pit_get_frequency();
        uint32_t seconds = freq ? ticks / freq : 0;
        kprint("Uptime : ");
        kprint_dec(seconds);
        kprint(" secondes (");
        kprint_dec(ticks);
        kprint(" ticks)");
    }
    else if (m_strcmp(input_buffer, "reboot") == 0) {
        kprint("Redemarrage...\n");
        reboot();
    }
    else if (buffer_idx > 0) {
        kprint("Commande inconnue : ");
        kprint(input_buffer);
    }

    kprint("\n> ");
    buffer_idx = 0;
}

void shell_update(char c) {
    if (c == '\n') {
        shell_execute();
    } else if (c == '\b') {
        if (buffer_idx > 0) {
            buffer_idx--;
            kprint_char(c);
        }
    } else if (c == KEY_ARROW_UP) {
        history_up();
    } else if (c == KEY_ARROW_DOWN) {
        history_down();
    } else if (buffer_idx < 255) {
        input_buffer[buffer_idx++] = c;
        kprint_char(c);
    }
}

void shell_poll() {
    char c;
    while (keyboard_read_char(&c)) {
        shell_update(c);
    }
    shell_poll_ipc();
}

// Vérifie (sans bloquer) si un message IPC est arrivé dans la boîte aux
// lettres du shell (PID 0) et l'affiche. Démontre la communication entre
// tâches isolées : ipc_recv() copie les octets depuis la mémoire noyau,
// aucune tâche n'accède jamais directement à la mémoire d'une autre.
void shell_poll_ipc() {
    char buf[128];
    uint32_t len = 0;
    int sender = -1;

    if (ipc_recv(buf, &len, &sender)) {
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        buf[len] = '\0';
        kprint("\n[IPC] Message de la tache ");
        kprint_dec(sender);
        kprint(" : ");
        kprint(buf);
        kprint("\n");
    }
}