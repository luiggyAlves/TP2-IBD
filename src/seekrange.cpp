// seekrange <ID-inicial> <ID-final> — busca por intervalo percorrendo as folhas encadeadas do indice primario (Parte 2)
//
// Deve informar, ao final, a quantidade de blocos lidos na operação e os
// totais de blocos dos arquivos envolvidos. Todo acesso a disco deve passar
// pela camada blockio.

#include <cstdio>

#include "blockio.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: seekrange <ID-inicial> <ID-final>\n");
        return 2;
    }
    (void)argv;
    std::fprintf(stderr, "seekrange ainda nao implementado\n");
    return 1;
}
