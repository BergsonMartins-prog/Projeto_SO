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

// Mutex data structure and helpers (managed per-simulation run from simulate_to_ticks)
typedef struct MutexNode {
    int id;
    TCB *owner;
    TCB *wait_head; // FIFO queue of waiting TCBs
    TCB *wait_tail;
    struct MutexNode *next;
} MutexNode;

static MutexNode* mutex_find(MutexNode *root, int id) {
    MutexNode *p = root;
    while(p) { if (p->id == id) return p; p = p->next; }
    return NULL;
}

static MutexNode* mutex_ensure(MutexNode **root, int id) {
    MutexNode *n = mutex_find(*root, id);
    if (n) return n;
    n = (MutexNode*) malloc(sizeof(MutexNode));
    if (!n) { perror("malloc"); exit(EXIT_FAILURE); }
    n->id = id; n->owner = NULL; n->wait_head = n->wait_tail = NULL; n->next = NULL;
    // append
    if (!*root) *root = n; else {
        MutexNode *q = *root; while(q->next) q = q->next; q->next = n;
    }
    return n;
}

static int mutex_try_acquire(MutexNode **root, int id, TCB *req) {
    MutexNode *n = mutex_ensure(root, id);
    if (n->owner == NULL) { n->owner = req; return 1; }
    return 0;
}

// enqueue a waiter (if not already enqueued)
static void mutex_enqueue_waiter(MutexNode **root, int id, TCB *waiter) {
    if (!waiter) return;
    /* debug prints removed */
    MutexNode *n = mutex_ensure(root, id);
    // avoid double-enqueue
    if (waiter->waiting_mutex_id == id && waiter->mutex_next != NULL) return;
    waiter->mutex_next = NULL;
    if (!n->wait_head) { n->wait_head = n->wait_tail = waiter; }
    else { n->wait_tail->mutex_next = waiter; n->wait_tail = waiter; }
}

// dequeue next waiter (FIFO) or NULL
static TCB* mutex_dequeue_waiter(MutexNode **root, int id) {
    MutexNode *n = mutex_find(*root, id);
    if (!n || !n->wait_head) return NULL;
    TCB *w = n->wait_head;
    n->wait_head = w->mutex_next;
    if (!n->wait_head) n->wait_tail = NULL;
    w->mutex_next = NULL;
    return w;
}

// release mutex and wake up one waiting TCB (FIFO by blocked_since). Returns awakened TCB or NULL.
static TCB* mutex_release_and_wakeup(MutexNode **root, int id, int tick) {
    MutexNode *n = mutex_find(*root, id);
    if (!n) return NULL;
    n->owner = NULL;
    // pop next waiter from FIFO
    TCB *best = mutex_dequeue_waiter(root, id);
    if (best) {
        // transfer ownership
        n->owner = best;
        // record blocked interval (duration)
        if (best->blocked_since >= 0) {
            int dur = (tick + 1) - best->blocked_since;
            if (dur > 0) tcb_add_io_segment(best, best->blocked_since, dur);
        }
        // consume the pending ML operation for the woken task if it matches
        if (best->mutex_index < best->tarefa.n_mops) {
            MutexOp mo = best->tarefa.mops[best->mutex_index];
            if (mo.kind == 'L' && mo.id == id) {
                best->mutex_index += 1;
                printf("[MUTEX] tarefa %s: ML%d granted at tick %d (wakeup)\n", best->tarefa.id, id, tick);
            }
        }
        best->waiting_mutex_id = -1;
        best->blocked_since = -1;
        best->estado = PRONTA;
        best->waiting_time = 0;
        // add to ready queue
        scheduler_adicionar(best);
        return best;
    }
    return NULL;
}

