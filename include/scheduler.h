#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "tcb.h"

void scheduler_init(const char *algoritmo, int quantum, int alpha);
void scheduler_adicionar(TCB *tcb);
void scheduler_remover(TCB *tcb);
TCB* scheduler_escolher_proxima(void);
TCB* scheduler_get_executando(void);
void scheduler_marcar_finalizado(TCB *tcb);
void scheduler_yield_current(void);
// Bloqueia a tarefa atualmente em execução (por exemplo, para IO).
// A tarefa ficará com estado BLOQUEADA e não será colocada na fila de prontos.
void scheduler_block_current(void);
// Notify scheduler that a tick was executed by the current task.
// Returns 1 if the scheduler considers the quantum expired and the current should yield.
int scheduler_tick_executed(void);
// Set current simulation tick (used by aging calculation)
void scheduler_set_current_tick(int tick);

#endif
