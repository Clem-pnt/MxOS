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
- **Chargeur de programmes utilisateur** (`kernel/exec.c`, `userprogs/`) :
  format binaire plat compilé/lié séparément, chargé depuis le système de
  fichiers par la commande `exec <fichier>` et exécuté dans son propre
  espace d'adressage isolé. Plusieurs programmes peuvent tourner
  **simultanément** : jusqu'à `MAX_EXEC_SLOTS` (4) emplacements de
  chargement (`0x300000`, `0x330000`, `0x360000`, `0x390000`, chacun avec
  sa propre pile), réattribués dès qu'une tâche se termine. Un programme
  de démonstration (`userprogs/hello_user.c`) est embarqué dans l'image du
  noyau et écrit automatiquement sur le disque virtuel au premier boot
  (sous le nom `hello`), pour qu'`exec hello` fonctionne dès le départ.
- **Ordonnanceur préemptif** (`kernel/sched.c`) : tâches noyau et
  utilisateur, sommeil (`task_sleep`), sortie propre, bascule de CR3.
- **Syscalls** (`kernel/syscall.c`) : `SYS_PRINT`, `SYS_EXIT`, `SYS_SLEEP`,
  `SYS_SEND`/`SYS_RECV` (IPC) via `int 0x80`.
- **IPC basique par boîte aux lettres** (`ipc_send`/`ipc_recv` dans
  `kernel/sched.c`) : chaque tâche dispose d'une case mailbox (1 message en
  attente maximum) ; le noyau copie les octets entre l'expéditeur et le
  destinataire, aucune tâche n'accède jamais directement à la mémoire d'une
  autre. Démo : la tâche ring3 `UserDemo` envoie un message au shell (PID 0),
  qui l'affiche via `shell_poll_ipc()`.
- **Système de fichiers** (`drivers/fs.c`) sur disque ATA PIO
  (`drivers/ata.c`) : `ls`, `cat`, `write`, `rm`, table racine persistée.
  Allocation par **bitmap** (1 octet/secteur, secteur dédié `FS_BITMAP_LBA`) :
  l'espace libéré par `rm` est réellement récupéré et réutilisable par les
  écritures suivantes (contrairement à l'ancien "bump allocator").
- **Shell** (`kernel/shell.c`) : `help`, `clear`, `ver`, `ls`, `cat`, `write`,
  `rm`, `exec`, `ps`, `mem`, `uptime`, `reboot`. Historique de commandes
  (flèches haut/bas, 8 dernières commandes) et prise en charge de la touche
  Shift (majuscules/symboles) via `kernel/keyboard.c`.
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

Construit le projet puis démarre l'image dans QEMU en headless sur **trois
lancements distincts** (pour limiter les risques de dérive de synchronisation
clavier sur de longues séquences), injecte des commandes shell via le
moniteur QEMU, capture la sortie sur le port série, puis vérifie par
recherche de motifs que le boot, l'espace utilisateur ring3, l'IPC,
l'allocateur bitmap, l'exec concurrent et le clavier (Shift + historique)
fonctionnent correctement. Code de sortie non nul en cas d'échec (logs
conservés dans `/tmp/mxos_test*_failed.log`).

## Limitations connues

- Les emplacements `exec` (1 à 3) chargent le même binaire lié pour
  l'adresse `0x300000` (slot 0) : cela ne fonctionne correctement que parce
  que `hello_user.c` est trivial et ne contient aucune référence absolue à
  ses propres données (uniquement des appels relatifs). Un programme plus
  complexe devrait soit toujours tourner sur le slot 0, soit passer par un
  vrai chargeur relogeant les adresses (non implémenté ici).
- L'IPC ne gère qu'un message en attente par tâche (pas de file) et n'est
  pas bloquant : `ipc_recv` renvoie immédiatement si la boîte est vide.
- Jusqu'à 6 espaces d'adressage isolés simultanés (`MAX_ADDR_SPACES` dans
  `kernel/paging.c`), tous types de tâches ring3 confondus (démo compilée
  au boot + jusqu'à 4 emplacements `exec` + marge).
- La touche Shift ne modifie que les lettres (majuscules) ; les chiffres et
  symboles restent non « shiftés » par simplicité.
- L'historique de commandes conserve les 8 dernières commandes (tampon
  circulaire), sans persistance entre redémarrages.
