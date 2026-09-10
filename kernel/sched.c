#include "sched.h"
#include "cpu.h"
#include "screen.h"
#include "mem.h"
#include "pit.h"
#include "gdt.h"
#include "paging.h"

#define MAX_TASKS 16
// Taille de la pile allouée pour chaque tâche (noyau ou utilisateur).
#define TASK_STACK_SIZE 4096

static Task tasks[MAX_TASKS];
static int current_task = 0;
static int multitasking_enabled = 0;
static int task_count = 0;

// Sélecteurs de segment ring3 (doivent correspondre à kernel/gdt.c)
#define SEL_USER_CODE 0x1B // index 3, RPL 3
#define SEL_USER_DATA 0x23 // index 4, RPL 3

// Cherche la prochaine tâche à exécuter. Ne renvoie JAMAIS l'ESP d'une tâche
// inactive (ex: qui vient de se terminer et dont la pile a été libérée) :
// - Passe 1 : parmi les tâches actives ET prêtes (ni endormie, ni réveil pas
//   encore échu), on ne considère que celles ayant la PLUS HAUTE priorité
//   présente (ex: le Shell, priorité 1, passe toujours avant une tâche
//   ring3 de priorité 0), puis on choisit round-robin parmi celles-ci à
//   partir de la tâche courante (équité entre tâches de même priorité).
// - Passe 2 (repli) : n'importe quelle tâche active, même encore endormie,
//   priorité ignorée (évite un blocage total si tout dort).
static int find_next_task(void) {
    int next_task = -1;
    int best_priority = -1;

    for (int i = 1; i <= MAX_TASKS; i++) {
        int candidate = (current_task + i) % MAX_TASKS;
        if (!tasks[candidate].active) continue;

        if (tasks[candidate].sleeping) {
            if (get_ticks() >= tasks[candidate].wake_tick) {
                tasks[candidate].sleeping = 0;
            } else {
                continue;
            }
        }

        if (tasks[candidate].priority > best_priority) {
            best_priority = tasks[candidate].priority;
            next_task = candidate;
        }
    }

    if (next_task < 0) {
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].active) {
                next_task = i;
                break;
            }
        }
    }

    return next_task;
}

uint32_t schedule(uint32_t last_esp) {
    timer_tick();

    if (!multitasking_enabled) return last_esp;

    tasks[current_task].esp = last_esp;

    int next_task = find_next_task();
    if (next_task < 0) {
        // Ne devrait arriver que si plus aucune tâche n'est active du tout.
        for (;;) __asm__ volatile("cli\n\thlt");
    }

    current_task = next_task;

    // Si la prochaine tâche est interrompue depuis le ring3, le CPU aura
    // besoin d'une pile ring0 valide pour la prochaine transition -> on
    // pointe TSS.esp0 vers le haut de la pile noyau de cette tâche.
    tss_set_kernel_stack(tasks[current_task].stack_base + TASK_STACK_SIZE);

    // Bascule vers l'espace d'adressage de la tâche élue : chaque tâche
    // ring3 a son propre répertoire de pages (isolation de sa pile
    // utilisateur vis-à-vis des autres tâches) ; les tâches ring0 pures
    // partagent le répertoire noyau.
    if (tasks[current_task].addr_space >= 0) {
        paging_switch_directory(paging_task_directory_phys(tasks[current_task].addr_space));
    } else {
        paging_switch_directory(paging_kernel_directory_phys());
    }

    return tasks[current_task].esp;
}

