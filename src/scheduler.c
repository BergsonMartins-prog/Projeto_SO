#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "scheduler.h"

// Lista encadeada de prontos  
static TCB *prontos_head = NULL;
static TCB *prontos_tail = NULL;
// Ponteiro para o TCB atualmente executando 
static TCB *executando = NULL;
// Strings/parametros do algoritmo configurado
static char algoritmo[32];
static int quantum_global = 0;

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

// Inicializa com algoritmo e quantum 
void scheduler_init(const char *alg, int quantum) {
    strncpy(algoritmo, alg, sizeof(algoritmo)-1);
    algoritmo[sizeof(algoritmo)-1] = '\0';
    quantum_global = quantum;
    prontos_head = prontos_tail = NULL;
    executando = NULL;
    printf("[Scheduler] inicializado: algoritmo=%s quantum=%d\n", algoritmo, quantum_global);
}

// Adiciona tcb à fila de prontos 
void scheduler_adicionar(TCB *tcb){
    if(!tcb) return;
    tcb->prox_pronto = NULL;
    tcb->estado = PRONTA;
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

static TCB* choose_priority(void){
    TCB *best = NULL;
    for(TCB *p = prontos_head; p != NULL; p = p->prox_pronto){
        if (!best || p->tarefa.prioridade > best->tarefa.prioridade){
            best = p;
        }
    }
    if(executando && executando->estado == EXECUTANDO && executando->tempo_restante > 0){
        if(!best || executando->tarefa.prioridade >= best->tarefa.prioridade){
            return executando;
        }
    }
    return best;
}

//escolhe a próxima tarefa e a marca como executando.
TCB* scheduler_escolher_proxima(void){
    TCB *chosen = NULL;
    if(strcasecmp(algoritmo, "FIFO") == 0){
        chosen = choose_fifo();
    } 
    else if(strcasecmp(algoritmo, "SRTF") == 0){
        chosen = choose_srtf();
    } 
    else if(strcasecmp(algoritmo, "PRIORIDADE") == 0 || strcasecmp(algoritmo, "PRIORITY") == 0) {
        chosen = choose_priority();
    } 
    else{
        chosen = choose_fifo();
    }

    // Se o escolhido estiver na fila de prontos, remover e marcar como EXECUTANDO
    if (chosen) {
        scheduler_remover(chosen);
        chosen->estado = EXECUTANDO;
        executando = chosen;
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
    t->prox_pronto = NULL;

    if (!prontos_head){
        prontos_head = prontos_tail = t;
    } 
    else {
        prontos_tail->prox_pronto = t;
        prontos_tail = t;
    }
}
