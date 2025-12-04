#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include "escalonador.h"

static TCB *prontos_head = NULL; // Lista encadeada de prontos  
static TCB *prontos_tail = NULL;
static TCB *executando = NULL; // Ponteiro para o TCB atualmente executando

// Strings/parametros do algoritmo configurado
static char algoritmo[32];
static int quantum_global = 0;
static int alpha_global = 0; // parâmetro de envelhecimento
static int current_tick_global = 0;
static int exec_tick_count = 0; // ticks executados pela tarefa atual em seu quantum atual

// Remove um TCB da fila de prontos 
void escalonador_remover(TCB *tcb){
    if(!prontos_head || !tcb){
        return;
    }

    // usa prox_pronto para operar a fila de prontos, pra preservar a proxima tarefa
    if(prontos_head == tcb){
        prontos_head = prontos_head->prox_pronto;
        if (!prontos_head) prontos_tail = NULL;
        tcb->prox_pronto = NULL;
        return;
    }
    TCB *p = prontos_head;
    while(p && p->prox_pronto && p->prox_pronto != tcb)
        p = p->prox_pronto;
    if(p && p->prox_pronto == tcb){
        p->prox_pronto = tcb->prox_pronto;
        if (tcb == prontos_tail)
            prontos_tail = p;
        tcb->prox_pronto = NULL;
    }
}

// Inicializa com algoritmo, quantum e alpha (envelhecimento)
void escalonador_init(const char *alg, int quantum, int alpha) {
    strncpy(algoritmo, alg, sizeof(algoritmo)-1);
    algoritmo[sizeof(algoritmo)-1] = '\0';
    quantum_global = quantum;
    alpha_global = alpha;
    current_tick_global = 0;
    prontos_head = prontos_tail = NULL;
    executando = NULL;
    printf("[Escalonador] inicializado: algoritmo=%s quantum=%d alpha=%d\n", algoritmo, quantum_global, alpha_global);
}

// Define o tick atual da simulação (usado para cálculo de envelhecimento)
void escalonador_set_tick_atual(int tick) {
    current_tick_global = tick;
}

int escalonador_tick_executado(void) {
    if (!executando)
        return 0;
    // Aplica quantum apenas para algoritmo RR
    if (!(strcasecmp(algoritmo, "RR") == 0))
        return 0;
    // nenhum quantum configurado: nunca força preempção
    if (quantum_global <= 0)
        return 0;
    exec_tick_count += 1;
    if (exec_tick_count >= quantum_global) {
        // quantum expirou
        exec_tick_count = 0;
        return 1;
    }
    return 0;
}

// Adiciona tcb à fila de prontos 
void escalonador_adicionar(TCB *tcb){
    if(!tcb) return;
    tcb->prox_pronto = NULL;
    tcb->estado = PRONTA;
    tcb->waiting_time = 0; // reseta tempo de espera quando adicionada à fila de prontos
    if(!prontos_head){
        prontos_head = prontos_tail = tcb;
    } 
    else{
        prontos_tail->prox_pronto = tcb;
        prontos_tail = tcb;
    }
}

// Funções internas de escolha para cada algoritmo
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

static TCB* choose_priority_aging(void) {
    TCB *best = NULL;
    long best_eff = LONG_MIN;
    for (TCB *p = prontos_head; p != NULL; p = p->prox_pronto) {
        long eff;
        // Se a tarefa está bloqueada (em IO), não aplica envelhecimento; usa prioridade base
        if (p->estado == BLOQUEADA) {
            eff = (long)p->tarefa.prioridade;
        } else {
            long wait = p->waiting_time;
            if (wait < 0)
                wait = 0;
            eff = (long)p->tarefa.prioridade + (long)alpha_global * wait;
        }
        if (!best || eff > best_eff) {
            best = p;
            best_eff = eff;
        }
    }
    if (executando && executando->estado == EXECUTANDO && executando->tempo_restante > 0) {
        long eff_e;
        // Tarefa em execução não deve ser considerada envelhecida enquanto está rodando; usa prioridade base
        if (executando->estado == BLOQUEADA) {
            eff_e = (long)executando->tarefa.prioridade;
        } else {
            long wait_e = executando->waiting_time;
            if (wait_e < 0) 
                wait_e = 0;
            eff_e = (long)executando->tarefa.prioridade + (long)alpha_global * wait_e;
        }
        if (!best || eff_e >= best_eff) {
            return executando;
        }
    }
    return best;
}

//escolhe a próxima tarefa e a marca como executando.
TCB* escalonador_escolher_proxima(void){
    TCB *chosen = NULL;
    
    if (executando && executando->estado == EXECUTANDO && executando->tempo_restante > 0) {
        if (strcasecmp(algoritmo, "RR") != 0) {
            return executando;
        } else {
            // RR roda até expirar o quantum
            if (quantum_global > 0 && exec_tick_count < quantum_global)
                return executando;
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
    } 
    else if (strcasecmp(algoritmo, "PRIOPEnv") == 0 || strcasecmp(algoritmo, "PRIOPENV") == 0) {
        chosen = choose_priority_aging();
    } 
    else if (strcasecmp(algoritmo, "RR") == 0) {
        chosen = choose_fifo();
    } 
    else{
        chosen = choose_fifo();
    }

    // Se o escolhido estiver na fila de prontos, remover e marcar como EXECUTANDO
    if (chosen) {
        escalonador_remover(chosen);
        chosen->estado = EXECUTANDO;
        executando = chosen;
        exec_tick_count = 0;
    } else {
        executando = NULL;
    }
    return chosen;
}

// Retorna o TCB atualmente executando
TCB* escalonador_get_executando(void) {
    return executando;
}

// Marca o TCB como finalizado e o remove da fila de prontos
void escalonador_marcar_finalizado(TCB *tcb) {
    if (!tcb)
        return;
    escalonador_remover(tcb);
    tcb->estado = FINALIZADA;
    if (tcb == executando)
        executando = NULL;
}

// Devolve o TCB atualmente em execução para a fila de prontos.
// Isso é usado quando um quantum termina e a tarefa ainda não finalizou,
void escalonador_prepara_atual(void){
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
    t->waiting_time = 0;
    t->prox_pronto = NULL;

    if (!prontos_head){
        prontos_head = prontos_tail = t;
    } 
    else {
        prontos_tail->prox_pronto = t;
        prontos_tail = t;
    }
}

// Bloqueia o TCB atualmente em execução (usado quando a tarefa inicia uma operação de IO).
void escalonador_bloqueia_atual(void) {
    if (!executando)
        return;
    TCB *t = executando;
    executando = NULL;
    t->estado = BLOQUEADA;
    t->waiting_time = 0;
    t->prox_pronto = NULL;
}
