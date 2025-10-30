#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tcb.h"

#define CAPACIDADE_SEGS 4

//incializa TCB
TCB* tcb_criar(const Tarefa *t) {
    TCB *novo = (TCB*) malloc(sizeof(TCB));
    if (!novo) {
        printf("[ERRO] Falha ao alocar TCB\n");
        return NULL;
    }

    novo->tarefa = *t;
    novo->estado = NOVA;
    novo->tempo_restante = t->duracao;
    novo->tempo_executado = 0;
    novo->tempo_inicio = -1;
    novo->tempo_fim = -1;
    //prox: mantém a lista global de TCBs (all_head)
    novo->prox = NULL;
    //prox_pronto: usado pela fila de prontos no scheduler
    novo->prox_pronto = NULL;

    //aloca array inicial para segmentos do Gantt
    novo->segs = (Seg*) malloc(sizeof(Seg) * CAPACIDADE_SEGS);
    if (!novo->segs) {
        printf("[ERRO] Falha ao alocar segmentos\n");
        free(novo);
        return NULL;
    }
    novo->segs_count = 0;
    novo->segs_cap = CAPACIDADE_SEGS;

    return novo;
}

//Garante capacidade no array de segmentos (dobra quando cheio)
static void ensure_segs(TCB *tcb) {
    //Invariante esperada: segs_cap > 0 (inicializado em tcb_criar)
    if (tcb->segs_count >= tcb->segs_cap) {
        int nc = tcb->segs_cap * 2; // nova capacidade (dobrar)
        Seg *ns = (Seg*) realloc(tcb->segs, sizeof(Seg) * nc);
        if (!ns) {
            fprintf(stderr, "[ERRO] Falha realloc segs\n");
            exit(EXIT_FAILURE);
        }
        tcb->segs = ns;
        tcb->segs_cap = nc;
    }
}

//Concatena com o anterior se contínuo
void tcb_add_segment(TCB *tcb, int start, int length) {
    if (!tcb) return;
    if (length <= 0) return;

    //Se houver um segmento anterior, checar contiguidade
    if (tcb->segs_count > 0) {
        Seg *last = &tcb->segs[tcb->segs_count - 1];
        // Contíguo se último termina exatamente em 'start'
        if (last->start + last->length == start) {
            last->length += length; // concatena
            return;
        }
    }

    //Garantir espaço e inserir novo segmento
    ensure_segs(tcb);
    tcb->segs[tcb->segs_count].start = start;
    tcb->segs[tcb->segs_count].length = length;
    tcb->segs_count++;
}

//Executa 1 tick e finaliza se restante==0
void tcb_executar_tick(TCB *tcb, int tick) {
    if (!tcb) return;
    if (tcb->tempo_restante <= 0) {
        return;
    } //já terminou ou inválido
    if (tcb->tempo_inicio == -1) {
        tcb->tempo_inicio = tick;
    }   
    //Atualiza segmentos: se o último segmento termina em 'tick', estende; senão cria novo
    if (tcb->segs_count > 0) {
        Seg *last = &tcb->segs[tcb->segs_count - 1];
        if (last->start + last->length == tick) {
            //contínuo: estende em 1 
            last->length += 1;
        } else {
            //não contínuo: novo segmento de comprimento 1
            tcb_add_segment(tcb, tick, 1);
        }
    } else {
        //primeiro segmento
        tcb_add_segment(tcb, tick, 1);
    }

    //atualizar contadores
    tcb->tempo_executado += 1;
    tcb->tempo_restante -= 1;

    //Se terminou, registrar fim e estado
    if (tcb->tempo_restante == 0) {
        tcb->tempo_fim = tick + 1; //fim do intervalo [tick, tick+1)
        tcb->estado = FINALIZADA;
    }
}

//muda estado do TCB
void tcb_mudar_estado(TCB *tcb, Estado novo) {
    if (!tcb) return;
    tcb->estado = novo;
}

/* Exibe informações para debug. */
void tcb_exibir(const TCB *tcb) {
    if (!tcb) return;
    printf("TCB[%s] estado=%d chegada=%d dur=%d rem=%d pri=%d inicio=%d fim=%d\n",
           tcb->tarefa.id,
           tcb->estado,
           tcb->tarefa.ingresso,
           tcb->tarefa.duracao,
           tcb->tempo_restante,
           tcb->tarefa.prioridade,
           tcb->tempo_inicio,
           tcb->tempo_fim);
}

//libera memória do TCB
void tcb_free(TCB *tcb) {
    if (!tcb) return;
    if (tcb->segs) free(tcb->segs);
    free(tcb);
}
