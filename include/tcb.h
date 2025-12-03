#ifndef TCB_H
#define TCB_H

#include "tarefa.h"

typedef enum{ 
    NOVA,
    PRONTA,
    EXECUTANDO,
    BLOQUEADA,
    FINALIZADA
}Estado;

typedef struct{ 
    int start;
    int length;
 }Seg;

typedef struct {
    int start;
    int length;
} IOSeg;

typedef struct TCB {
    Tarefa tarefa;
    Estado estado;
    int tempo_restante;
    int tempo_executado;
    int tempo_inicio;
    int tempo_fim;
    struct TCB *prox;//ponteiro usado pela lista
    struct TCB *prox_pronto;//ponteiro usado pela fila de prontos
    struct TCB *mutex_next; // ponteiro usado para fila de espera de mutex
    Seg *segs;
    int segs_count;
    int segs_cap;
    int waiting_time; // ticks spent in PRONTA (used for aging)
    int io_index;     // next IO index to process from tarefa.ios
    int io_remaining; // remaining ticks for current IO (when BLOQUEADA)
    IOSeg *io_segs;
    int io_segs_count;
    int io_segs_cap;
    int io_just_started; // flag to avoid decrementing io_remaining in same tick it was started
    int mutex_index; // next mutex op to process from tarefa.mops
    int waiting_mutex_id; // mutex id the TCB is currently waiting for (or -1)
    int blocked_since; // absolute tick when blocked started (for mutex)
} TCB;
TCB* tcb_criar(const Tarefa *t);
void tcb_mudar_estado(TCB *tcb, Estado novo);
void tcb_executar_tick(TCB *tcb, int tick);
void tcb_add_segment(TCB *tcb, int start, int length);
// add IO segment to TCB (start absolute tick, length duration)
void tcb_add_io_segment(TCB *tcb, int start, int length);
void tcb_exibir(const TCB *tcb);
void tcb_free(TCB *tcb);

#endif
