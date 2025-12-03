#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

#define UNIT_W 20
#define ROW_H 30
#define MARGIN 20

// Cabeçalho SVG.
static void svg_header(FILE *f, int width, int height) {
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" ");
    fprintf(f, "width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n", width, height, width, height);
}

static void svg_footer(FILE *f) {
    fprintf(f, "</svg>\n");
}

// Texto SVG. 
static void svg_text(FILE *f, int x, int y, const char *text) {
    fprintf(f, "<text x=\"%d\" y=\"%d\" font-family=\"Arial\" font-size=\"12\">%s</text>\n", x, y, text);
}

// Gera e salva o Gantt em SVG. 
int ui_salvar_gantt_svg(TCB *tcbs_head, int total_ticks, const char *filename, const char *algoritmo, int alpha) {
    if(!filename) return -1;

    // Conta quantas tarefas (linhas) serão desenhadas 
    int n = 0;
    for(TCB *p = tcbs_head; p != NULL; p = p->prox) n++;
    if(n == 0) {
        fprintf(stderr, "[UI] sem tarefas para desenhar.\n");
        return -1;
    }

    int width = MARGIN*2 + total_ticks * UNIT_W;
    int height = MARGIN*2 + n * ROW_H + 50;
    FILE *f = fopen(filename, "w");
    if(!f) { perror("fopen svg"); return -1; }

    svg_header(f, width, height);

    // Marcações de tempo (linhas verticais e labels a cada 5 ticks) 
    for(int t = 0; t <= total_ticks; ++t) {
        int x = MARGIN + t * UNIT_W;
        // Marca em cada tick (1 em 1) 
        fprintf(f, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#ddd\" stroke-width=\"1\" />\n",
                x, MARGIN, x, MARGIN + n*ROW_H);
        char num[16]; snprintf(num, sizeof(num), "%d", t);
        svg_text(f, x+2, MARGIN-5, num);
    }

    // Desenha cada linha de tarefa e seus segmentos 
    int idx = 0;
    for(TCB *p = tcbs_head; p != NULL; p = p->prox) {
        int y = MARGIN + idx*ROW_H;
    char label[64];
    // id já inclui prefixo (ex: t01)
    snprintf(label, sizeof(label), "%s", p->tarefa.id);
        // bar vertical position and inner height
        int bar_y = y + 2;
        int bar_h = ROW_H - 10;
        int label_ty = bar_y + bar_h/2 + 5;
    // center label within left margin area
    fprintf(f, "<text x=\"%d\" y=\"%d\" font-family=\"Arial\" font-size=\"12\" text-anchor=\"middle\">%s</text>\n",
        MARGIN/2, label_ty, label);

        // Fundo da linha (grade) 
        fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#f8f8f8\" stroke=\"#eee\" />\n",
                MARGIN, y, total_ticks*UNIT_W, ROW_H-6);

    // Desenha retângulo vazio representando tempo de espera (ingresso -> tempo_inicio)
    // se houver espera antes da primeira execução. 
    int wait_start = p->tarefa.ingresso;
    int wait_end = (p->tempo_inicio >= 0) ? p->tempo_inicio : total_ticks;
    int wait_len = wait_end - wait_start;
    if(wait_len > 0) {
        int wx = MARGIN + wait_start * UNIT_W;
        int ww = wait_len * UNIT_W;
        // barra de espera inicial: cinza claro com mesmo contorno das execuções
        fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#e8e8e8\" stroke=\"#333\" stroke-width=\"1\" />\n",
            wx, bar_y, ww, bar_h);
    }

                // Desenha segmentos de execução e lacunas de espera entre eles
                int prev_end = p->tarefa.ingresso;
                for(int s = 0; s < p->segs_count; ++s) {
                    int seg_start = p->segs[s].start;
                    int seg_len = p->segs[s].length;
                    // draw waiting gap between prev_end and seg_start (if any)
                    int gap = seg_start - prev_end;
                    if (gap > 0) {
                        int gx = MARGIN + prev_end * UNIT_W;
                        int gw = gap * UNIT_W;
                        fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#e8e8e8\" stroke=\"#333\" stroke-width=\"1\" />\n",
                                gx, bar_y, gw, bar_h);
                    }

                    int sx = MARGIN + seg_start * UNIT_W;
                    int sw = seg_len * UNIT_W;
                    const char *col = p->tarefa.cor;
                    if(!col || strlen(col) == 0) col = "#88c";
                    fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"%s\" stroke=\"#333\" stroke-width=\"1\" />\n",
                            sx, bar_y, sw, bar_h, col);
                    prev_end = seg_start + seg_len;
                }

                // If using PRIOPEnv, print priority value for each tick (waiting or executing)
                if (algoritmo && (strcasecmp(algoritmo, "PRIOPEnv") == 0 || strcasecmp(algoritmo, "PRIOPENV") == 0)) {
                    int waiting_count = 0;
                        // Use recorded IO segments from TCB (if any) to treat IO ticks specially
                        int n_intvs = p->io_segs_count;
                        // iterate ticks from 0..total_ticks-1 and determine state at each tick
                        int prev_state = -1; // 0=PRONTA,1=EXECUTANDO,2=BLOQUEADA
                        for (int t = 0; t < total_ticks; ++t) {
                            // only consider ticks at or after ingresso
                            if (t < p->tarefa.ingresso) continue;
                            // if task finished before this tick, stop printing
                            if (p->tempo_fim >= 0 && t >= p->tempo_fim) break;

                            // determine if in_exec
                            int in_exec = 0;
                            for (int s = 0; s < p->segs_count; ++s) {
                                int st = p->segs[s].start;
                                int en = st + p->segs[s].length; // exclusive
                                if (t >= st && t < en) { in_exec = 1; break; }
                            }
                            // determine if in_io using recorded io_segs
                            int in_io = 0;
                            for (int ii = 0; ii < n_intvs; ++ii) {
                                int ist = p->io_segs[ii].start;
                                int ien = ist + p->io_segs[ii].length;
                                if (t >= ist && t < ien) { in_io = 1; break; }
                            }

                            int state;
                            if (in_exec) state = 1; // EXECUTANDO
                            else if (in_io) state = 2; // BLOQUEADA (IO)
                            else state = 0; // PRONTA (waiting)

                            char buf[32];
                            int tx = MARGIN + t * UNIT_W + UNIT_W/2; // center of unit
                            int ty = bar_y + bar_h/2 + 5; // vertical center of inner bar

                            if (state == 1) {
                                // executing: show base priority, reset waiting_count
                                snprintf(buf, sizeof(buf), "%d", p->tarefa.prioridade);
                                waiting_count = 0;
                            } else if (state == 2) {
                                // IO: show base priority and do not increment waiting_count
                                snprintf(buf, sizeof(buf), "%d", p->tarefa.prioridade);
                            } else { // PRONTA
                                // if we just transitioned into PRONTA from non-PRONTA, reset waiting_count
                                if (prev_state != 0) waiting_count = 0;
                                // increment first so aging is immediate on first waiting tick
                                waiting_count += 1;
                                long eff = (long)p->tarefa.prioridade + (long)alpha * waiting_count;
                                snprintf(buf, sizeof(buf), "%ld", eff);
                            }
                            // center text horizontally using text-anchor="middle"
                            fprintf(f, "<text x=\"%d\" y=\"%d\" font-family=\"Arial\" font-size=\"12\" text-anchor=\"middle\">%s</text>\n",
                                    tx, ty, buf);

                            prev_state = state;
                        }
                }
        idx++;
    }

    svg_text(f, MARGIN, MARGIN + n*ROW_H + 20, "Legenda: retângulos coloridos = execução da tarefa");
    svg_footer(f);
    fclose(f);
    printf("[UI] Gantt salvo em: %s\n", filename);
    return 0;
}