// Termine proprement la tâche courante (cas "ring0 tombe en fin de fonction") :
// libère sa pile puis boucle en hlt. Sûr uniquement si IF=1 dans ce contexte
// (i.e. appelé directement par la tâche, pas depuis un gestionnaire d'interruption),
// puisqu'il attend d'être préempté par le prochain tick pour ne jamais être réélu.
void task_exit_current(void) {
    if (tasks[current_task].active) {
        tasks[current_task].active = 0;
        tasks[current_task].sleeping = 0;
        if (task_count > 0) task_count--;
        if (tasks[current_task].stack_base) {
            kfree((void*)tasks[current_task].stack_base);
            tasks[current_task].stack_base = 0;
        }
        if (tasks[current_task].addr_space >= 0) {
            paging_free_task_directory(tasks[current_task].addr_space);
            tasks[current_task].addr_space = -1;
        }
    }
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void task_exit() {
    task_exit_current();
}

// Variante appelable depuis un gestionnaire d'interruption (ex: syscall
// SYS_EXIT). Comme int 0x80 utilise une porte d'interruption, IF=0 durant
// son exécution : on NE PEUT PAS attendre un tick pour être repris, il faut
// resélectionner explicitement une autre tâche via schedule() et renvoyer
// son ESP, que le gestionnaire chargera avant l'iret (comme timer_handler).
uint32_t task_exit_and_reschedule(uint32_t current_esp) {
    if (tasks[current_task].active) {
        tasks[current_task].active = 0;
        tasks[current_task].sleeping = 0;
        if (task_count > 0) task_count--;
        if (tasks[current_task].stack_base) {
            kfree((void*)tasks[current_task].stack_base);
            tasks[current_task].stack_base = 0;
        }
        if (tasks[current_task].addr_space >= 0) {
            paging_free_task_directory(tasks[current_task].addr_space);
            tasks[current_task].addr_space = -1;
        }
    }

    if (task_count == 0) {
        kprint("Toutes les taches sont terminees. Arret du systeme.\n");
        for (;;) __asm__ volatile("cli\n\thlt");
    }

    return schedule(current_esp);
}

// Met la tâche courante en sommeil pendant au moins `ms` millisecondes,
// en bloquant sur `hlt` (IF doit être à 1 dans ce contexte : appel direct
// depuis une tâche, jamais depuis un gestionnaire d'interruption).
void task_sleep(uint32_t ms) {
    uint32_t freq = pit_get_frequency();
    uint32_t ticks_to_wait = (ms * freq) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    tasks[current_task].wake_tick = get_ticks() + ticks_to_wait;
    tasks[current_task].sleeping = 1;

    while (tasks[current_task].sleeping) {
        __asm__ volatile("hlt");
    }
}

// Variante appelable depuis un gestionnaire d'interruption (ex: syscall
// SYS_SLEEP) : marque la tâche endormie puis rebascule immédiatement vers
// une autre tâche via schedule(), sans jamais attendre en `hlt` avec IF=0.
uint32_t task_sleep_and_reschedule(uint32_t ms, uint32_t current_esp) {
    uint32_t freq = pit_get_frequency();
    uint32_t ticks_to_wait = (ms * freq) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    tasks[current_task].wake_tick = get_ticks() + ticks_to_wait;
    tasks[current_task].sleeping = 1;

    return schedule(current_esp);
}

// Le handler de l'horloge en assembleur pur (naked)
__attribute__((naked)) void timer_handler() {
    __asm__ volatile (
        "pushal \n"          // Sauvegarde EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
        "movl %esp, %eax \n"
        "pushl %eax \n"      // On passe l'ESP actuel à schedule()
        "call schedule \n"
        "movl %eax, %esp \n" // On récupère le nouvel ESP renvoyé par schedule()
        "movb $0x20, %al \n"
        "outb %al, $0x20 \n" // Envoi de l'EOI au PIC
        "popal \n"           // Restauration des registres de la nouvelle tâche
        "iret \n"            // Retour d'interruption
    );
}

static void mailbox_reset(int i) {
    tasks[i].mailbox_head = 0;
    tasks[i].mailbox_count = 0;
    for (int k = 0; k < MAILBOX_QUEUE_SIZE; k++) {
        tasks[i].mailbox_len[k] = 0;
        tasks[i].mailbox_sender[k] = -1;
    }
}

int create_task(void (*entry)(), char* name) {
    if (task_count >= MAX_TASKS) return -1;

    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].active) break;
    }
    if (i == MAX_TASKS) return -1;

    uint32_t stack_address = (uint32_t)kmalloc(TASK_STACK_SIZE);
    if (stack_address == 0) return -1;

    uint32_t *stack = (uint32_t*)(stack_address + TASK_STACK_SIZE);
    tasks[i].stack_base = (uint32_t)stack - TASK_STACK_SIZE;
    tasks[i].sleeping = 0;
    tasks[i].wake_tick = 0;
    
    *(--stack) = (uint32_t)task_exit;
    *(--stack) = 0x202; // EFLAGS
    *(--stack) = 0x08;  // CS
    *(--stack) = (uint32_t)entry;
    
    // Simulation du pushal initial
    for(int j = 0; j < 8; j++) *(--stack) = 0;

    tasks[i].esp = (uint32_t)stack;
    tasks[i].active = 1;
    tasks[i].name = name;
    tasks[i].addr_space = -1; // Tâche ring0 pure : partage le répertoire noyau
    mailbox_reset(i);
    tasks[i].priority = 0;
    task_count++;

    return i;
}

