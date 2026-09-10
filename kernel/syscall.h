#ifndef SYSCALL_H
#define SYSCALL_H

#define SYS_PRINT 1
#define SYS_EXIT  2
#define SYS_SLEEP 3
#define SYS_SEND  4
#define SYS_RECV  5

void syscall_init(void);

#endif
