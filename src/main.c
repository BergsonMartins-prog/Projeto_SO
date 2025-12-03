#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>
#include "parser.h"
#include "tcb.h"
#include "scheduler.h"
#include "ui.h"
#include "tarefa.h"

// Simula do estado inicial (arquivo de configuração) até `ticks_completed` ticks.
// Retorna uma lista de TCBs (alocada) representando o estado após `ticks_completed`.
// Caller must free the returned TCBs with tcb_free.
// Simulate up to `ticks_completed` ticks (or until all tasks finish).
// Returns a freshly-allocated list of TCBs representing state after simulation.
// If `out_remaining` is non-NULL, it is set to the number of tasks still not finalized after the simulation.
static TCB* simulate_to_ticks(const Config *cfg, int ticks_completed, int max_sim_ticks, int *out_remaining) {
    TCB *head = NULL, *tail = NULL;
    for (int i = 0; i < cfg->n_tarefas; ++i) {
        TCB *tcb = tcb_criar(&cfg->tarefas[i]);
        if (!head) head = tail = tcb; else { tail->prox = tcb; tail = tcb; }
    }

    scheduler_init(cfg->algoritmo, cfg->quantum, cfg->alpha);

    int remaining = cfg->n_tarefas;
    for (int tick = 0; tick < ticks_completed && tick < max_sim_ticks && remaining > 0; ++tick) {
        for (TCB *p = head; p != NULL; p = p->prox) {
            if (p->estado == NOVA && p->tarefa.ingresso == tick) {
                scheduler_adicionar(p);
                p->estado = PRONTA;
            }
        }

        scheduler_set_current_tick(tick);
        TCB *sel = scheduler_escolher_proxima();
        if (sel) {
            sel->estado = EXECUTANDO;
            sel->waiting_time = 0; // reset waiting time while executing
            tcb_executar_tick(sel, tick);
                if (sel->estado == FINALIZADA) {
                    scheduler_marcar_finalizado(sel);
                    remaining--;
                } else {
                    if (strcasecmp(cfg->algoritmo, "SRTF") == 0
                        || strcasecmp(cfg->algoritmo, "PRIORIDADE") == 0
                        || strcasecmp(cfg->algoritmo, "PRIORITY") == 0
                        || strcasecmp(cfg->algoritmo, "PRIOPEnv") == 0
                        || strcasecmp(cfg->algoritmo, "PRIOPENV") == 0) {
                        scheduler_yield_current();
                    }
                }
        }

        // After selection/execution and any yielding, increment waiting_time for tasks that are PRONTA
        for (TCB *q = head; q != NULL; q = q->prox) {
            if (q->estado == PRONTA) q->waiting_time += 1;
        }
    }
    if (out_remaining) *out_remaining = remaining;
    return head;
}

// Compute the total number of ticks necessary to display the Gantt chart
// by scanning segments and tempo_fim values. Returns a non-negative tick count.
static int compute_total_ticks(TCB *head) {
    int max_tick = 0;
    for (TCB *p = head; p != NULL; p = p->prox) {
        if (p->tempo_fim > max_tick) max_tick = p->tempo_fim;
        for (int s = 0; s < p->segs_count; ++s) {
            int end = p->segs[s].start + p->segs[s].length;
            if (end > max_tick) max_tick = end;
        }
        // also consider ingresso in case task never ran
        if (p->tarefa.ingresso > max_tick) max_tick = p->tarefa.ingresso;
    }
    return max_tick;
}

