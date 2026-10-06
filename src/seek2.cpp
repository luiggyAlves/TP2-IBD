// seek2 <Titulo> — busca pelo indice secundario em B+Tree (Parte 2)
//
// Deve informar, ao final, a quantidade de blocos lidos na operação e os
// totais de blocos dos arquivos envolvidos. Todo acesso a disco deve passar
// pela camada blockio.

#include <cstdio>

#include "blockio.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: seek2 <Titulo>\n");
        return 2;
    }
    (void)argv;
    std::fprintf(stderr, "seek2 ainda nao implementado\n");
    return 1;
}
