#ifndef PARSER_H
#define PARSER_H

#include "tarefa.h"

typedef struct {
    char algoritmo[32];
    int quantum;
    Tarefa *tarefas;
    int n_tarefas;
} Config;
int parser_ler_arquivo(const char *path, Config *cfg);
void parser_liberar(Config *cfg);

#endif
