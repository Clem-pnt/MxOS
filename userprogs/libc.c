#include "libc.h"

#define SYS_PRINT 1
#define SYS_EXIT  2
#define SYS_SLEEP 3
#define SYS_SEND  4
#define SYS_RECV  5

void sys_print(const char *msg) {
    __asm__ volatile("int $0x80" : : "a"(SYS_PRINT), "b"(msg) : "memory");
}

void sys_exit(void) {
    __asm__ volatile("int $0x80" : : "a"(SYS_EXIT));
    for (;;) { } /* Jamais atteint : sys_exit() ne revient pas. */
}

void sys_sleep(uint32_t ticks) {
    __asm__ volatile("int $0x80" : : "a"(SYS_SLEEP), "b"(ticks) : "memory");
}

uint32_t sys_send(int pid, const void *msg, uint32_t len) {
    uint32_t ret;
    __asm__ volatile("int $0x80"
                      : "=a"(ret)
                      : "a"(SYS_SEND), "b"(pid), "c"(msg), "d"(len)
                      : "memory");
    return ret;
}

int sys_recv(void *buf, uint32_t *out_len, int *out_sender) {
    int ret;
    __asm__ volatile("int $0x80"
                      : "=a"(ret)
                      : "a"(SYS_RECV), "b"(buf), "c"(out_len), "d"(out_sender)
                      : "memory");
    return ret;
}

uint32_t u_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

int u_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

char *u_strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) { }
    return dst;
}
