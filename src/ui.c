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
int ui_salvar_gantt_svg(TCB *tcbs_head, int total_ticks, const char *filename) {
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
        svg_text(f, 2, y + ROW_H/2 + 5, label);

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
        // branco com borda leve para indicar espera 
        fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#ffffff\" stroke=\"#bbb\" stroke-width=\"1\" />\n",
            wx, y, ww, ROW_H-10);
    }

        // Desenha segmentos de execução 
        for(int s = 0; s < p->segs_count; ++s) {
            int sx = MARGIN + p->segs[s].start * UNIT_W;
            int sw = p->segs[s].length * UNIT_W;
            const char *col = p->tarefa.cor;
            if(!col || strlen(col) == 0) col = "#88c";
            fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"%s\" stroke=\"#333\" stroke-width=\"1\" />\n",
                    sx, y, sw, ROW_H-10, col);
        }
        idx++;
    }

    svg_text(f, MARGIN, MARGIN + n*ROW_H + 20, "Legenda: retângulos coloridos = execução da tarefa");
    svg_footer(f);
    fclose(f);
    printf("[UI] Gantt salvo em: %s\n", filename);
    return 0;
}
