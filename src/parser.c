/*
 * src/parser.c
 * Lê config: 1ª linha algoritmo;quantum, demais linhas tarefas.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

/* Remove \n/\r do final. */
static void trim_newline(char *s) {
    char *p = s;
    while (*p) {
        if (*p == '\r' || *p == '\n') { *p = '\0'; break; }
        p++;
    }
}

/* Parse de uma linha de tarefa (campos separados por ';'). */
static int parse_tarefa_line(const char *line, Tarefa *out) {
    char buf[512];
    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    trim_newline(buf);

    /* Tokenizar até 6 campos (lista_eventos pode ser vazia) */
    char *tokens[6];
    int i = 0;
    char *p = strtok(buf, ";");
    while (p && i < 6) {
        tokens[i++] = p;
        p = strtok(NULL, ";");
    }
    if (i < 5) return -1; /* campos insuficientes */

    /* Mapear campos para estrutura Tarefa */
    /* id pode ter formato como 't01' -> copiar como string */
    strncpy(out->id, tokens[0], sizeof(out->id)-1);
    out->id[sizeof(out->id)-1] = '\0';
    strncpy(out->cor, tokens[1], sizeof(out->cor)-1);
    out->cor[sizeof(out->cor)-1] = '\0';
    out->ingresso = atoi(tokens[2]);
    out->duracao = atoi(tokens[3]);
    out->prioridade = atoi(tokens[4]);

    return 0;
}

/* Lê o arquivo e preenche Config. */
int parser_ler_arquivo(const char *path, Config *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("Erro ao abrir arquivo de configuração");
        return -1;
    }
    char line[512];

    /* lê primeira linha: algoritmo;quantum */
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        fprintf(stderr, "Arquivo vazio\n");
        return -1;
    }
    trim_newline(line);
    char *tok = strtok(line, ";");
    if (!tok) { fclose(f); return -1; }
    strncpy(cfg->algoritmo, tok, sizeof(cfg->algoritmo)-1);
    cfg->algoritmo[sizeof(cfg->algoritmo)-1] = '\0';
    tok = strtok(NULL, ";");
    if (tok) cfg->quantum = atoi(tok); else cfg->quantum = 0;

    /* Lê linhas de tarefas dinamicamente */
    Tarefa *arr = NULL;
    int cap = 0, n = 0;
    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        if (line[0] == '\0') continue; /* pula linhas vazias */
        if (n >= cap) {
            int nc = cap == 0 ? 8 : cap*2;
            Tarefa *na = realloc(arr, sizeof(Tarefa) * nc);
            if (!na) { perror("realloc"); fclose(f); return -1; }
            arr = na; cap = nc;
        }
        if (parse_tarefa_line(line, &arr[n]) != 0) {
            fprintf(stderr, "Linha de tarefa inválida: %s\n", line);
            free(arr); fclose(f); return -1;
        }
        n++;
    }
    fclose(f);

    cfg->tarefas = arr;
    cfg->n_tarefas = n;
    return 0;
}

/* Libera memória do parser. */
void parser_liberar(Config *cfg) {
    if (!cfg) return;
    if (cfg->tarefas) {
        free(cfg->tarefas);
        cfg->tarefas = NULL;
    }
    cfg->n_tarefas = 0;
}
