#ifndef TAREFA_H
#define TAREFA_H

typedef struct {
    char id[16];
    char cor[32];
    int ingresso;
    int duracao;
    int prioridade;
} Tarefa;

#endif
