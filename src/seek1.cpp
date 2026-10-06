// seek1 <ID> — busca pelo indice primario em B+Tree (Parte 2)
//
// Deve informar, ao final, a quantidade de blocos lidos na operação e os
// totais de blocos dos arquivos envolvidos. Todo acesso a disco deve passar
// pela camada blockio.

#include <cstdio>

#include "blockio.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: seek1 <ID>\n");
        return 2;
    }
    (void)argv;
    std::fprintf(stderr, "seek1 ainda nao implementado\n");
    return 1;
}
