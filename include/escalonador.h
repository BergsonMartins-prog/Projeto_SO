#ifndef ESCALONADOR_H
#define ESCALONADOR_H

#include "tcb.h"

void escalonador_init(const char *algoritmo, int quantum, int alpha);
void escalonador_adicionar(TCB *tcb);
void escalonador_remover(TCB *tcb);
TCB* escalonador_escolher_proxima(void);
TCB* escalonador_get_executando(void);
void escalonador_marcar_finalizado(TCB *tcb);
void escalonador_prepara_atual(void);

// Bloqueia a tarefa atualmente em execução (para IO e mutex).
// A tarefa ficará com estado BLOQUEADA e não será colocada na fila de prontos.
void escalonador_bloqueia_atual(void);

// Notifica o escalonador que um tick foi executado pela tarefa atual.
// Retorna 1 se o escalonador considerar que o quantum expirou e a tarefa atual deve ceder a vez.
int escalonador_tick_executado(void);

// Define o tick atual da simulação (usado para cálculo de envelhecimento)
void escalonador_set_tick_atual(int tick);

#endif
