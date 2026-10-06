// findtitle <Titulo> — busca por titulo percorrendo sequencialmente o arquivo de dados (Parte 1)
//
// Deve informar, ao final, a quantidade de blocos lidos na operação e os
// totais de blocos dos arquivos envolvidos. Todo acesso a disco deve passar
// pela camada blockio.

#include <cstdio>

#include "blockio.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: findtitle <Titulo>\n");
        return 2;
    }
    (void)argv;
    std::fprintf(stderr, "findtitle ainda nao implementado\n");
    return 1;
}
