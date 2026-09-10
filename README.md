# MxOS

MxOS est un noyau x86 (i386) éducatif, écrit en C et en assembleur x86,
démarrant sur un vrai BIOS/QEMU en mode réel puis passant en mode protégé.
Il implémente un shell, un système de fichiers minimal sur disque ATA, un
ordonnanceur préemptif multi-tâches, et un espace utilisateur ring3 isolé
par répertoire de pages.

## Fonctionnalités

- **Bootloader** (`boot.asm`) : charge le noyau depuis le disque (80 secteurs,
  `KERNEL_SECTORS`) et bascule en mode protégé 32 bits. La boucle de lecture
  gère correctement le dépassement 16 bits de `BX` (adressage réel
  segment:offset) au-delà de 64 Ko chargés en avançant `ES` de `0x1000` — un
  vrai bug latent découvert et corrigé en Phase 8 (voir "Bugs corrigés").
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
- **Chargeur ELF relogeant** (`kernel/exec.c`, `kernel/elf.c`, `userprogs/`) :
  les programmes utilisateur sont compilés/liés séparément en de véritables
  fichiers ELF32 (`ld --emit-relocs`, qui conserve les sections de
  relocation `SHT_REL` normalement supprimées d'un exécutable final),
  chargés depuis le système de fichiers par la commande
  `exec <fichier> [args...]` et exécutés dans leur propre espace
  d'adressage isolé. `kernel/elf.c` place les segments `PT_LOAD`, met à
  zéro le `.bss`, puis applique un décalage uniforme (`load_base -
  link_base`) à chaque relocation `R_386_32` (adresses absolues) : le
  **même** fichier ELF peut donc être chargé correctement à n'importe
  lequel des `MAX_EXEC_SLOTS` (4) emplacements (`0x300000`, `0x330000`,
  `0x360000`, `0x390000`, chacun avec sa propre pile), réattribués dès
  qu'une tâche se termine — ce n'est plus une coïncidence liée à la
  simplicité du programme de démo. Un programme de démonstration
  (`userprogs/hello_user.c`) est embarqué dans l'image du noyau et écrit
  automatiquement sur le disque virtuel au premier boot (sous le nom
  `hello`), pour qu'`exec hello` fonctionne dès le départ.
- **Arguments de programme (argc/argv)** : `exec <fichier> arg1 arg2 ...`
  transmet les arguments au programme lancé. Convention "maison" (pas de
  vrai ABI, projet éducatif) : `create_user_task_argv()` (`kernel/sched.c`)
  précharge EBX=argc/ECX=argv dans la pile simulée avant le tout premier
  `iret` de la tâche ; le programme les lit via un idiome GCC (opérandes de
  sortie sur un `asm volatile` vide) en tout premier dans `user_main()`.
  Les chaînes/tableau de pointeurs argv sont construits dans une petite
  zone réservée au bas de la pile de la tâche (accessible en ring3).
- **Ordonnanceur préemptif** (`kernel/sched.c`) : tâches noyau et
  utilisateur, sommeil (`task_sleep`), sortie propre, bascule de CR3.
- **Syscalls** (`kernel/syscall.c`) : `SYS_PRINT`, `SYS_EXIT`, `SYS_SLEEP`,
  `SYS_SEND`/`SYS_RECV` (IPC) via `int 0x80`.
- **IPC basique par boîtes aux lettres avec file FIFO** (`ipc_send`/`ipc_recv`
  dans `kernel/sched.c`) : chaque tâche dispose d'une file circulaire de
  `MAILBOX_QUEUE_SIZE` (4) messages en attente (auparavant un seul message
  possible) ; le noyau copie les octets entre l'expéditeur et le
  destinataire, aucune tâche n'accède jamais directement à la mémoire d'une
  autre. Démo : la tâche ring3 `UserDemo` et le programme `echo` envoient un
  message au shell (PID 0), qui l'affiche via `shell_poll_ipc()`.
- **Système de fichiers** (`drivers/fs.c`) sur disque ATA PIO
  (`drivers/ata.c`, avec relecture/retry automatique sur erreur E/S) :
  `ls`, `cat`, `write`, `rm`, table racine persistée sur plusieurs secteurs
  (`FS_ROOT_SECTORS`), capacité de `FS_MAX_FILES` = 32 fichiers (auparavant
  15, limité à un seul secteur de répertoire). Allocation par **bitmap**
  (1 octet/secteur, secteur dédié `FS_BITMAP_LBA`) : l'espace libéré par `rm`
  est réellement récupéré et réutilisable par les écritures suivantes
  (contrairement à l'ancien "bump allocator").
- **Shell** (`kernel/shell.c`) : `help`, `clear`, `ver`, `ls`, `cat`, `write`,
  `rm`, `exec <fichier> [args...]`, `ps`, `kill <pid>`,
  `priority <pid> <niveau>`, `mem`, `uptime`, `reboot`.
  Historique de commandes (flèches haut/bas, 8 dernières commandes) et prise
  en charge de la touche Shift (majuscules/symboles) via
  `kernel/keyboard.c`.
- **Mini-libc pour l'espace utilisateur** (`userprogs/libc.h`/`libc.c`) :
  wrappers `sys_print`/`sys_exit`/`sys_sleep`/`sys_send`/`sys_recv` autour de
  `int 0x80`, helpers `u_strlen`/`u_strcmp`/`u_strcpy`, et la macro
  `MXOS_READ_ARGV(argc, argv)`. Deux programmes utilisateur l'utilisent :
  `hello_user.c` (démo) et `echo_user.c` (recompose argv et l'envoie en IPC
  au shell), tous deux embarqués dans l'image du noyau et semés
  automatiquement sur le disque virtuel (`hello`, `echo`) au premier boot.
- **Persistance de configuration** : l'historique de commandes est
  automatiquement sauvegardé (`history.dat`, un secteur par écriture, texte
  brut une commande par ligne) après chaque commande et rechargé au boot
  suivant — la navigation flèche haut fonctionne donc dès le démarrage même
  sans rien avoir tapé dans la session courante. Un message du jour (MOTD)
  peut être défini simplement via `write motd <texte>` (commande générique
  déjà existante, aucun code spécial nécessaire) et s'affiche automatiquement
  au boot s'il existe.
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

Construit le projet puis démarre l'image dans QEMU en headless sur **sept
lancements distincts** (pour limiter les risques de dérive de synchronisation
clavier sur de longues séquences, et pour tester la persistance entre deux
redémarrages), injecte des commandes shell via le moniteur QEMU, capture la
sortie sur le port série, puis vérifie par recherche de motifs que le boot,
l'espace utilisateur ring3, l'IPC, l'allocateur bitmap, l'exec concurrent, le
chargeur ELF relogeant avec argv, le clavier (Shift + historique), la
persistance de configuration (historique + MOTD entre deux boots), et enfin
le second programme utilisateur (`echo`, mini-libc + IPC), la commande
`kill` et la commande `priority` fonctionnent correctement. Code de sortie
non nul en cas d'échec (logs conservés dans `/tmp/mxos_test*_failed.log`).

## Bugs corrigés (Phase 8)

Deux bugs sérieux, préexistants et indépendants des nouvelles
fonctionnalités, ont été découverts et corrigés pendant le développement de
cette phase :

- **Débordement de la file clavier** (`kernel/keyboard.c`) : `key_queue`
  faisait 128 octets, mais les index de lecture/écriture (`queue_read`,
  `queue_write`) étaient des `uint8_t` (0–255). Au-delà de 128 touches non
  consommées (ex. rafale d'écritures disque), `queue_push()` écrivait hors
  tableau et corrompait la mémoire statique adjacente (symptôme observé :
  shell qui se corrompt puis reste bloqué en majuscules). Corrigé en
  portant `key_queue` à 256 octets (bornes exactes de l'index `uint8_t`).
- **Dépassement 64 Ko au chargement du noyau** (`boot.asm`) : la boucle de
  lecture disque en mode réel incrémentait `BX` de 512 par secteur sans
  jamais ajuster `ES`, plafonnant silencieusement à ~63-64 secteurs
  chargeables avant un retour à zéro de `BX` (16 bits) qui écrase le début
  du noyau déjà chargé. C'est pourquoi `KERNEL_SECTORS` valait exactement
  63 à l'origine : ce n'était pas un choix arbitraire mais une limite
  cachée du chargeur. Corrigé en détectant le dépassement (drapeau carry
  après `add bx, 512`) et en avançant `ES` de `0x1000` (soit +64 Ko
  physiques), ce qui permet désormais des noyaux de taille arbitraire.

## Limitations connues

- Jusqu'à 4 messages IPC en attente par tâche (`MAILBOX_QUEUE_SIZE`), au
  delà `ipc_send` échoue silencieusement (retour 0) ; non bloquant :
  `ipc_recv` renvoie immédiatement si la boîte est vide.
- Jusqu'à 6 espaces d'adressage isolés simultanés (`MAX_ADDR_SPACES` dans
  `kernel/paging.c`), tous types de tâches ring3 confondus (démo compilée
  au boot + jusqu'à 4 emplacements `exec` + marge).
- La touche Shift gère lettres, chiffres et principaux symboles (disposition
  US QWERTY simplifiée).
- L'historique de commandes conserve les 8 dernières commandes (tampon
  circulaire), persistées sur disque (`history.dat`).
- La zone argv réservée au bas de la pile de chaque tâche `exec` fait 512
  octets ; un programme utilisant plus de ~3,5 Ko de pile risquerait
  d'écraser ses propres arguments (limitation acceptée pour ce projet
  éducatif).