// Crée une tâche destinée à s'exécuter en ring3 (espace utilisateur).
// Contrairement à create_task(), la pile initiale contient une trame iret
// complète à 5 mots (SS, ESP, EFLAGS, CS, EIP) car le passage ring0->ring3
// est un changement de niveau de privilège : le CPU exige que SS:ESP soient
// également empilés pour savoir où basculer la pile utilisateur.
// `user_stack_top`/`user_stack_size` décrivent la pile dédiée de la tâche
// (mappée avec le bit U/S, cf. kernel/paging.c) : un espace d'adressage
// isolé est créé pour que SEULE cette fenêtre soit accessible en ring3,
// en plus du code/rodata/data partagé du noyau.
int create_user_task(void (*entry)(), char* name, uint32_t user_stack_top, uint32_t user_stack_size) {
    return create_user_task_ex(entry, name, user_stack_top, user_stack_size, 0, 0);
}

// Variante acceptant en plus une fenêtre de code utilisateur externe
// (`code_base`/`code_size`) : utilisée par le chargeur `exec` (kernel/exec.c)
// pour un programme chargé depuis le disque, dont le code ne fait PAS
// partie de la zone code/rodata/data partagée du noyau. Passer code_size=0
// équivaut à create_user_task (code déjà compilé dans l'image du noyau).
int create_user_task_ex(void (*entry)(), char* name, uint32_t user_stack_top, uint32_t user_stack_size,
                         uint32_t code_base, uint32_t code_size) {
    return create_user_task_argv(entry, name, user_stack_top, user_stack_size, code_base, code_size, 0, 0);
}

