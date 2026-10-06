// findrec <ID> — busca no arquivo de dados pela organizacao por hashing (Parte 1)
//
// Deve informar, ao final, a quantidade de blocos lidos na operação e os
// totais de blocos dos arquivos envolvidos. Todo acesso a disco deve passar
// pela camada blockio.

#include <cstdio>

#include "blockio.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: findrec <ID>\n");
        return 2;
    }
    (void)argv;
    std::fprintf(stderr, "findrec ainda nao implementado\n");
    return 1;
}
