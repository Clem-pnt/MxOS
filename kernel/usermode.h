#ifndef USERMODE_H
#define USERMODE_H

// Point d'entrée de la tâche de démonstration exécutée en ring3.
void user_task_demo(void);

// Renvoie le sommet de la pile utilisateur dédiée à cette démo (à passer à
// create_user_task()).
unsigned int user_demo_stack_top(void);
unsigned int user_demo_stack_size(void);

#endif
