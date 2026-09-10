@echo off
set CC=gcc
set LD=ld
set OBJCOPY=objcopy
set ASM=nasm

:: Paramètres
set CFLAGS=-m32 -ffreestanding -fno-stack-protector -fno-pie -mgeneral-regs-only -fno-leading-underscore -I. -nostdlib
set LDFLAGS=-m i386pe -T linker.ld

echo [1/6] Nettoyage...
del /s /q *.o kernel.bin kernel.tmp mxos_image.img boot.bin 2>nul

echo [2/6] Compilation du Bootloader (ASM)...
if not exist boot.asm echo ERREUR: boot.asm manquant ! && pause && exit /b
%ASM% -f bin boot.asm -o boot.bin

echo [3/6] Modules Kernel...
%CC% %CFLAGS% -c kernel.c -o kernel.o
%CC% %CFLAGS% -c kernel/init.c -o kernel/init.o
%CC% %CFLAGS% -c kernel/cpu.c -o kernel/cpu.o
%CC% %CFLAGS% -c kernel/interrupts.c -o kernel/interrupts.o
%CC% %CFLAGS% -c kernel/mem.c -o kernel/mem.o
%CC% %CFLAGS% -c kernel/screen.c -o kernel/screen.o
%CC% %CFLAGS% -c kernel/sched.c -o kernel/sched.o
%CC% %CFLAGS% -c kernel/shell.c -o kernel/shell.o
%CC% %CFLAGS% -c kernel/keyboard.c -o kernel/keyboard.o
%CC% %CFLAGS% -c kernel/paging.c -o kernel/paging.o
%CC% %CFLAGS% -c kernel/exceptions.c -o kernel/exceptions.o
%CC% %CFLAGS% -c kernel/pit.c -o kernel/pit.o
%CC% %CFLAGS% -c kernel/syscall.c -o kernel/syscall.o
%CC% %CFLAGS% -c kernel/gdt.c -o kernel/gdt.o
%CC% %CFLAGS% -c kernel/usermode.c -o kernel/usermode.o
%CC% %CFLAGS% -c kernel/serial.c -o kernel/serial.o
%CC% %CFLAGS% -c kernel/exec.c -o kernel/exec.o

echo [3b/6] Programme utilisateur de demonstration (charge par exec)...
%CC% %CFLAGS% -c userprogs/hello_user.c -o userprogs/hello_user.o
%LD% -m i386pe -T userprogs/user.ld -o userprogs/hello_user.tmp userprogs/hello_user.o
%OBJCOPY% -S -O binary userprogs/hello_user.tmp userprogs/hello_user.bin
cd userprogs && %OBJCOPY% -I binary -O pe-i386 -B i386 hello_user.bin hello_user_blob.o && cd ..

echo [4/6] Drivers...
%CC% %CFLAGS% -c drivers/ata.c -o drivers/ata.o
%CC% %CFLAGS% -c drivers/fs.c -o drivers/fs.o

echo [5/6] Linkage et Extraction...
%LD% %LDFLAGS% -o kernel.tmp kernel.o kernel/init.o kernel/cpu.o kernel/interrupts.o kernel/mem.o kernel/screen.o kernel/sched.o kernel/shell.o kernel/keyboard.o kernel/paging.o kernel/exceptions.o kernel/pit.o kernel/syscall.o kernel/gdt.o kernel/usermode.o kernel/serial.o kernel/exec.o userprogs/hello_user_blob.o drivers/ata.o drivers/fs.o

if not exist kernel.tmp echo ERREUR: Linkage echoue ! && pause && exit /b
%OBJCOPY% -S -O binary kernel.tmp kernel.bin

echo [6/6] Creation de l'image disque...
if not exist boot.bin echo ERREUR: boot.bin manquant ! && pause && exit /b
if not exist kernel.bin echo ERREUR: kernel.bin manquant ! && pause && exit /b

:: Le bootloader reserve 63 secteurs pour le noyau.
powershell -NoProfile -Command "$p=[IO.File]::ReadAllBytes('kernel.bin'); if ($p.Length -gt 32256) { throw 'kernel.bin depasse 63 secteurs' }; $o=New-Object byte[] 32256; [Array]::Copy($p,$o,$p.Length); [IO.File]::WriteAllBytes('kernel.bin',$o)"
powershell -NoProfile -Command "$o=New-Object byte[] 512; $o[0]=0x4d; $o[1]=0x46; $o[2]=0x53; $o[3]=0x31; [IO.File]::WriteAllBytes('root.bin',$o)"
:: Zone de donnees du systeme de fichiers (256 Ko, au-dela de root.bin).
powershell -NoProfile -Command "[IO.File]::WriteAllBytes('data.bin', (New-Object byte[] 262144))"
copy /b boot.bin + kernel.bin + root.bin + data.bin mxos_image.img

echo.
echo ========================================
echo   SUCCESS : mxos_image.img est pret !
echo ========================================

qemu-system-i386 -drive format=raw,file=mxos_image.img
pause