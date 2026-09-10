#include "shell.h"
#include "screen.h"
#include "keyboard.h"
#include "mem.h"
#include "pit.h"
#include "cpu.h"
#include "../drivers/fs.h"

static char input_buffer[256];
static int buffer_idx = 0;

void shell_init() {
    kprint("\nMxOS Shell v1.0\nTapez 'help' pour la liste des commandes.\n> ");
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

void shell_execute() {
    kprint("\n");
    input_buffer[buffer_idx] = '\0'; // Termine la chaîne

    if (m_strcmp(input_buffer, "help") == 0) {
        kprint("Commandes : help, clear, ver, ls, cat <file>, write <file> <contenu>,\nmem, uptime, reboot");
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
}