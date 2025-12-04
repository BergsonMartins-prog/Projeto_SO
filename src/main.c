#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>
#include "parser.h"
#include "tcb.h"
#include "escalonador.h"
#include "grafico.h"
#include "tarefa.h"

// Estruturas de dados de Mutex
typedef struct MutexNode {
    int id;
    TCB *owner;
    TCB *wait_head;
    TCB *wait_tail;
    struct MutexNode *next;
} MutexNode;

static MutexNode* mutex_find(MutexNode *root, int id) {
    MutexNode *p = root;
    while(p) {
        if (p->id == id)
            return p; 
        p = p->next;
    }
    return NULL;
}

static MutexNode* mutex_ensure(MutexNode **root, int id) {
    MutexNode *n = mutex_find(*root, id);
    if (n) return n;
    n = (MutexNode*) malloc(sizeof(MutexNode));
    if (!n) { 
        perror("malloc"); 
        exit(EXIT_FAILURE); 
    }
    n->id = id; 
    n->owner = NULL; 
    n->wait_head = n->wait_tail = NULL; 
    n->next = NULL;
    if (!*root) 
        *root = n; 
    else {
        MutexNode *q = *root; 
        while(q->next) 
            q = q->next; 
        q->next = n;
    }
    return n;
}

static int mutex_try_acquire(MutexNode **root, int id, TCB *req) {
    MutexNode *n = mutex_ensure(root, id);
    if (n->owner == NULL) { 
        n->owner = req; 
        return 1; 
    }
    return 0;
}

// coloca um waiter na fila
static void mutex_enqueue_waiter(MutexNode **root, int id, TCB *waiter) {
    if (!waiter) 
        return;
    MutexNode *n = mutex_ensure(root, id);
    // evita enfileirar duas vezes o mesmo waiter para o mesmo mutex
    if (waiter->waiting_mutex_id == id && waiter->mutex_next != NULL) 
        return;
    waiter->mutex_next = NULL;
    if (!n->wait_head) { 
        n->wait_head = n->wait_tail = waiter; 
    }
    else { 
        n->wait_tail->mutex_next = waiter; 
        n->wait_tail = waiter; 
    }
}

// desenfileira o próximo waiter (FIFO)
static TCB* mutex_dequeue_waiter(MutexNode **root, int id) {
    MutexNode *n = mutex_find(*root, id);
    if (!n || !n->wait_head) 
        return NULL;
    TCB *w = n->wait_head;
    n->wait_head = w->mutex_next;
    if (!n->wait_head) 
        n->wait_tail = NULL;
    w->mutex_next = NULL;
    return w;
}

