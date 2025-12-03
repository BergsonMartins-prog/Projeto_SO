#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include "scheduler.h"

// Lista encadeada de prontos  
static TCB *prontos_head = NULL;
static TCB *prontos_tail = NULL;
// Ponteiro para o TCB atualmente executando 
static TCB *executando = NULL;
// Strings/parametros do algoritmo configurado
static char algoritmo[32];
static int quantum_global = 0;
static int alpha_global = 0; // aging parameter
static int current_tick_global = 0;
static int exec_tick_count = 0; // ticks executed by current running task in its current quantum

// Remove um TCB da fila de prontos 
void scheduler_remover(TCB *tcb){
    if(!prontos_head || !tcb){
        return;
    }
    // usa prox_pronto para operar a fila de prontos, pra presevar a prox
    if(prontos_head == tcb){
        prontos_head = prontos_head->prox_pronto;
        if (!prontos_head) prontos_tail = NULL;
        tcb->prox_pronto = NULL;
        return;
    }
    TCB *p = prontos_head;
    while(p && p->prox_pronto && p->prox_pronto != tcb) p = p->prox_pronto;
    if(p && p->prox_pronto == tcb){
        p->prox_pronto = tcb->prox_pronto;
        if (tcb == prontos_tail) prontos_tail = p;
        tcb->prox_pronto = NULL;
    }
}

// Inicializa com algoritmo, quantum e alpha (envelhecimento)
void scheduler_init(const char *alg, int quantum, int alpha) {
    strncpy(algoritmo, alg, sizeof(algoritmo)-1);
    algoritmo[sizeof(algoritmo)-1] = '\0';
    quantum_global = quantum;
    alpha_global = alpha;
    current_tick_global = 0;
    prontos_head = prontos_tail = NULL;
    executando = NULL;
    printf("[Scheduler] inicializado: algoritmo=%s quantum=%d alpha=%d\n", algoritmo, quantum_global, alpha_global);
}

// Set the current simulation tick (used for aging calculations)
void scheduler_set_current_tick(int tick) {
    current_tick_global = tick;
}

int scheduler_tick_executed(void) {
    if (!executando) return 0;
    // Only enforce quantum for RR algorithm
    if (!(strcasecmp(algoritmo, "RR") == 0)) return 0;
    // no quantum configured: never force yield here
    if (quantum_global <= 0) return 0;
    exec_tick_count += 1;
    if (exec_tick_count >= quantum_global) {
        // quantum expired
        exec_tick_count = 0;
        return 1;
    }
    return 0;
}

// Adiciona tcb à fila de prontos 
void scheduler_adicionar(TCB *tcb){
    if(!tcb) return;
    tcb->prox_pronto = NULL;
    tcb->estado = PRONTA;
    tcb->waiting_time = 0; // reset waiting time when added to ready queue
    if(!prontos_head){
        prontos_head = prontos_tail = tcb;
    } 
    else{
        prontos_tail->prox_pronto = tcb;
        prontos_tail = tcb;
    }
}

/* --- Funções internas de escolha para cada algoritmo --- */
static TCB* choose_fifo(void){
    if(executando && executando->estado == EXECUTANDO && executando->tempo_restante > 0){
        return executando;
    }
    return prontos_head;
}

static TCB* choose_srtf(void){
    TCB *best = NULL;
    for(TCB *p = prontos_head; p != NULL; p = p->prox_pronto){
        if (!best || p->tempo_restante < best->tempo_restante) {
            best = p;
        }
    }
    if(executando && executando->estado == EXECUTANDO && executando->tempo_restante > 0){
        if(!best || executando->tempo_restante <= best->tempo_restante){
            return executando;
        }
    }
    return best;
}

// Priority chooser without aging (original behavior)
static TCB* choose_priority_noaging(void) {
    TCB *best = NULL;
    for (TCB *p = prontos_head; p != NULL; p = p->prox_pronto) {
        if (!best || p->tarefa.prioridade > best->tarefa.prioridade) {
            best = p;
        }
    }
    if (executando && executando->estado == EXECUTANDO && executando->tempo_restante > 0) {
        if (!best || executando->tarefa.prioridade >= best->tarefa.prioridade) {
            return executando;
        }
    }
    return best;
}

