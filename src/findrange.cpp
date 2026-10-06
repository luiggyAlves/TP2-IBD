// findrange <ID-inicial> <ID-final> — busca por intervalo de IDs percorrendo sequencialmente o arquivo de dados (Parte 1)
//
// Deve informar, ao final, a quantidade de blocos lidos na operação e os
// totais de blocos dos arquivos envolvidos. Todo acesso a disco deve passar
// pela camada blockio.

#include <cstdio>

#include "blockio.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: findrange <ID-inicial> <ID-final>\n");
        return 2;
    }
    (void)argv;
    std::fprintf(stderr, "findrange ainda nao implementado\n");
    return 1;
}
