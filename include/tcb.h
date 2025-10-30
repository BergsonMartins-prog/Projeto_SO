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

typedef struct TCB {
    Tarefa tarefa;
    Estado estado;
    int tempo_restante;
    int tempo_executado;
    int tempo_inicio;
    int tempo_fim;
    struct TCB *prox;//ponteiro usado pela lista
    struct TCB *prox_pronto;//ponteiro usado pela fila de prontos
    Seg *segs;
    int segs_count;
    int segs_cap;
} TCB;
TCB* tcb_criar(const Tarefa *t);
void tcb_mudar_estado(TCB *tcb, Estado novo);
void tcb_executar_tick(TCB *tcb, int tick);
void tcb_add_segment(TCB *tcb, int start, int length);
void tcb_exibir(const TCB *tcb);
void tcb_destruir(TCB *tcb);
#endif
