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
    struct TCB *prox; //ponteiro usado pela lista
    struct TCB *prox_pronto; //ponteiro usado pela fila de prontos
    struct TCB *mutex_next; // ponteiro usado para fila de espera de mutex
    Seg *segs;
    int segs_count;
    int segs_cap;
    int waiting_time; // ticks esperando como PRONTA (usado para envelhecimento)
    int io_index;     // próximo índice de IO a processar de tarefa.ios
    int io_remaining; // ticks restantes para o IO atual (quando BLOQUEADA)
    IOSeg *io_segs;
    int io_segs_count;
    int io_segs_cap;
    int io_just_started; // flag para evitar decrementar io_remaining no mesmo tick em que foi iniciado
    int mutex_index; // próximo mutex op a processar de tarefa.mops
    int waiting_mutex_id; // id do mutex que o TCB está esperando atualmente (ou -1)
    int blocked_since; // tick absoluto quando o bloqueio começou (para mutex)
} TCB;
TCB* tcb_criar(const Tarefa *t);
void tcb_mudar_estado(TCB *tcb, Estado novo);
void tcb_executar_tick(TCB *tcb, int tick);
void tcb_add_segment(TCB *tcb, int start, int length); // adiciona segmento de IO ao TCB (tick absoluto de início, duração)
void tcb_add_io_segment(TCB *tcb, int start, int length);
void tcb_exibir(const TCB *tcb);
void tcb_free(TCB *tcb);

#endif
