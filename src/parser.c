#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

//remover \n e \r
static void trim_newline(char *s){
    char *p = s;
    while(*p){
        if(*p == '\r' || *p == '\n'){
            *p = '\0'; 
            break;
        }
        p++;
    }
}

static int parse_tarefa_line(const char *line,Tarefa *out){
    char buf[512];
    strncpy(buf,line,sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    trim_newline(buf);

    // Tokenizar todos os campos separados por ';'
    char *tokens[64];
    int i = 0;
    char *p = strtok(buf,";");
    while(p && i < (int)(sizeof(tokens)/sizeof(tokens[0]))){
        tokens[i++] = p;
        p = strtok(NULL,";");
    }
    if(i<5){
        return -1; 
    }

    // Mapeia os campos básicos da tarefa
    strncpy(out->id,tokens[0],sizeof(out->id)-1);
    out->id[sizeof(out->id)-1] = '\0';
    strncpy(out->cor,tokens[1],sizeof(out->cor)-1);
    out->cor[sizeof(out->cor)-1] = '\0';
    out->ingresso = atoi(tokens[2]);
    out->duracao = atoi(tokens[3]);
    out->prioridade = atoi(tokens[4]);

    // Inicializa lista de IOs
    out->ios = NULL;
    out->n_ios = 0;
    // Inicializa lista de mutex ops
    out->mops = NULL;
    out->n_mops = 0;

    // Tokens adicionais podem ser operações IO no formato IO:xx-yy
    for(int j = 5; j < i; ++j){
        char *tok = tokens[j];
        if(!tok || tok[0] == '\0') continue;
        if(strncasecmp(tok, "IO:", 3) == 0){
            char *rest = tok + 3;
            int instante = 0, dur = 0;
            if(sscanf(rest, "%d-%d", &instante, &dur) == 2){
                IOOp *na = realloc(out->ios, sizeof(IOOp) * (out->n_ios + 1));
                if(!na){ perror("realloc");
                    // em caso de falha no realloc, liberar o que já tínhamos
                    free(out->ios); out->ios = NULL; out->n_ios = 0;
                    return -1;
                }
                out->ios = na;
                out->ios[out->n_ios].instante = instante;
                out->ios[out->n_ios].duracao = dur;
                out->n_ios += 1;
            } else {
                fprintf(stderr, "Formato de IO inválido: %s\n", tok);
                // tratar como erro de análise
                free(out->ios); out->ios = NULL; out->n_ios = 0;
                return -1;
            }
        } else if (strncasecmp(tok, "ML", 2) == 0 || strncasecmp(tok, "MU", 2) == 0) {
            int id = 0, instante = 0;
            char kind = toupper((unsigned char)tok[0]) == 'M' && toupper((unsigned char)tok[1]) == 'L' ? 'L' : 'U';
            // encontrar ':'
            char *pcol = strchr(tok, ':');
            if (pcol) {
                // analisar número opcional entre ML/MU e ':'
                char numbuf[16];
                int nlen = (int)(pcol - (tok + 2));
                if (nlen <= 0) {
                    // id omitido, padrão 0
                    id = 0;
                } else if (nlen < (int)sizeof(numbuf)) {
                    strncpy(numbuf, tok + 2, nlen);
                    numbuf[nlen] = '\0';
                    id = atoi(numbuf);
                } else {
                    fprintf(stderr, "Formato de mutex inválido (id muito longo): %s\n", tok);
                    free(out->mops); out->mops = NULL; out->n_mops = 0; return -1;
                }
                // analisar instante (obrigatório)
                if (sscanf(pcol + 1, "%d", &instante) != 1) {
                    fprintf(stderr, "Formato de mutex inválido (instante): %s\n", tok);
                    free(out->mops); out->mops = NULL; out->n_mops = 0; return -1;
                }
                MutexOp *nm = realloc(out->mops, sizeof(MutexOp) * (out->n_mops + 1));
                if (!nm) { perror("realloc"); free(out->mops); out->mops = NULL; out->n_mops = 0; return -1; }
                out->mops = nm;
                out->mops[out->n_mops].kind = kind;
                out->mops[out->n_mops].id = id;
                out->mops[out->n_mops].instante = instante;
                out->n_mops += 1;
            } else {
                fprintf(stderr, "Formato de mutex inválido (esperado ML[id]:instante): %s\n", tok);
                free(out->mops); out->mops = NULL; out->n_mops = 0; return -1;
            }
        } else {
            // token desconhecido — ignorar para compatibilidade
            continue;
        }
    }

    return 0;
}

//Lê o arquivo e preenche Config
int parser_ler_arquivo(const char *path,Config *cfg){
    FILE *f = fopen(path,"r");
    if(!f){
        printf("Erro ao abrir arquivo de configuração");
        return -1;
    }
    char line[512];
    //lê primeira linha algoritmo e quantum
    if(!fgets(line, sizeof(line),f)){
        fclose(f);
        fprintf(stderr,"Arquivo vazio\n");
        return -1;
    }
    trim_newline(line);
    char *tok = strtok(line,";");
    if(!tok){
         fclose(f);
          return -1; 
    }
    strncpy(cfg->algoritmo, tok, sizeof(cfg->algoritmo)-1);
    cfg->algoritmo[sizeof(cfg->algoritmo)-1] = '\0';
    // segundo token: quantum (opcional)
    tok = strtok(NULL, ";");
    if (tok) cfg->quantum = atoi(tok); else cfg->quantum = 0;
    // terceiro token: alpha (opcional, usado por PRIOPEnv)
    tok = strtok(NULL, ";");
    if (tok) cfg->alpha = atoi(tok); else cfg->alpha = 0;

    // Lê linhas de tarefas dinamicamente 
    Tarefa *arr = NULL;
    int cap = 0, n = 0;
    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        if (line[0] == '\0') 
            continue; //pular linhas vazias
        if (n >= cap){
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
// Libera memória do parser
void parser_liberar(Config *cfg) {
    if (!cfg) return;
    if (cfg->tarefas) {
        // liberar arrays internos de IO de cada tarefa
        for (int i = 0; i < cfg->n_tarefas; ++i) {
            if (cfg->tarefas[i].ios) {
                free(cfg->tarefas[i].ios);
                cfg->tarefas[i].ios = NULL;
                cfg->tarefas[i].n_ios = 0;
            }
            if (cfg->tarefas[i].mops) {
                free(cfg->tarefas[i].mops);
                cfg->tarefas[i].mops = NULL;
                cfg->tarefas[i].n_mops = 0;
            }
        }
        free(cfg->tarefas);
        cfg->tarefas = NULL;
    }
    cfg->n_tarefas = 0;
}
