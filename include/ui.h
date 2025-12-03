#ifndef UI_H
#define UI_H
#include "tcb.h"

// If algoritmo is "PRIOPEnv" (case-insensitive) and alpha>0, the UI will
// render the effective priority for each task at each tick inside its
// execution rectangles so the effect of aging can be seen.
int ui_salvar_gantt_svg(TCB *tcbs_head, int total_ticks, const char *filename, const char *algoritmo, int alpha);

#endif