// libera o mutex e acorda um TCB esperando. Retorna o TCB acordado ou NULL.
static TCB* mutex_release_and_wakeup(MutexNode **root, int id, int tick) {
    MutexNode *n = mutex_find(*root, id);
    if (!n) 
        return NULL;
    n->owner = NULL;
    TCB *best = mutex_dequeue_waiter(root, id);
    if (best) {
        // transfere ownership
        n->owner = best;
        // registra intervalo bloqueado (duração)
        if (best->blocked_since >= 0) {
            int dur = (tick + 1) - best->blocked_since;
            if (dur > 0) 
                tcb_add_io_segment(best, best->blocked_since, dur);
        }
        // consome a operação ML pendente para a tarefa acordada
        if (best->mutex_index < best->tarefa.n_mops) {
            MutexOp mo = best->tarefa.mops[best->mutex_index];
            if (mo.kind == 'L' && mo.id == id) {
                best->mutex_index += 1;
                printf("[MUTEX] tarefa %s: ML%d concedido no tick %d (acordar)\n", best->tarefa.id, id, tick);
            }
        }
        best->waiting_mutex_id = -1;
        best->blocked_since = -1;
        best->estado = PRONTA;
        best->waiting_time = 0;
        // adiciona à fila de prontas
        escalonador_adicionar(best);
        return best;
    }
    return NULL;
}
// Simula do estado inicial (arquivo de configuração) até `ticks_completed` ticks.
static TCB* simulate_to_ticks(const Config *cfg, int ticks_completed, int max_sim_ticks, int *out_remaining) {
    TCB *head = NULL, *tail = NULL;
    MutexNode *mutex_head = NULL;
    for (int i = 0; i < cfg->n_tarefas; ++i) {
        TCB *tcb = tcb_criar(&cfg->tarefas[i]);
        if (!head) 
            head = tail = tcb; 
        else { 
            tail->prox = tcb; 
            tail = tcb; 
        }
    }

    escalonador_init(cfg->algoritmo, cfg->quantum, cfg->alpha);

    int remaining = cfg->n_tarefas;
    for (int tick = 0; tick < ticks_completed && tick < max_sim_ticks && remaining > 0; ++tick) {
        for (TCB *p = head; p != NULL; p = p->prox) {
            if (p->estado == NOVA && p->tarefa.ingresso == tick) {
                // Na chegada, verifica operações de mutex ou IO no instante 0
                int handled = 0;
                if (p->tarefa.n_mops > 0 && p->tarefa.mops[0].instante == 0) {
                    MutexOp mo = p->tarefa.mops[0];
                    if (mo.kind == 'L') {
                        // tenta adquirir
                        if (mutex_try_acquire(&mutex_head, mo.id, p)) {
                            // adquirido
                            handled = 0;
                        } else {
                            // não conseguiu adquirir: bloqueia imediatamente e enfileira na lista de espera do mutex
                            p->estado = BLOQUEADA;
                            p->waiting_mutex_id = mo.id;
                            p->blocked_since = tick + 1;
                            p->waiting_time = 0;
                            // adiciona à fila de espera do mutex
                            mutex_enqueue_waiter(&mutex_head, mo.id, p);
                            printf("[MUTEX] tarefa %s: bloqueada ML%d no tick %d (chegada)\n", p->tarefa.id, mo.id, tick);
                            handled = 1;
                        }
                    } 
                    else if (mo.kind == 'U') {
                        // desbloqueia: libera e acorda uma tarefa
                        mutex_release_and_wakeup(&mutex_head, mo.id, tick);
                        handled = 0;
                    }
                }
                if (!handled) {
                    // Se a tarefa tem um IO no instante 0, inicia bloqueada imediatamente
                    if (p->tarefa.n_ios > 0 && p->tarefa.ios[0].instante == 0) {
                        p->estado = BLOQUEADA;
                        p->io_index = 1;
                        p->io_remaining = p->tarefa.ios[0].duracao;
                        p->waiting_time = 0; // previne envelhecimento enquanto bloqueada
                        // registra segmento de IO iniciando no ingresso
                        tcb_add_io_segment(p, tick, p->tarefa.ios[0].duracao);
                        p->io_just_started = 1;
                        printf("[IO] tarefa %s: inicio IO idx=0 no tick %d dur=%d (chegada)\n", p->tarefa.id, tick, p->tarefa.ios[0].duracao);
                    } 
                    else {
                        escalonador_adicionar(p);
                        p->estado = PRONTA;
                    }
                }
            }
        }

        escalonador_set_tick_atual(tick);
        TCB *sel = escalonador_escolher_proxima();
        if (sel) {
            sel->estado = EXECUTANDO;
            sel->waiting_time = 0; // reseta tempo de espera enquanto executando
            tcb_executar_tick(sel, tick);

            if (sel->estado == FINALIZADA) {
                escalonador_marcar_finalizado(sel);
                remaining--;
            } else {
                // Após executar, primeiro verifica operações de mutex no instante relativo da tarefa
                if (sel->tarefa.n_mops > sel->mutex_index) {
                    MutexOp mo = sel->tarefa.mops[sel->mutex_index];
                    if (sel->tempo_executado == mo.instante) {
                        if (mo.kind == 'L') {
                            // tenta adquirir
                            if (mutex_try_acquire(&mutex_head, mo.id, sel)) {
                                // adquirido: consome operação e continua
                                sel->mutex_index += 1;
                                printf("[MUTEX] tarefa %s: ML%d adquirido no tick %d\n", sel->tarefa.id, mo.id, tick);
                            } else {
                                    // não conseguiu adquirir: bloqueia imediatamente e enfileira na lista de espera do mutex
                                    sel->waiting_mutex_id = mo.id;
                                    sel->blocked_since = tick + 1;
                                    sel->waiting_time = 0;
                                    // adiciona à fila de espera do mutex
                                    mutex_enqueue_waiter(&mutex_head, mo.id, sel);
                                    printf("[MUTEX] tarefa %s: bloqueada ML%d no tick %d\n", sel->tarefa.id, mo.id, tick);
                                    escalonador_bloqueia_atual();
                            }
                        } else if (mo.kind == 'U') {
                            // unlock: libera e acorda uma tarefa esperando
                            mutex_release_and_wakeup(&mutex_head, mo.id, tick);
                            sel->mutex_index += 1;
                            printf("[MUTEX] tarefa %s: MU%d liberada no tick %d\n", sel->tarefa.id, mo.id, tick);
                        }
                    }
                }

                // Se nenhum mutex causou bloqueio, então verifica IO ou preempção
                if (sel->estado != BLOQUEADA) {
                    // Após executar, verifica se a tarefa tem um IO que deve iniciar agora
                    if (sel->tarefa.n_ios > sel->io_index) {
                        IOOp io = sel->tarefa.ios[sel->io_index];
                        if (sel->tempo_executado == io.instante) {
                            // inicia IO: seta remaining e bloqueia a tarefa atual
                            if (io.duracao > 0) {
                                sel->io_remaining = io.duracao;
                                sel->io_index += 1;
                                // registra segmento de IO iniciando no próximo tick (IO começa após este tick)
                                tcb_add_io_segment(sel, tick + 1, io.duracao);
                                // escalonador marcará a atual como BLOQUEADA
                                printf("[IO] tarefa %s: inicio IO idx=%d no tick %d dur=%d\n", sel->tarefa.id, sel->io_index-1, tick+1, io.duracao);
                                sel->io_just_started = 1;
                                escalonador_bloqueia_atual();
                            }
                        } else {
                            // Nenhum IO iniciado; apenas cede se o quantum expirou (preempção por quantum)
                            if (escalonador_tick_executado()) {
                                escalonador_prepara_atual();
                            }
                        }
                    } else {
                        // Nenhum IO pendente; apenas cede se o quantum expirou
                        if (escalonador_tick_executado()) {
                            escalonador_prepara_atual();
                        }
                    }
                }
            }
        }

        // Após seleção/execução e qualquer preempção, incrementa waiting_time para tarefas que estão PRONTA
        for (TCB *q = head; q != NULL; q = q->prox) {
            if (q->estado == PRONTA) 
                q->waiting_time += 1;
            // Se bloqueada por IO, decrementa remaining e passa para pronta quando acabar
            if (q->estado == BLOQUEADA) {
                if (q->io_remaining > 0) {
                    if (q->io_just_started) {
                        // primeiro tick após início de IO: não decrementa, apenas limpa a flag
                        q->io_just_started = 0;
                    } else {
                        q->io_remaining -= 1;
                        if (q->io_remaining <= 0) {
                            // IO finalizado: torna pronta
                            escalonador_adicionar(q);
                            q->estado = PRONTA;
                            q->waiting_time = 0;
                            printf("[IO] tarefa %s: fim IO no tick %d\n", q->tarefa.id, tick+1);
                        }
                    }
                }
            }
        }
    }
    if (out_remaining) 
        *out_remaining = remaining;
    // libera lista de nós de mutex (eles não possuem TCBs)
    MutexNode *mcur = mutex_head;
    while (mcur) {
        MutexNode *mn = mcur->next;
        free(mcur);
        mcur = mn;
    }
    return head;
}

