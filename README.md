# MxOS

MxOS est un noyau x86 (i386) éducatif, écrit en C et en assembleur x86,
démarrant sur un vrai BIOS/QEMU en mode réel puis passant en mode protégé.
Il implémente un shell, un système de fichiers minimal sur disque ATA, un
ordonnanceur préemptif multi-tâches, et un espace utilisateur ring3 isolé
par répertoire de pages.

## Fonctionnalités

- **Bootloader** (`boot.asm`) : charge le noyau depuis le disque (63 secteurs
  max) et bascule en mode protégé 32 bits.
- **Interruptions & exceptions** (`kernel/interrupts.c`, `kernel/exceptions.c`) :
  IDT complète, gestionnaire dédié pour chacune des 32 exceptions CPU.
- **Pagination avec isolation par tâche** (`kernel/paging.c`) : chaque tâche
  ring3 dispose de son propre répertoire de pages. Seules sa pile utilisateur
  et la zone code/rodata/data partagée du noyau sont accessibles en ring3 ;
  le BSS noyau (structures internes, tas, piles des autres tâches) est
  supervisor-only et donc inaccessible depuis l'espace utilisateur.
- **GDT/TSS et ring3** (`kernel/gdt.c`, `kernel/usermode.c`) : segments
  noyau/utilisateur, TSS pour les transitions ring3→ring0, tâche de
  démonstration s'exécutant entièrement en ring3 via `int 0x80`.
- **Ordonnanceur préemptif** (`kernel/sched.c`) : tâches noyau et
  utilisateur, sommeil (`task_sleep`), sortie propre, bascule de CR3.
- **Syscalls** (`kernel/syscall.c`) : `SYS_PRINT`, `SYS_EXIT`, `SYS_SLEEP` via
  `int 0x80`.
- **Système de fichiers** (`drivers/fs.c`) sur disque ATA PIO
  (`drivers/ata.c`) : `ls`, `cat`, `write`, `rm`, table racine persistée.
- **Shell** (`kernel/shell.c`) : `help`, `clear`, `ver`, `ls`, `cat`, `write`,
  `rm`, `ps`, `mem`, `uptime`, `reboot`.
- **Sortie série (COM1)** (`kernel/serial.c`) : miroir de tout l'affichage
  écran, utilisé pour les tests automatisés sans capture d'écran.

## Compilation

```sh
make        # construit mxos_image.img
make run    # construit puis lance QEMU
make clean  # nettoie les artefacts de build
```

Sous Windows : `compile.bat` (équivalent utilisant `powershell`/`ld` PE).

## Tests automatisés

```sh
./test.sh
```

Construit le projet, démarre l'image dans QEMU en headless, injecte une
séquence de commandes shell via le moniteur QEMU, capture la sortie sur le
port série, puis vérifie par recherche de motifs que le boot, l'espace
utilisateur ring3 et les commandes du système de fichiers fonctionnent
correctement. Code de sortie non nul en cas d'échec (log conservé dans
`/tmp/mxos_test_failed.log`).

## Limitations connues

- Pas de vrai chargeur de binaires utilisateur : les tâches ring3
  actuelles sont des fonctions C compilées dans l'image du noyau.
- L'allocateur de blocs disque (`fs.c`) est un simple "bump allocator" :
  l'espace libéré par `rm` n'est pas récupéré.
- Une seule tâche à la fois peut se trouver en ring3 avec son propre
  répertoire de pages actif (jusqu'à 4 espaces d'adressage isolés
  simultanés, cf. `MAX_ADDR_SPACES` dans `kernel/paging.c`).
