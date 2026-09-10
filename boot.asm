[org 0x7c00]
KERNEL_OFFSET equ 0x8000
KERNEL_SECTORS equ 80

xor ax, ax
mov ds, ax
mov es, ax
mov ss, ax
mov [BOOT_DRIVE], dl
mov bp, 0x7b00
mov sp, bp

call load_kernel
jc disk_error

call switch_to_pm
jmp $

print_string_16:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp print_string_16
.done:
    ret

[bits 16]
load_kernel:
    mov ax, 0
    mov es, ax
    mov di, 0x600
    mov ah, 0x08
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .failed

    and cl, 0x3f          ; CL bits 0-5 = sectors per track
    mov [SECTORS_PER_TRACK], cl
    inc dh                 ; DH = max head index -> head count
    mov [HEAD_COUNT], dh

    mov ax, 0
    mov es, ax
    mov bx, KERNEL_OFFSET
    mov si, 1              ; sectors already read
    mov ch, 0
    mov dh, 0
    mov cl, 2

.next_sector:
    mov ah, 0x02
    mov al, 1
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc .failed

    add bx, 512
    jnc .no_segment_bump
    ; BX (offset 16 bits) vient de déborder au-delà de 0xFFFF : on avance
    ; ES de 0x1000 (soit 0x1000*16 = 0x10000 en adresse physique) pour
    ; compenser exactement ce débordement et continuer à charger le noyau
    ; de façon linéaire en mémoire au-delà de la limite de 64 Ko d'un seul
    ; segment réel-mode. Sans cela, BX boucle silencieusement sur lui-même
    ; après KERNEL_OFFSET+64 Ko (~63 secteurs à partir de 0x8000), et les
    ; secteurs suivants du noyau écrasent le début du noyau déjà chargé.
    mov ax, es
    add ax, 0x1000
    mov es, ax
.no_segment_bump:
    inc cl
    cmp cl, [SECTORS_PER_TRACK]
    jbe .continue
    mov cl, 1
    inc dh
    cmp dh, [HEAD_COUNT]
    jb .continue
    mov dh, 0
    inc ch

.continue:
    inc si
    cmp si, KERNEL_SECTORS + 1
    jb .next_sector
    clc
    ret
.failed:
    stc
    ret

disk_error:
    mov si, disk_error_message
.print_error:
    lodsb
    test al, al
    jz $
    mov ah, 0x0e
    int 0x10
    jmp .print_error

switch_to_pm:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:init_pm

gdt_start:
    dq 0x0
gdt_code:
    dw 0xffff, 0x0, 0x9a00, 0x00cf
gdt_data:
    dw 0xffff, 0x0, 0x9200, 0x00cf
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ebp, 0x90000
    mov esp, ebp
    call KERNEL_OFFSET ; Saute dans le kernel.c
    jmp $

BOOT_DRIVE db 0
SECTORS_PER_TRACK db 0
HEAD_COUNT db 0
disk_error_message db "Disk read error", 0

times 510-($-$$) db 0
dw 0xaa55