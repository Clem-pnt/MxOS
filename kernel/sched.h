#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

typedef struct {
    uint32_t esp;        // Pointeur de pile (sauvegarde l'état du CPU)
    uint32_t stack_base; 
    int active;
    char* name;
    int sleeping;
    uint32_t wake_tick;
    // Index de l'espace d'adressage isolé (cf. kernel/paging.c), ou -1 si
    // la tâche partage le répertoire de pages noyau (tâches ring0 pures).
    int addr_space;
    // Boîte aux lettres IPC (une seule case en attente à la fois) : le
    // noyau copie les données entre tâches, aucune n'accède jamais
    // directement à la mémoire d'une autre (cf. ipc_send/ipc_recv).
    char mailbox[128];
    uint32_t mailbox_len;
    int mailbox_has_msg;
    int mailbox_sender;
} Task;

/* --- PROTOTYPES --- */
void init_multitasking();
int create_task(void (*entry)(), char* name);
int create_user_task(void (*entry)(), char* name, uint32_t user_stack_top, uint32_t user_stack_size);
int create_user_task_ex(void (*entry)(), char* name, uint32_t user_stack_top, uint32_t user_stack_size,
                         uint32_t code_base, uint32_t code_size);
int create_user_task_argv(void (*entry)(), char* name, uint32_t user_stack_top, uint32_t user_stack_size,
                           uint32_t code_base, uint32_t code_size,
                           uint32_t init_ebx, uint32_t init_ecx);
uint32_t schedule(uint32_t last_esp);
void timer_handler(); // Le handler naked
void task_sleep(uint32_t ms);
void task_exit_current(void); // Appelable directement (contexte tâche, IF=1)
uint32_t task_exit_and_reschedule(uint32_t current_esp); // Appelable depuis un gestionnaire d'interruption (ex: syscall)
uint32_t task_sleep_and_reschedule(uint32_t ms, uint32_t current_esp); // Idem, pour SYS_SLEEP
void sched_dump_tasks(void); // Utilisé par la commande shell "ps"
int task_is_active(int pid); // Utilisé par exec.c pour éviter d'écraser un programme en cours
int sched_current_pid(void); // PID de la tâche actuellement élue

// IPC basique : dépose/retire un message dans la boîte aux lettres d'une
// tâche. `data`/`out` pointent dans la mémoire de l'appelant (ring0 ou
// ring3), toujours valides depuis le noyau car le code s'exécute en CPL 0.
// ipc_send: renvoie 1 si envoyé, 0 si boîte du destinataire déjà pleine,
// -1 si le PID cible est invalide/inactif.
int ipc_send(int target_pid, const void* data, uint32_t len);
// ipc_recv: renvoie 1 si un message a été récupéré (remplit *out_len et
// *out_sender s'ils sont non NULL), 0 si la boîte de l'appelant est vide.
int ipc_recv(void* out, uint32_t* out_len, int* out_sender);

#endif