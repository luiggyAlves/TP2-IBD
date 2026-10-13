// findtitle "<Titulo>" — busca por título percorrendo SEQUENCIALMENTE o
// arquivo de dados, sem índice (Parte 1).
//
// Como o arquivo está organizado por hashing de ID, o título pode estar em
// qualquer bloco e pode se repetir em vários registros: a varredura precisa
// ler o arquivo inteiro, mesmo depois de achar uma ocorrência.
// Custo: todos os blocos do arquivo (cabeçalho + primários + overflow).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "blockio.hpp"
#include "config.hpp"
#include "datafile.hpp"

using namespace tp2;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: findtitle <Titulo>\n");
        return 2;
    }
    const std::string titulo = argv[1];

    try {
        HashFile hf;
        hf.open(data_path());  // cabeçalho: 1 bloco

        std::printf("== findtitle \"%s\" ==\n", titulo.c_str());
        std::printf("arquivo de dados: %s\n", data_path().c_str());

        std::uint64_t encontrados = 0;
        Article a;
        hf.scan([&](const Rid& rid, const std::uint8_t* rec) {
            decode_record(rec, a);
            if (a.titulo == titulo) {  // comparação exata do título completo
                ++encontrados;
                std::printf("registro %llu:\n", static_cast<unsigned long long>(encontrados));
                print_record(a, rid);
            }
        });

        if (encontrados == 0) std::printf("AVISO: nenhum registro com esse titulo foi encontrado.\n");
        else std::printf("registros encontrados: %llu\n", static_cast<unsigned long long>(encontrados));

        const auto lidos = hf.file().stats().reads;
        std::printf("blocos lidos no arquivo de dados: %llu (varredura completa)\n",
                    static_cast<unsigned long long>(lidos));
        std::printf("total de blocos do arquivo de dados: %llu\n",
                    static_cast<unsigned long long>(hf.file().block_count()));
        std::printf("RESUMO findtitle encontrados=%llu blocos_lidos_dados=%llu total_blocos_dados=%llu\n",
                    static_cast<unsigned long long>(encontrados), static_cast<unsigned long long>(lidos),
                    static_cast<unsigned long long>(hf.file().block_count()));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