// Variante complète : identique à create_user_task_ex, mais permet en plus
// de préinitialiser EBX/ECX au tout premier "popal" ring3 (avant le tout
// premier octet exécuté par `entry`), utilisée par kernel/exec.c pour
// transmettre argc (EBX) / argv (ECX, pointeur vers un tableau de pointeurs
// dans la fenêtre de pile de CETTE tâche) à un programme chargé par `exec`.
int create_user_task_argv(void (*entry)(), char* name, uint32_t user_stack_top, uint32_t user_stack_size,
                           uint32_t code_base, uint32_t code_size,
                           uint32_t init_ebx, uint32_t init_ecx) {
    if (task_count >= MAX_TASKS) return -1;

    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].active) break;
    }
    if (i == MAX_TASKS) return -1;

    uint32_t stack_address = (uint32_t)kmalloc(TASK_STACK_SIZE);
    if (stack_address == 0) return -1;

    int space = paging_create_task_directory_ex(code_base, code_size,
                                                 user_stack_top - user_stack_size, user_stack_size);
    if (space < 0) {
        kfree((void*)stack_address);
        return -1;
    }

    uint32_t *stack = (uint32_t*)(stack_address + TASK_STACK_SIZE);
    tasks[i].stack_base = (uint32_t)stack - TASK_STACK_SIZE;
    tasks[i].sleeping = 0;
    tasks[i].wake_tick = 0;

    *(--stack) = SEL_USER_DATA;      // SS (pile utilisateur)
    *(--stack) = user_stack_top;     // ESP (pile utilisateur)
    *(--stack) = 0x202;              // EFLAGS (IF=1)
    *(--stack) = SEL_USER_CODE;      // CS ring3
    *(--stack) = (uint32_t)entry;    // EIP

    // Simulation du pushal initial. IMPORTANT : "*(--stack) = X" écrit
    // aux adresses DÉCROISSANTES à chaque ligne, donc la DERNIÈRE ligne
    // écrite se retrouve à l'adresse la plus basse (= tasks[i].esp final,
    // = ce que popal lira EN PREMIER, càd EDI). Il faut donc écrire dans
    // l'ordre INVERSE de "edi,esi,ebp,esp_dummy,ebx,edx,ecx,eax" (l'ordre
    // de lecture de popal), soit "eax,ecx,edx,ebx,esp_dummy,ebp,esi,edi"
    // (l'ordre d'écriture par pushal lui-même). Une version précédente de
    // ce code avait l'ordre inversé par erreur : EBX/ECX recevaient alors
    // les valeurs destinées à ESP_dummy/ESI (tous deux 0 jusqu'ici, d'où
    // un bug invisible tant que argc/argv valaient 0).
    *(--stack) = 0;         // eax
    *(--stack) = init_ecx;  // ecx (argv pour un programme lancé par exec)
    *(--stack) = 0;         // edx
    *(--stack) = init_ebx;  // ebx (argc pour un programme lancé par exec)
    *(--stack) = 0;         // esp_dummy
    *(--stack) = 0;         // ebp
    *(--stack) = 0;         // esi
    *(--stack) = 0;         // edi

    tasks[i].esp = (uint32_t)stack;
    tasks[i].active = 1;
    tasks[i].name = name;
    tasks[i].addr_space = space;
    mailbox_reset(i);
    tasks[i].priority = 0;
    task_count++;

    return i;
}


void init_multitasking() {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].active = 0;
        tasks[i].sleeping = 0;
        tasks[i].wake_tick = 0;
        tasks[i].addr_space = -1;
        tasks[i].priority = 0;
        mailbox_reset(i);
    }

    // Tâche 0 (Shell / Main) - l'état sera sauvegardé lors de la première interruption
    tasks[0].name = "Shell";
    tasks[0].active = 1;
    tasks[0].priority = 0; // Même priorité par défaut que les autres tâches : donner au
    // Shell une priorité PLUS HAUTE ici affamerait complètement toute autre
    // tâche (le Shell est quasiment toujours "prêt", donc toujours réélu en
    // premier), cf. find_next_task(). La priorité reste ajustable via
    // task_set_priority() / la commande shell "priority <pid> <niveau>",
    // à utiliser ponctuellement plutôt que comme réglage permanent du Shell.
    task_count = 1;
    current_task = 0;

    multitasking_enabled = 1;
}

// Affiche la liste des tâches actives (commande shell "ps") : nom, état
// (en cours / prête / en sommeil) et type d'espace d'adressage.
void sched_dump_tasks(void) {
    kprint("PID  NOM              ETAT       ESPACE\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].active) continue;
        kprint_dec(i);
        kprint("    ");
        kprint(tasks[i].name ? tasks[i].name : "?");
        kprint("            ");
        if (i == current_task) {
            kprint("en cours  ");
        } else if (tasks[i].sleeping) {
            kprint("sommeil   ");
        } else {
            kprint("prete     ");
        }
        if (tasks[i].addr_space >= 0) {
            kprint("isole (ring3)");
        } else {
            kprint("noyau partage");
        }
        kprint("\n");
    }
}

// Renvoie 1 si le PID donné correspond encore à une tâche active. Utilisé
// par exec.c pour refuser de lancer un nouveau programme tant que la zone
// de chargement fixe (partagée par tous les exec) est encore utilisée par
// une tâche en cours.
int task_is_active(int pid) {
    if (pid < 0 || pid >= MAX_TASKS) return 0;
    return tasks[pid].active;
}