// Simula do estado inicial (arquivo de configuração) até `ticks_completed` ticks.
// Retorna uma lista de TCBs (alocada) representando o estado após `ticks_completed`.
// Caller must free the returned TCBs with tcb_free.
// Simulate up to `ticks_completed` ticks (or until all tasks finish).
// Returns a freshly-allocated list of TCBs representing state after simulation.
// If `out_remaining` is non-NULL, it is set to the number of tasks still not finalized after the simulation.
static TCB* simulate_to_ticks(const Config *cfg, int ticks_completed, int max_sim_ticks, int *out_remaining) {
    TCB *head = NULL, *tail = NULL;
    MutexNode *mutex_head = NULL;
    for (int i = 0; i < cfg->n_tarefas; ++i) {
        TCB *tcb = tcb_criar(&cfg->tarefas[i]);
        if (!head) head = tail = tcb; else { tail->prox = tcb; tail = tcb; }
    }

    scheduler_init(cfg->algoritmo, cfg->quantum, cfg->alpha);

    int remaining = cfg->n_tarefas;
    for (int tick = 0; tick < ticks_completed && tick < max_sim_ticks && remaining > 0; ++tick) {
        for (TCB *p = head; p != NULL; p = p->prox) {
            if (p->estado == NOVA && p->tarefa.ingresso == tick) {
                // At arrival, check for mutex ops or IO at instant 0
                int handled = 0;
                if (p->tarefa.n_mops > 0 && p->tarefa.mops[0].instante == 0) {
                    MutexOp mo = p->tarefa.mops[0];
                    if (mo.kind == 'L') {
                        // try to acquire
                        if (mutex_try_acquire(&mutex_head, mo.id, p)) {
                            // acquired: nothing else to do here
                            handled = 0;
                        } else {
                            // couldn't acquire: block immediately and enqueue on mutex wait list
                            p->estado = BLOQUEADA;
                            p->waiting_mutex_id = mo.id;
                            p->blocked_since = tick + 1;
                            p->waiting_time = 0;
                            // add to per-mutex wait queue
                            mutex_enqueue_waiter(&mutex_head, mo.id, p);
                            printf("[MUTEX] tarefa %s: bloqueada ML%d at tick %d (arrival)\n", p->tarefa.id, mo.id, tick);
                            handled = 1;
                        }
                    } else if (mo.kind == 'U') {
                        // unlock: release and wake one
                        mutex_release_and_wakeup(&mutex_head, mo.id, tick);
                        handled = 0;
                    }
                }
                if (!handled) {
                    // If the task has an IO at instant 0, start it blocked immediately
                    if (p->tarefa.n_ios > 0 && p->tarefa.ios[0].instante == 0) {
                        p->estado = BLOQUEADA;
                        p->io_index = 1; // consume this IO
                        p->io_remaining = p->tarefa.ios[0].duracao;
                        p->waiting_time = 0; // ensure no aging while blocked
                        // record IO segment starting at ingresso
                        tcb_add_io_segment(p, tick, p->tarefa.ios[0].duracao);
                        p->io_just_started = 1;
                        printf("[IO] tarefa %s: inicio IO idx=0 at tick %d dur=%d (arrival)\n", p->tarefa.id, tick, p->tarefa.ios[0].duracao);
                    } else {
                        scheduler_adicionar(p);
                        p->estado = PRONTA;
                    }
                }
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
                // After executing, first check for mutex ops at this task-relative instant
                if (sel->tarefa.n_mops > sel->mutex_index) {
                    MutexOp mo = sel->tarefa.mops[sel->mutex_index];
                    if (sel->tempo_executado == mo.instante) {
                        if (mo.kind == 'L') {
                            // attempt to acquire
                            if (mutex_try_acquire(&mutex_head, mo.id, sel)) {
                                // acquired: consume op and continue
                                sel->mutex_index += 1;
                                printf("[MUTEX] tarefa %s: ML%d acquired at tick %d\n", sel->tarefa.id, mo.id, tick);
                            } else {
                                    // couldn't acquire: block current task and enqueue on mutex wait list
                                    sel->waiting_mutex_id = mo.id;
                                    sel->blocked_since = tick + 1;
                                    sel->waiting_time = 0;
                                    // enqueue on mutex wait queue
                                    mutex_enqueue_waiter(&mutex_head, mo.id, sel);
                                    printf("[MUTEX] tarefa %s: blocked ML%d at tick %d\n", sel->tarefa.id, mo.id, tick);
                                    scheduler_block_current();
                            }
                        } else if (mo.kind == 'U') {
                            // unlock: release and wake one waiting task
                            mutex_release_and_wakeup(&mutex_head, mo.id, tick);
                            sel->mutex_index += 1;
                            printf("[MUTEX] tarefa %s: MU%d released at tick %d\n", sel->tarefa.id, mo.id, tick);
                        }
                    }
                }

                // If no mutex caused blocking, then check for IO or preemption
                if (sel->estado != BLOQUEADA) {
                    // After executing, check if the task has an IO that should start now
                    if (sel->tarefa.n_ios > sel->io_index) {
                        IOOp io = sel->tarefa.ios[sel->io_index];
                        if (sel->tempo_executado == io.instante) {
                            // start IO: set remaining and block current task
                            if (io.duracao > 0) {
                                sel->io_remaining = io.duracao;
                                sel->io_index += 1;
                                // record IO segment starting at next tick (IO begins after this tick)
                                tcb_add_io_segment(sel, tick + 1, io.duracao);
                                // scheduler will mark the current as BLOQUEADA
                                printf("[IO] tarefa %s: inicio IO idx=%d at tick %d dur=%d\n", sel->tarefa.id, sel->io_index-1, tick+1, io.duracao);
                                sel->io_just_started = 1;
                                scheduler_block_current();
                            }
                        } else {
                            // No IO started; only yield if quantum expired (preemption by quantum)
                            if (scheduler_tick_executed()) {
                                scheduler_yield_current();
                            }
                        }
                    } else {
                        // No pending IOs; only yield if quantum expired
                        if (scheduler_tick_executed()) {
                            scheduler_yield_current();
                        }
                    }
                }
            }
        }

        // After selection/execution and any yielding, increment waiting_time for tasks that are PRONTA
        for (TCB *q = head; q != NULL; q = q->prox) {
            if (q->estado == PRONTA) q->waiting_time += 1;
            // If blocked for IO, decrement remaining and move to ready when done
            if (q->estado == BLOQUEADA) {
                if (q->io_remaining > 0) {
                    if (q->io_just_started) {
                        // first tick after IO start: don't decrement, just clear the flag
                        q->io_just_started = 0;
                    } else {
                        q->io_remaining -= 1;
                        if (q->io_remaining <= 0) {
                            // IO finished: make ready
                            scheduler_adicionar(q);
                            q->estado = PRONTA;
                            q->waiting_time = 0;
                            printf("[IO] tarefa %s: fim IO at tick %d\n", q->tarefa.id, tick+1);
                        }
                    }
                }
            }
        }
    }
    if (out_remaining) *out_remaining = remaining;
    // free mutex nodes list (they don't own TCBs)
    MutexNode *mcur = mutex_head;
    while (mcur) {
        MutexNode *mn = mcur->next;
        free(mcur);
        mcur = mn;
    }
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

    // If not passo mode: run full simulation once and save results
    if (!modo_passo) {
        int rem = 0;
        TCB *all_head = simulate_to_ticks(&cfg, max_sim_ticks, max_sim_ticks, &rem);
        int total_ticks = compute_total_ticks(all_head);
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
    // NOTE: do not free previous view_head here; freeing in this interactive loop
    // has caused intermittent crashes (likely due to internal pointer sharing).
    // Keep previous allocations for the interactive session and free once at the end.
    TCB *new_view = simulate_to_ticks(&cfg, current_tick, max_sim_ticks, &view_remaining);
    // free previous interactive view to avoid leaking memory across steps
    if (view_head && view_head != new_view) {
        for (TCB *p = view_head; p != NULL; ) { TCB *nx = p->prox; tcb_free(p); p = nx; }
    }
    view_head = new_view;

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
