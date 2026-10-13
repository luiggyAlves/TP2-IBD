// seekrange <ID-inicial> <ID-final> — busca por intervalo fechado de IDs
// pelo índice primário em B+Tree (Parte 2).
//
// Caminho: desce da raiz até a folha da primeira chave >= ID-inicial e, a
// partir dela, percorre sequencialmente as folhas encadeadas (ponteiro next)
// até encontrar uma chave > ID-final. Para cada entrada do intervalo, lê o
// registro no arquivo de dados pelo RID. Os registros saem naturalmente em
// ordem crescente de ID, porque as folhas estão ordenadas.
//
// Custo esperado:
//   índice = altura + (folhas adicionais percorridas)
//   dados  = 1 bloco por registro do intervalo (o hashing espalha os
//            registros; não há reaproveitamento de bloco entre registros)

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "args.hpp"
#include "blockio.hpp"
#include "bptree.hpp"
#include "config.hpp"
#include "datafile.hpp"

using namespace tp2;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: seekrange <ID-inicial> <ID-final>\n");
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
        BPlusTree idx;
        idx.open(primary_index_path());
        blockio::BlockFile dados;
        dados.open(data_path(), blockio::Mode::kRead);

        std::printf("== seekrange [%d, %d] ==\n", ini, fim);
        std::printf("indice primario : %s\n", idx.path().c_str());
        std::printf("arquivo de dados: %s\n", data_path().c_str());
        std::printf("registros (ID | titulo | ano | autores | citacoes | atualizacao | snippet):\n");

        std::uint8_t chave[kIdKeySize];
        encode_id_key(ini, chave);
        std::uint64_t encontrados = 0, folhas = 0;
        std::uint32_t folha_atual = UINT32_MAX;
        Article a;
        for (BPlusTree::Cursor c = idx.lower_bound(chave); c.valid; idx.next(c)) {
            if (c.leaf != folha_atual) {  // só para o relatório: folhas visitadas
                folha_atual = c.leaf;
                ++folhas;
            }
            const int id = decode_id_key(idx.key_at(c));
            if (id > fim) break;  // ultrapassou o limite final
            if (!read_record_at(dados, idx.rid_at(c), a) || a.id != id)
                throw std::runtime_error("indice aponta para RID invalido (refaca o upload)");
            print_record_line(a);
            ++encontrados;
        }

        if (encontrados == 0) std::printf("AVISO: nenhum registro com ID no intervalo [%d, %d].\n", ini, fim);
        else std::printf("registros encontrados: %llu\n", static_cast<unsigned long long>(encontrados));

        const auto li = idx.stats().reads, ld = dados.stats().reads;
        std::printf("blocos lidos no indice primario : %llu (altura %u; folhas com entradas visitadas: %llu)\n",
                    static_cast<unsigned long long>(li), idx.height(), static_cast<unsigned long long>(folhas));
        std::printf("blocos lidos no arquivo de dados: %llu\n", static_cast<unsigned long long>(ld));
        std::printf("total de blocos lidos           : %llu\n", static_cast<unsigned long long>(li + ld));
        std::printf("total de blocos do indice primario : %llu\n",
                    static_cast<unsigned long long>(idx.block_count()));
        std::printf("total de blocos do arquivo de dados: %llu\n",
                    static_cast<unsigned long long>(dados.block_count()));
        std::printf("RESUMO seekrange ini=%d fim=%d encontrados=%llu blocos_lidos_indice=%llu blocos_lidos_dados=%llu "
                    "total_blocos_indice=%llu total_blocos_dados=%llu altura=%u\n",
                    ini, fim, static_cast<unsigned long long>(encontrados), static_cast<unsigned long long>(li),
                    static_cast<unsigned long long>(ld), static_cast<unsigned long long>(idx.block_count()),
                    static_cast<unsigned long long>(dados.block_count()), idx.height());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
