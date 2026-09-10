# =============================================================================
# MxOS - Project Status & Configuration
# =============================================================================

# Project Information
PROJECT_NAME = MxOS
VERSION      = 0.1
ARCH         = x86 (i386)
BOOTLOADER   = NASM (sector 0)
KERNEL_BASE  = 0x1000

# -----------------------------------------------------------------------------
# Current State of Modules
# -----------------------------------------------------------------------------

# [DONE] Bootloader: Loads a reserved 63-sector kernel area and jumps to 0x1000.
# [DONE] Kernel Init: PIC remapping, exception/IRQ IDT setup, multitasking entry.
# [DONE] VGA Driver: Basic text mode screen output (kprint, clear_screen).
# [DONE] Keyboard Driver: IRQ1 handling, scancode to ASCII conversion.
# [DONE] Multitasking: Basic round-robin scheduler (2 tasks max).
# [DONE] Memory: Primitive sequential allocator starting at 1MB (no free).
# [DONE] ATA Driver: PIO mode disk access with bounded waits and error checks.
# [DONE] Filesystem: Sector-based flat FS (root at LBA 64, data from LBA 65).
# [PARTIAL] Shell: Simple command processor (help, clear, ver, ls, cat).

# -----------------------------------------------------------------------------
# Build Configuration (Extracted from compile.bat)
# -----------------------------------------------------------------------------

CC       = gcc
LD       = ld
OBJCOPY  = objcopy
ASM      = nasm

CFLAGS   = -m32 -ffreestanding -fno-stack-protector -fno-pie \
           -mgeneral-regs-only -fno-leading-underscore -I. -nostdlib
LDFLAGS  = -m elf_i386 -T linker.ld (Makefile/Linux; compile.bat uses i386pe)

# -----------------------------------------------------------------------------
# Roadmap / To-Do
# -----------------------------------------------------------------------------
# - Implement proper memory management (kmalloc/kfree with heap/paging).
# - Expand scheduler to support dynamic task creation and priorities.
# - Improve FS (support directories, variable file sizes, better metadata).
# - Add more CLI commands and system calls.
# - Implement a basic userspace.
# - Add a real filesystem image builder for file contents.

# =============================================================================
