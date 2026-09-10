CC       ?= gcc
LD       ?= ld
OBJCOPY  ?= objcopy
ASM      ?= nasm
QEMU     ?= qemu-system-i386

CFLAGS   := -m32 -ffreestanding -fno-stack-protector -fno-pie \
            -mgeneral-regs-only -fno-leading-underscore -I. -nostdlib
LDFLAGS  := -m elf_i386 -T linker.ld
KERNEL_SECTORS := 63
KERNEL_BYTES := $(shell expr $(KERNEL_SECTORS) \* 512)

KERNEL_OBJECTS := kernel.o \
	kernel/init.o kernel/cpu.o kernel/interrupts.o kernel/mem.o \
	kernel/screen.o kernel/sched.o kernel/shell.o kernel/keyboard.o \
	kernel/paging.o kernel/exceptions.o kernel/pit.o kernel/syscall.o \
	kernel/gdt.o kernel/usermode.o \
	drivers/ata.o drivers/fs.o

DATA_SECTORS := 512
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
	rm -f $(KERNEL_OBJECTS) boot.bin kernel.tmp kernel.bin root.bin data.bin mxos_image.img
