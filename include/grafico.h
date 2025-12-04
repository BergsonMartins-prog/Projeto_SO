#ifndef GRAFICO_H
#define GRAFICO_H
#include "tcb.h"

int grafico_salvar_gantt_svg(TCB *tcbs_head, int total_ticks, const char *filename, const char *algoritmo, int alpha);

#endif