// Computa o número total de ticks necessários para exibir o gráfico de Gantt
// escaneando segmentos e valores de tempo_fim. Retorna uma contagem de ticks não negativa.
static int compute_total_ticks(TCB *head) {
    int max_tick = 0;
    for (TCB *p = head; p != NULL; p = p->prox) {
        if (p->tempo_fim > max_tick) 
            max_tick = p->tempo_fim;
        for (int s = 0; s < p->segs_count; ++s) {
            int end = p->segs[s].start + p->segs[s].length;
            if (end > max_tick) 
                max_tick = end;
        }
        // também considera ingresso caso a tarefa nunca tenha rodado
        if (p->tarefa.ingresso > max_tick) 
            max_tick = p->tarefa.ingresso;
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

    // Se não for modo passo: executar simulação completa uma vez e salvar resultados
    if (!modo_passo) {
        int rem = 0;
        TCB *all_head = simulate_to_ticks(&cfg, max_sim_ticks, max_sim_ticks, &rem);
        int total_ticks = compute_total_ticks(all_head);
        grafico_salvar_gantt_svg(all_head, total_ticks, svg_out, cfg.algoritmo, cfg.alpha);

        for (TCB *p = all_head; p != NULL; ) { 
            TCB *nx = p->prox; 
            tcb_free(p); 
            p = nx; 
        }
        parser_liberar(&cfg);
        printf("Simulação finalizada (não-interativa).\n");
        return 0;
    }

    // Modo passo-a-passo reversível
    // Configura terminal para leitura de tecla única
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
        // lê tecla
        char buf[3];
        ssize_t n = read(STDIN_FILENO, buf, 1);
        if (n <= 0) break;
        if (buf[0] == '\n' || buf[0] == '\r') {
            // ENTER == avançar um
            current_tick++;
        } else if (buf[0] == 'q' || buf[0] == 'Q') {
            break;
        } else if (buf[0] == 0x1b) {
            // possível sequência de seta, lê mais dois bytes
            if (read(STDIN_FILENO, buf+1, 2) == 2) {
                if (buf[1] == '[' && buf[2] == 'C') { // direita
                    current_tick++;
                } else if (buf[1] == '[' && buf[2] == 'D') { // esquerda
                    if (current_tick > 0) current_tick--;
                }
            }
        } else if (buf[0] == 'a' || buf[0] == 'A') {
            // opcional: executar até o fim
            // define current_tick para um número grande para deixar a simulação rodar até remaining==0
            current_tick = max_sim_ticks;
        }

    if (current_tick < 0) current_tick = 0;
    // reconstruir view_head para o tick selecionado
    // NOTA: não liberar view_head anterior aqui; liberar neste loop interativo
    // causou travamentos intermitentes (provavelmente devido a compartilhamento interno de ponteiros).
    // Manter alocações anteriores para a sessão interativa e liberar apenas no final.
    TCB *new_view = simulate_to_ticks(&cfg, current_tick, max_sim_ticks, &view_remaining);
    // liberar view interativa anterior para evitar vazamento de memória entre passos
    if (view_head && view_head != new_view) {
        for (TCB *p = view_head; p != NULL; ) { TCB *nx = p->prox; tcb_free(p); p = nx; }
    }
    view_head = new_view;

        // Salvar Gantt para este passo (sobrescrever saída a cada passo)
        if (grafico_salvar_gantt_svg(view_head, current_tick, svg_out, cfg.algoritmo, cfg.alpha) != 0) {
            fprintf(stderr, "Falha ao salvar SVG em %s (tick %d)\n", svg_out, current_tick);
        }

        // Se não houver tarefas restantes, sair do modo interativo automaticamente
        if (view_remaining == 0) {
            printf("Todas as tarefas finalizaram no tick %d. Saindo...\n", current_tick);
            break;
        }
    }

    // restaurar terminal
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);

    int total_ticks = current_tick;
    grafico_salvar_gantt_svg(view_head, total_ticks, svg_out, cfg.algoritmo, cfg.alpha);

    for (TCB *p = view_head; p != NULL; ) { TCB *nx = p->prox; tcb_free(p); p = nx; }
    parser_liberar(&cfg);

    printf("Simulação finalizada em %d ticks\n", total_ticks);
    return 0;
}