// Priority chooser with aging (effective priority = base + alpha * wait_time)
static TCB* choose_priority_aging(void) {
    TCB *best = NULL;
    long best_eff = LONG_MIN;
    for (TCB *p = prontos_head; p != NULL; p = p->prox_pronto) {
        long eff;
        // If the task is blocked (in IO), do not apply aging; use base priority
        if (p->estado == BLOQUEADA) {
            eff = (long)p->tarefa.prioridade;
        } else {
            long wait = p->waiting_time;
            if (wait < 0) wait = 0;
            eff = (long)p->tarefa.prioridade + (long)alpha_global * wait;
        }
        if (!best || eff > best_eff) { best = p; best_eff = eff; }
    }
    if (executando && executando->estado == EXECUTANDO && executando->tempo_restante > 0) {
        long eff_e;
        // Executing task should not be considered aged while it's running; use base priority
        if (executando->estado == BLOQUEADA) {
            eff_e = (long)executando->tarefa.prioridade;
        } else {
            long wait_e = executando->waiting_time;
            if (wait_e < 0) wait_e = 0;
            eff_e = (long)executando->tarefa.prioridade + (long)alpha_global * wait_e;
        }
        if (!best || eff_e >= best_eff) {
            return executando;
        }
    }
    return best;
}

//escolhe a próxima tarefa e a marca como executando.
TCB* scheduler_escolher_proxima(void){
    TCB *chosen = NULL;
    // If there is a currently executing task and it still has remaining time
    // For non-RR algorithms, keep it running until it finishes or blocks (no quantum-based preemption).
    if (executando && executando->estado == EXECUTANDO && executando->tempo_restante > 0) {
        if (strcasecmp(algoritmo, "RR") != 0) {
            return executando;
        } else {
            // RR: allow running until its quantum expires (exec_tick_count handles it)
            if (quantum_global > 0 && exec_tick_count < quantum_global) return executando;
        }
    }
    if(strcasecmp(algoritmo, "FIFO") == 0){
        chosen = choose_fifo();
    } 
    else if(strcasecmp(algoritmo, "SRTF") == 0){
        chosen = choose_srtf();
    } 
    else if(strcasecmp(algoritmo, "PRIORIDADE") == 0 || strcasecmp(algoritmo, "PRIOP") == 0) {
        chosen = choose_priority_noaging();
    } else if (strcasecmp(algoritmo, "PRIOPEnv") == 0 || strcasecmp(algoritmo, "PRIOPENV") == 0) {
        /* Preemptive priority with aging */
        chosen = choose_priority_aging();
    } else if (strcasecmp(algoritmo, "RR") == 0) {
        /* Round Robin behaves like FIFO in selection order, quantum handled globally */
        chosen = choose_fifo();
    } 
    else{
        chosen = choose_fifo();
    }

    // Se o escolhido estiver na fila de prontos, remover e marcar como EXECUTANDO
    if (chosen) {
        scheduler_remover(chosen);
        chosen->estado = EXECUTANDO;
        executando = chosen;
        // reset quantum counter for newly chosen task
        exec_tick_count = 0;
    } else {
        executando = NULL;
    }
    return chosen;
}

// Retorna o TCB atualmente executando
TCB* scheduler_get_executando(void) {
    return executando;
}

// Marca o TCB como finalizado e o remove da fila de prontos
void scheduler_marcar_finalizado(TCB *tcb) {
    if (!tcb) return;
    scheduler_remover(tcb);
    tcb->estado = FINALIZADA;
    if (tcb == executando) executando = NULL;
}

// Devolve o TCB atualmente em execução para a fila de prontos.
// Isso é usado quando um tick termina e a tarefa ainda não finalizou,
void scheduler_yield_current(void){
    if(!executando){
        return;
    }

    TCB *t = executando;
    executando = NULL;

    if(t->estado == FINALIZADA){

        t->prox_pronto = NULL;
        return;
    }

    t->estado = PRONTA;
    t->waiting_time = 0; // reset waiting time when yielding current to ready
    t->prox_pronto = NULL;

    if (!prontos_head){
        prontos_head = prontos_tail = t;
    } 
    else {
        prontos_tail->prox_pronto = t;
        prontos_tail = t;
    }
}

// Block the currently executing TCB (used when the task starts an IO operation).
void scheduler_block_current(void) {
    if (!executando) return;
    TCB *t = executando;
    executando = NULL;
    // mark as blocked
    t->estado = BLOQUEADA;
    // reset waiting time while blocked so aging does not reflect prior waiting
    t->waiting_time = 0;
    // ensure it's not in ready queue
    t->prox_pronto = NULL;
}
