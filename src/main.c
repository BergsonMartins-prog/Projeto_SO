#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "parser.h"
#include "tcb.h"
#include "scheduler.h"
#include "ui.h"
#include "tarefa.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Uso: %s <config.txt> <modo: passo|auto> <saida.svg>\n", argv[0]);
        printf("Exemplo: %s examples/exemplo_config.txt passo saida.svg\n", argv[0]);
        return 1;
    }
    const char *config_path = argv[1];
    const char *modo = argv[2];
    const char *svg_out = argv[3];
    int modo_passo = (strcasecmp(modo, "passo") == 0);

    //ler arquivo de configuração
    Config cfg = {0};
    if (parser_ler_arquivo(config_path, &cfg) != 0) {
        printf("Erro ao ler arquivo de configuração\n");
        return 2;
    }
    // Debug: mostrar algoritmo/quantum lidos
    printf("[Config] algoritmo='%s' quantum=%d n_tarefas=%d\n", cfg.algoritmo, cfg.quantum, cfg.n_tarefas);

    // Criar TCBs a partir das tarefas lidas (lista encadeada all_head)
    TCB *all_head = NULL;
    TCB *all_tail = NULL;
    for (int i = 0; i < cfg.n_tarefas; ++i) {
        TCB *tcb = tcb_criar(&cfg.tarefas[i]);
        if (!all_head) all_head = all_tail = tcb;
        else { all_tail->prox = tcb; all_tail = tcb; }
    }

    // Inicializa o scheduler
    scheduler_init(cfg.algoritmo, cfg.quantum);

    // Loop principal de simulação:
    int tick = 0;
    int remaining = cfg.n_tarefas;
    int max_sim_ticks = 1000000; // evitar loops infinitos

    // Checagem de chegada: para cada TCB NOVA cujo ingresso == tick, adicionamos à fila de prontos
    while (remaining > 0 && tick < max_sim_ticks) {
        for (TCB *p = all_head; p != NULL; p = p->prox) {
                if (p->estado == NOVA && p->tarefa.ingresso == tick) {
                scheduler_adicionar(p);
                p->estado = PRONTA;
                printf("[tick %d] Tarefa %s chegou e foi para PRONTA\n", tick, p->tarefa.id);
            }
        }

        /* Escolher próxima tarefa pelo scheduler (decisão) */
        TCB *sel = scheduler_escolher_proxima();

        if (sel) {
            /* executar 1 tick na tarefa selecionada (tcb_executar_tick atualiza segmentos e tempos) */
            sel->estado = EXECUTANDO; /* garantia local, o scheduler já fez isso */
            tcb_executar_tick(sel, tick);
            printf("[tick %d] Executando %s (rem=%d)\n", tick, sel->tarefa.id, sel->tempo_restante);
                if (sel->estado == FINALIZADA) {
                scheduler_marcar_finalizado(sel);
                remaining--;
                printf("[tick %d] %s FINALIZADA\n", tick, sel->tarefa.id);
            } 
            else {
                /*
                 * Se não terminou, decidimos se devolvemos a tarefa para a fila de prontos.
                 * - Para algoritmos preemptivos (SRTF, PRIORIDADE) devolvemos para que o scheduler
                 *   possa reconsiderá-la no próximo tick.
                 * - Para FIFO (não-preemptivo) não devolvemos, permitindo que a mesma tarefa
                 *   permaneça como 'executando' até terminar.
                 *
                 * Observação: esta decisão poderia ser encapsulada no scheduler (por exemplo,
                 * um flag que indica se o algoritmo é preemptivo). Aqui optamos por checar
                 * a string do algoritmo para simplicidade didática.
                 */
                if (strcasecmp(cfg.algoritmo, "SRTF") == 0
                 || strcasecmp(cfg.algoritmo, "PRIORIDADE") == 0
                 || strcasecmp(cfg.algoritmo, "PRIORITY") == 0) {
                    scheduler_yield_current();
                }
            }
        } 
        else {
            /* CPU ociosa neste tick */
            printf("[tick %d] CPU ociosa\n", tick);
        }

        /* Modo passo-a-passo: exibe estado e aguarda ENTER para avançar 1 tick */
        if (modo_passo) {
            printf("=== Estado no tick %d ===\n", tick);
            for (TCB *p = all_head; p != NULL; p = p->prox) {
                tcb_exibir(p);
            }
            printf("Pressione ENTER para avançar...\n");
            getchar();
        }

        /* Avança o relógio (tick) */
        tick++;
    }

    /* Ao final, gera o SVG com o gráfico de Gantt */
    int total_ticks = tick;
    ui_salvar_gantt_svg(all_head, total_ticks, svg_out);

    /* Cleanup: libera TCBs e memória do parser */
    for (TCB *p = all_head; p != NULL; ) {
        TCB *nx = p->prox;
        tcb_destruir(p);
        p = nx;
    }
    parser_liberar(&cfg);

    printf("Simulação finalizada em %d ticks\n", total_ticks);
    return 0;
}
