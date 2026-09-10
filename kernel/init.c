#include "kernel/interrupts.h"
#include "kernel/screen.h"
#include "kernel/sched.h"
#include "kernel/shell.h"
#include "kernel/paging.h"
#include "kernel/exceptions.h"
#include "kernel/pit.h"
#include "kernel/syscall.h"
#include "kernel/gdt.h"
#include "kernel/usermode.h"
#include "../drivers/fs.h"
#include "mem.h"  
#include "shell.h"

extern void keyboard_handler();

void task_clock();
extern uint8_t __bss_start;
extern uint8_t __bss_end;

void main_entry() __attribute__((section(".text.main")));

void main_entry() {
    for (uint8_t *p = &__bss_start; p < &__bss_end; p++) {
        *p = 0;
    }
    clear_screen();
    kprint("MxOS Kernel Loading...\n");

    gdt_init();
    remap_pic();

    install_exception_handlers();
    set_idt_gate(32, (uint32_t)timer_handler);
    set_idt_gate(33, (uint32_t)keyboard_handler);
    syscall_init();
    load_idt();

    pit_init(100);

    fs_init();
    shell_init();

    kprint("Enabling paging...\n");
    paging_init();

    kprint("Initializing memory...\n");
    heap_init(0x100000, 0x100000); // 1Mo à partir de 1Mo

    kprint("Starting scheduler...\n");
    init_multitasking();
    if (create_task(task_clock, "Clock") < 0) {
        kprint("Warning: unable to create clock task.\n");
    }
    if (create_user_task(user_task_demo, "UserDemo", user_demo_stack_top()) < 0) {
        kprint("Warning: unable to create user demo task.\n");
    }

    __asm__ volatile("sti");

    while (1) {
        shell_poll();
        __asm__ volatile("hlt");
    }
}
