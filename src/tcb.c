#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tcb.h"

/* Capacidade inicial do array de segmentos. */
#define INITIAL_SEGS_CAP 4

/* Cria TCB a partir de Tarefa e inicializa campos. */
TCB* tcb_criar(const Tarefa *t) {
    TCB *novo = (TCB*) malloc(sizeof(TCB));
    if (!novo) {
        fprintf(stderr, "[ERRO] Falha ao alocar TCB\n");
        exit(EXIT_FAILURE); /* Em projeto acadêmico podemos abortar; em produção, retornar erro */
    }

    /* copia os dados estáticos */
    novo->tarefa = *t;

    /* inicializa campos dinâmicos */
    novo->estado = NOVA;
    novo->tempo_restante = t->duracao;
    novo->tempo_executado = 0;
    novo->tempo_inicio = -1;
    novo->tempo_fim = -1;
    /* prox: mantém a lista global de TCBs (all_head) */
    novo->prox = NULL;
    /* prox_pronto: usado pela fila de prontos no scheduler */
    novo->prox_pronto = NULL;

    /* aloca array inicial para segmentos do Gantt */
    novo->segs = (Seg*) malloc(sizeof(Seg) * INITIAL_SEGS_CAP);
    if (!novo->segs) {
        fprintf(stderr, "[ERRO] Falha ao alocar segmentos\n");
        free(novo);
        exit(EXIT_FAILURE);
    }
    novo->segs_count = 0;
    novo->segs_cap = INITIAL_SEGS_CAP;

    return novo;
}

/* Garante capacidade no array de segmentos (dobra quando cheio). */
static void ensure_segs(TCB *tcb) {
    /* Invariante esperada: segs_cap > 0 (inicializado em tcb_criar) */
    if (tcb->segs_count >= tcb->segs_cap) {
        int nc = tcb->segs_cap * 2; /* nova capacidade (dobrar) */
        Seg *ns = (Seg*) realloc(tcb->segs, sizeof(Seg) * nc);
        if (!ns) {
            fprintf(stderr, "[ERRO] Falha realloc segs\n");
            exit(EXIT_FAILURE);
        }
        tcb->segs = ns;
        tcb->segs_cap = nc;
    }
}

/* Adiciona segmento; concatena com o anterior se contíguo. */
void tcb_add_segment(TCB *tcb, int start, int length) {
    if (!tcb) return;
    if (length <= 0) return;

    /* Se houver um segmento anterior, checar contiguidade */
    if (tcb->segs_count > 0) {
        Seg *last = &tcb->segs[tcb->segs_count - 1];
        /* Contíguo se último termina exatamente em 'start' */
        if (last->start + last->length == start) {
            last->length += length; /* concatena */
            return;
        }
    }

    /* Garantir espaço e inserir novo segmento */
    ensure_segs(tcb);
    tcb->segs[tcb->segs_count].start = start;
    tcb->segs[tcb->segs_count].length = length;
    tcb->segs_count++;
}

/* Executa 1 tick: atualiza segmentos/contadores; finaliza se rem==0. */
void tcb_executar_tick(TCB *tcb, int tick) {
    if (!tcb) return;
    if (tcb->tempo_restante <= 0) return; /* já terminou ou inválido */

    /* marca o primeiro início */
    if (tcb->tempo_inicio == -1) tcb->tempo_inicio = tick;

    /* Atualiza segmentos: se o último segmento termina em 'tick', estende; senão cria novo */
    if (tcb->segs_count > 0) {
        Seg *last = &tcb->segs[tcb->segs_count - 1];
        if (last->start + last->length == tick) {
            /* contíguo: estende em 1 */
            last->length += 1;
        } else {
            /* não contíguo: novo segmento de comprimento 1 */
            tcb_add_segment(tcb, tick, 1);
        }
    } else {
        /* primeiro segmento */
        tcb_add_segment(tcb, tick, 1);
    }

    /* atualizar contadores */
    tcb->tempo_executado += 1;
    tcb->tempo_restante -= 1;

    /* se terminou, registrar fim e estado */
    if (tcb->tempo_restante == 0) {
        tcb->tempo_fim = tick + 1; /* fim do intervalo [tick, tick+1) */
        tcb->estado = FINALIZADA;
    }
}

/* Altera estado do TCB. */
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

/* Libera memória do TCB (incluindo array de segmentos) */
void tcb_destruir(TCB *tcb) {
    if (!tcb) return;
    if (tcb->segs) free(tcb->segs);
    free(tcb);
}