// Renvoie le PID de la tâche actuellement élue (utilisé par ipc_send pour
// savoir qui est l'expéditeur, et par le shell pour connaître son propre PID).
int sched_current_pid(void) {
    return current_task;
}

// Termine une tâche arbitraire par PID, appelée directement depuis le
// contexte du Shell (ring0, IF=1, PAS depuis un gestionnaire d'interruption) :
// contrairement à task_exit_current()/task_exit_and_reschedule() qui ne
// terminent QUE la tâche courante, ceci permet de tuer n'importe quelle
// AUTRE tâche active (commande shell "kill <pid>") sans jamais se
// requalifier soi-même. Refuse de tuer la tâche courante (le Shell, PID 0
// en pratique) : ce cas nécessiterait la même gymnastique de rebasculement
// que task_exit_and_reschedule, non nécessaire pour ce projet éducatif.
int task_kill(int pid) {
    if (pid < 0 || pid >= MAX_TASKS || !tasks[pid].active) return 0;
    if (pid == current_task) return 0;

    tasks[pid].active = 0;
    tasks[pid].sleeping = 0;
    if (task_count > 0) task_count--;
    if (tasks[pid].stack_base) {
        kfree((void*)tasks[pid].stack_base);
        tasks[pid].stack_base = 0;
    }
    if (tasks[pid].addr_space >= 0) {
        paging_free_task_directory(tasks[pid].addr_space);
        tasks[pid].addr_space = -1;
    }
    mailbox_reset(pid);
    return 1;
}

// Ajuste la priorité d'une tâche existante (0 = normale par défaut, cf.
// find_next_task qui privilégie toujours la priorité la plus haute présente
// parmi les tâches prêtes). Sans effet si le PID est invalide/inactif.
void task_set_priority(int pid, int priority) {
    if (pid < 0 || pid >= MAX_TASKS || !tasks[pid].active) return;
    tasks[pid].priority = priority;
}

// Dépose un message dans la boîte aux lettres de `target_pid` : file
// circulaire FIFO d'au plus MAILBOX_QUEUE_SIZE messages en attente (au
// lieu d'une seule case comme auparavant). Si la file est déjà pleine,
// l'appel échoue (renvoie 0) et l'appelant est libre de réessayer plus tard.
int ipc_send(int target_pid, const void* data, uint32_t len) {
    if (target_pid < 0 || target_pid >= MAX_TASKS || !tasks[target_pid].active) return -1;
    Task* dst_task = &tasks[target_pid];
    if (dst_task->mailbox_count >= MAILBOX_QUEUE_SIZE) return 0;

    if (len > sizeof(dst_task->mailbox[0])) len = sizeof(dst_task->mailbox[0]);

    int slot = (dst_task->mailbox_head + dst_task->mailbox_count) % MAILBOX_QUEUE_SIZE;
    const uint8_t* src = (const uint8_t*)data;
    for (uint32_t k = 0; k < len; k++) {
        dst_task->mailbox[slot][k] = src[k];
    }
    dst_task->mailbox_len[slot] = len;
    dst_task->mailbox_sender[slot] = current_task;
    dst_task->mailbox_count++;
    return 1;
}

// Retire le plus ancien message en attente de la tâche courante (FIFO),
// s'il y en a un. Copie jusqu'à `sizeof(mailbox[0])` octets dans `out` ;
// renseigne la taille réelle et le PID expéditeur si les pointeurs
// correspondants sont fournis.
int ipc_recv(void* out, uint32_t* out_len, int* out_sender) {
    Task* self = &tasks[current_task];
    if (self->mailbox_count == 0) return 0;

    int slot = self->mailbox_head;
    uint32_t len = self->mailbox_len[slot];
    uint8_t* dst = (uint8_t*)out;
    for (uint32_t k = 0; k < len; k++) {
        dst[k] = (uint8_t)self->mailbox[slot][k];
    }
    if (out_len) *out_len = len;
    if (out_sender) *out_sender = self->mailbox_sender[slot];

    self->mailbox_head = (self->mailbox_head + 1) % MAILBOX_QUEUE_SIZE;
    self->mailbox_count--;
    return 1;
}

