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
// Set current simulation tick (used by aging calculation)
void scheduler_set_current_tick(int tick);

#endif
