// seek2 "<Titulo>" — busca por título pelo índice secundário em B+Tree (Parte 2).
//
// A chave do índice é (primeiros 64 bytes do título, RID). Para buscar:
//   1. monta a chave (prefixo do título, RID 0) — menor que qualquer entrada
//      com esse prefixo — e desce até a primeira entrada >= ela;
//   2. percorre as folhas encadeadas enquanto o prefixo for igual: cada
//      entrada é um CANDIDATO (títulos repetidos aparecem lado a lado);
//   3. para cada candidato, lê o registro no arquivo de dados (1 bloco) e
//      confere o título COMPLETO. Candidatos cujo título só coincide nos
//      64 primeiros bytes são descartados (falsos positivos do prefixo).
// Custo esperado: altura + folhas extras percorridas (índice) +
// 1 bloco por candidato (dados).

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "blockio.hpp"
#include "bptree.hpp"
#include "config.hpp"
#include "datafile.hpp"

using namespace tp2;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: seek2 <Titulo>\n");
        return 2;
    }
    const std::string titulo = argv[1];

    try {
        BPlusTree idx;
        idx.open(secondary_index_path());
        blockio::BlockFile dados;
        dados.open(data_path(), blockio::Mode::kRead);

        std::printf("== seek2 \"%s\" ==\n", titulo.c_str());
        std::printf("indice secundario: %s\n", secondary_index_path().c_str());
        std::printf("arquivo de dados : %s\n", data_path().c_str());

        std::vector<std::uint8_t> chave(kTitleKeySize);
        encode_title_search_key(titulo, chave.data());

        std::uint64_t candidatos = 0, encontrados = 0, falsos = 0;
        Article a;
        // Desce até a primeira entrada >= (prefixo, RID 0) e segue as folhas.
        for (BPlusTree::Cursor c = idx.lower_bound(chave.data()); c.valid; idx.next(c)) {
            if (!title_prefix_equal(idx.key_at(c), chave.data())) break;  // passou do prefixo: acabou
            ++candidatos;
            const Rid rid = idx.rid_at(c);
            if (!read_record_at(dados, rid, a)) throw std::runtime_error("indice aponta para RID invalido");
            if (a.titulo == titulo) {
                ++encontrados;
                std::printf("registro %llu:\n", static_cast<unsigned long long>(encontrados));
                print_record(a, rid);
            } else {
                ++falsos;  // mesmo prefixo de 64 bytes, título diferente
            }
        }

        if (encontrados == 0) std::printf("AVISO: nenhum registro com esse titulo foi encontrado.\n");
        else std::printf("registros encontrados: %llu\n", static_cast<unsigned long long>(encontrados));
        std::printf("entradas do indice com o mesmo prefixo: %llu (descartadas apos conferir o titulo: %llu)\n",
                    static_cast<unsigned long long>(candidatos), static_cast<unsigned long long>(falsos));

        const auto li = idx.stats().reads, ld = dados.stats().reads;
        std::printf("blocos lidos no indice secundario: %llu (altura %u)\n", static_cast<unsigned long long>(li),
                    idx.height());
        std::printf("blocos lidos no arquivo de dados : %llu\n", static_cast<unsigned long long>(ld));
        std::printf("total de blocos lidos            : %llu\n", static_cast<unsigned long long>(li + ld));
        std::printf("total de blocos do indice secundario: %llu\n",
                    static_cast<unsigned long long>(idx.block_count()));
        std::printf("total de blocos do arquivo de dados : %llu\n",
                    static_cast<unsigned long long>(dados.block_count()));
        std::printf("RESUMO seek2 encontrados=%llu candidatos=%llu blocos_lidos_indice=%llu blocos_lidos_dados=%llu "
                    "total_blocos_indice=%llu total_blocos_dados=%llu altura=%u\n",
                    static_cast<unsigned long long>(encontrados), static_cast<unsigned long long>(candidatos),
                    static_cast<unsigned long long>(li), static_cast<unsigned long long>(ld),
                    static_cast<unsigned long long>(idx.block_count()),
                    static_cast<unsigned long long>(dados.block_count()), idx.height());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
