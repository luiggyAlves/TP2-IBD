// findrange <ID-inicial> <ID-final> — busca por intervalo fechado de IDs
// percorrendo SEQUENCIALMENTE o arquivo de dados (Parte 1).
//
// O hashing espalha IDs vizinhos por buckets diferentes e não guarda nenhuma
// ordem, então não há como saber onde o intervalo começa ou termina: todos
// os blocos precisam ser lidos. Os registros encontrados são ordenados por ID
// em memória apenas para a exibição (isso não lê nenhum bloco).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "args.hpp"
#include "blockio.hpp"
#include "config.hpp"
#include "datafile.hpp"

using namespace tp2;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: findrange <ID-inicial> <ID-final>\n");
        return 2;
    }
    int ini = 0, fim = 0;
    if (!parse_id(argv[1], ini) || !parse_id(argv[2], fim)) {
        std::fprintf(stderr, "erro: os limites do intervalo devem ser inteiros de 32 bits\n");
        return 2;
    }
    if (ini > fim) {
        std::fprintf(stderr, "erro: intervalo invalido: ID-inicial (%d) > ID-final (%d)\n", ini, fim);
        return 2;
    }

    try {
        HashFile hf;
        hf.open(data_path());  // cabeçalho: 1 bloco

        std::vector<Article> achados;
        hf.scan([&](const Rid&, const std::uint8_t* rec) {
            const int id = peek_record_id(rec);
            if (id >= ini && id <= fim) {
                Article a;
                decode_record(rec, a);
                achados.push_back(std::move(a));
            }
        });
        std::sort(achados.begin(), achados.end(), [](const Article& x, const Article& y) { return x.id < y.id; });

        std::printf("== findrange [%d, %d] ==\n", ini, fim);
        std::printf("arquivo de dados: %s\n", data_path().c_str());
        std::printf("registros (ID | titulo | ano | autores | citacoes | atualizacao | snippet):\n");
        for (const Article& a : achados) print_record_line(a);
        if (achados.empty()) std::printf("AVISO: nenhum registro com ID no intervalo [%d, %d].\n", ini, fim);
        else std::printf("registros encontrados: %zu\n", achados.size());

        const auto lidos = hf.file().stats().reads;
        std::printf("blocos lidos no arquivo de dados: %llu (varredura completa)\n",
                    static_cast<unsigned long long>(lidos));
        std::printf("total de blocos do arquivo de dados: %llu\n",
                    static_cast<unsigned long long>(hf.file().block_count()));
        std::printf("RESUMO findrange ini=%d fim=%d encontrados=%zu blocos_lidos_dados=%llu total_blocos_dados=%llu\n",
                    ini, fim, achados.size(), static_cast<unsigned long long>(lidos),
                    static_cast<unsigned long long>(hf.file().block_count()));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
