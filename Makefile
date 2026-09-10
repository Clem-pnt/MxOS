CC       ?= gcc
LD       ?= ld
OBJCOPY  ?= objcopy
ASM      ?= nasm
QEMU     ?= qemu-system-i386

CFLAGS   := -m32 -ffreestanding -fno-stack-protector -fno-pie \
            -mgeneral-regs-only -fno-leading-underscore -I. -nostdlib
LDFLAGS  := -m elf_i386 -T linker.ld
KERNEL_SECTORS := 80
KERNEL_BYTES := $(shell expr $(KERNEL_SECTORS) \* 512)

KERNEL_OBJECTS := kernel.o \
	kernel/init.o kernel/cpu.o kernel/interrupts.o kernel/mem.o \
	kernel/screen.o kernel/sched.o kernel/shell.o kernel/keyboard.o \
	kernel/paging.o kernel/exceptions.o kernel/pit.o kernel/syscall.o \
	kernel/gdt.o kernel/usermode.o kernel/serial.o kernel/exec.o kernel/elf.o \
	userprogs/hello_user_blob.o userprogs/echo_user_blob.o \
	drivers/ata.o drivers/fs.o

DATA_SECTORS := 520
DATA_BYTES := $(shell expr $(DATA_SECTORS) \* 512)

.PHONY: all clean run

all: mxos_image.img

boot.bin: boot.asm
	$(ASM) -f bin $< -o $@

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -c $< -o $@

drivers/%.o: drivers/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# --- Programme utilisateur de démonstration (chargé par `exec`, cf. kernel/exec.c) ---
# Compilé/lié séparément du noyau en un vrai exécutable ELF32 i386, lié pour
# l'adresse 0x300000 (userprogs/user.ld) mais avec `--emit-relocs` : le
# fichier final GARDE ses relocations (R_386_32/R_386_PC32) au lieu de les
# "consommer" comme le ferait un exécutable normal, ce qui permet à
# kernel/elf.c de le RELOGER (charger correctement à une autre adresse,
# cf. les 4 emplacements concurrents de kernel/exec.c). Le fichier .elf
# complet (pas un binaire plat) est ensuite réembarqué comme blob de données
# dans l'image du noyau (symboles _binary_hello_user_elf_start/_end générés
# par objcopy -I binary), afin qu'exec_seed_programs() puisse l'écrire sur
# le disque virtuel au premier boot.
userprogs/hello_user.o: userprogs/hello_user.c userprogs/libc.h
	$(CC) $(CFLAGS) -c $< -o $@

userprogs/libc.o: userprogs/libc.c userprogs/libc.h
	$(CC) $(CFLAGS) -c $< -o $@

userprogs/hello_user.elf: userprogs/hello_user.o userprogs/libc.o userprogs/user.ld
	$(LD) -m elf_i386 -T userprogs/user.ld --emit-relocs -o $@ userprogs/hello_user.o userprogs/libc.o

userprogs/hello_user_blob.o: userprogs/hello_user.elf
	cd userprogs && $(OBJCOPY) -I binary -O elf32-i386 -B i386 hello_user.elf hello_user_blob.o

# --- Second programme utilisateur : echo + IPC via la mini-libc ---
# Même schéma de build que hello_user, démontre argv "recomposé" en une
# ligne et un envoi IPC (sys_send) vers le Shell (cf. userprogs/echo_user.c).
userprogs/echo_user.o: userprogs/echo_user.c userprogs/libc.h
	$(CC) $(CFLAGS) -c $< -o $@

userprogs/echo_user.elf: userprogs/echo_user.o userprogs/libc.o userprogs/user.ld
	$(LD) -m elf_i386 -T userprogs/user.ld --emit-relocs -o $@ userprogs/echo_user.o userprogs/libc.o

userprogs/echo_user_blob.o: userprogs/echo_user.elf
	cd userprogs && $(OBJCOPY) -I binary -O elf32-i386 -B i386 echo_user.elf echo_user_blob.o

kernel.tmp: $(KERNEL_OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJECTS)

kernel.bin: kernel.tmp
	$(OBJCOPY) -S -O binary $< $@
	truncate -s $(KERNEL_BYTES) $@

root.bin:
	printf '\x4d\x46\x53\x31' > $@
	truncate -s 512 $@

# Zone de données du système de fichiers (FS_DATA_LBA et au-delà),
# nécessaire pour que QEMU accepte les écritures ATA au-delà de root.bin.
data.bin:
	: > $@
	truncate -s $(DATA_BYTES) $@

mxos_image.img: boot.bin kernel.bin root.bin data.bin
	cat boot.bin kernel.bin root.bin data.bin > $@

run: mxos_image.img
	$(QEMU) -drive format=raw,file=$<

clean:
	rm -f $(KERNEL_OBJECTS) boot.bin kernel.tmp kernel.bin root.bin data.bin mxos_image.img \
		userprogs/hello_user.o userprogs/hello_user.elf userprogs/hello_user_blob.o \
		userprogs/echo_user.o userprogs/echo_user.elf userprogs/echo_user_blob.o \
		userprogs/libc.o