int main(int argc, char **argv){
    if(argc < 4) {
        printf("Uso: %s <config.txt> <modo: passo|auto> <saida.svg>\n", argv[0]);
        printf("Exemplo: %s examples/exemplo_config.txt passo saida.svg\n", argv[0]);
        return 1;
    }
    char *config_path = argv[1];
    char *modo = argv[2];
    char *svg_out = argv[3];
    int modo_passo = (strcasecmp(modo, "passo") == 0);

    //ler arquivo de configuração
    Config cfg = {0};
    if(parser_ler_arquivo(config_path, &cfg) != 0) {
        printf("Erro ao ler arquivo de configuração\n");
        return 2;
    }
    // Debug: mostrar algoritmo/quantum lidos
    printf("[Config] algoritmo='%s' quantum=%d n_tarefas=%d\n", cfg.algoritmo, cfg.quantum, cfg.n_tarefas);

    int max_sim_ticks = 1000000; // evitar loops infinitos

    // If not passo mode: run full simulation (existing behavior)
    if (!modo_passo) {
        int rem = 0;
        TCB *all_head = simulate_to_ticks(&cfg, max_sim_ticks, max_sim_ticks, &rem); // simulate until completion or max
        // rebuild and run to termination
    for (TCB *p = all_head; p != NULL; p = p->prox) { tcb_free(p); }
    all_head = NULL;
    // run properly until finish
    all_head = simulate_to_ticks(&cfg, max_sim_ticks, max_sim_ticks, &rem);
    // compute a sensible total_ticks from the simulated state
    int total_ticks = compute_total_ticks(all_head);
    // For compatibility with previous behavior, save a Gantt using the detected total_ticks
    ui_salvar_gantt_svg(all_head, total_ticks, svg_out, cfg.algoritmo, cfg.alpha);

        for (TCB *p = all_head; p != NULL; ) { TCB *nx = p->prox; tcb_free(p); p = nx; }
        parser_liberar(&cfg);
        printf("Simulação finalizada (não-interativa).\n");
        return 0;
    }

    // --- Modo passo-a-passo reversível ---
    // Setup terminal for single-key reads (non-canonical)
    struct termios orig_term, raw_term;
    tcgetattr(STDIN_FILENO, &orig_term);
    raw_term = orig_term;
    raw_term.c_lflag &= ~(ICANON | ECHO);
    raw_term.c_cc[VMIN] = 1;
    raw_term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term);

    int current_tick = 0;
    int view_remaining = 0;
    TCB *view_head = simulate_to_ticks(&cfg, current_tick, max_sim_ticks, &view_remaining);

    printf("Modo passo-a-passo reversível: ← retroceder, → avançar, q sair\n");

    while (1) {
        printf("=== Estado no tick %d ===\n", current_tick);
        for (TCB *p = view_head; p != NULL; p = p->prox) {
            tcb_exibir(p);
        }
        // read key
        char buf[3];
        ssize_t n = read(STDIN_FILENO, buf, 1);
        if (n <= 0) break;
        if (buf[0] == '\n' || buf[0] == '\r') {
            // ENTER == advance one
            current_tick++;
        } else if (buf[0] == 'q' || buf[0] == 'Q') {
            break;
        } else if (buf[0] == 0x1b) {
            // possible arrow sequence, read two more bytes
            if (read(STDIN_FILENO, buf+1, 2) == 2) {
                if (buf[1] == '[' && buf[2] == 'C') { // right
                    current_tick++;
                } else if (buf[1] == '[' && buf[2] == 'D') { // left
                    if (current_tick > 0) current_tick--;
                }
            }
        } else if (buf[0] == 'a' || buf[0] == 'A') {
            // optional: run to completion
            // set current_tick to a large number to let simulation run until remaining==0
            current_tick = max_sim_ticks;
        }

        if (current_tick < 0) current_tick = 0;
        // rebuild view_head for the selected tick
        for (TCB *p = view_head; p != NULL; ) { TCB *nx = p->prox; tcb_free(p); p = nx; }
        view_head = simulate_to_ticks(&cfg, current_tick, max_sim_ticks, &view_remaining);

        // Save Gantt for this step (overwrite output every step)
        if (ui_salvar_gantt_svg(view_head, current_tick, svg_out, cfg.algoritmo, cfg.alpha) != 0) {
            fprintf(stderr, "Falha ao salvar SVG em %s (tick %d)\n", svg_out, current_tick);
        }

        // If no tasks remain, exit interactive mode automatically
        if (view_remaining == 0) {
            printf("Todas as tarefas finalizaram no tick %d. Saindo...\n", current_tick);
            break;
        }
    }

    // restore terminal
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);

    int total_ticks = current_tick;
    ui_salvar_gantt_svg(view_head, total_ticks, svg_out, cfg.algoritmo, cfg.alpha);

    for (TCB *p = view_head; p != NULL; ) { TCB *nx = p->prox; tcb_free(p); p = nx; }
    parser_liberar(&cfg);

    printf("Simulação finalizada em %d ticks\n", total_ticks);
    return 0;
}
