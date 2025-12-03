#ifndef TAREFA_H
#define TAREFA_H

// Representa uma operação de IO relativa ao início da tarefa
typedef struct {
    int instante; // instante (offset) relativo ao ingresso da tarefa
    int duracao;  // tempo que a operação demanda
} IOOp;

// Representa uma operação de mutex: ML (lock) or MU (unlock)
typedef struct {
    char kind; // 'L' for lock (ML), 'U' for unlock (MU)
    int id;    // mutex id
    int instante; // instante relativo ao inicio da tarefa
} MutexOp;

// Tarefa com lista opcional de IOs
typedef struct {
    char id[16];
    char cor[32];
    int ingresso;
    int duracao;
    int prioridade;
    IOOp *ios;    // ponteiro para array de IOs (pode ser NULL)
    int n_ios;    // número de IOs no array
    MutexOp *mops; // ponteiro para array de mutex ops
    int n_mops;    // número of mutex ops
} Tarefa;

#endif
