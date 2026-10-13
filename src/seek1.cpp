// seek1 <ID> — busca por ID pelo índice primário em B+Tree (Parte 2).
//
// Caminho: raiz (bloco 0 do índice) -> ... -> folha que deve conter o ID;
// se a entrada (ID, RID) existir, lê o bloco RID.block do arquivo de dados.
// Custo esperado: altura da árvore (índice) + 1 (dados) se o ID existir;
// apenas a altura, se não existir.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "args.hpp"
#include "blockio.hpp"
#include "bptree.hpp"
#include "config.hpp"
#include "datafile.hpp"

using namespace tp2;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: seek1 <ID>\n");
        return 2;
    }
    int id = 0;
    if (!parse_id(argv[1], id)) {
        std::fprintf(stderr, "erro: ID invalido: '%s' (esperado um inteiro de 32 bits)\n", argv[1]);
        return 2;
    }

    try {
        BPlusTree idx;
        idx.open(primary_index_path());
        blockio::BlockFile dados;
        dados.open(data_path(), blockio::Mode::kRead);  // abrir não lê blocos

        // 1) Percorre a B+Tree até a folha.
        std::uint8_t chave[kIdKeySize];
        encode_id_key(id, chave);
        Rid rid;
        const bool no_indice = idx.find(chave, rid);

        // 2) Se achou, vai direto ao bloco do arquivo de dados indicado pelo RID.
        Article a;
        bool achou = false;
        if (no_indice) {
            achou = read_record_at(dados, rid, a) && a.id == id;
            if (!achou) std::fprintf(stderr, "erro: indice aponta para RID invalido (refaca o upload)\n");
        }

        std::printf("== seek1 %d ==\n", id);
        std::printf("indice primario : %s (altura %u)\n", idx.path().c_str(), idx.height());
        std::printf("arquivo de dados: %s\n", data_path().c_str());
        if (achou) {
            std::printf("registro encontrado:\n");
            print_record(a, rid);
        } else {
            std::printf("AVISO: nenhum registro com ID %d foi encontrado.\n", id);
        }
        const auto li = idx.stats().reads, ld = dados.stats().reads;
        std::printf("blocos lidos no indice primario : %llu\n", static_cast<unsigned long long>(li));
        std::printf("blocos lidos no arquivo de dados: %llu\n", static_cast<unsigned long long>(ld));
        std::printf("total de blocos lidos           : %llu\n", static_cast<unsigned long long>(li + ld));
        std::printf("total de blocos do indice primario : %llu\n",
                    static_cast<unsigned long long>(idx.block_count()));
        std::printf("total de blocos do arquivo de dados: %llu\n",
                    static_cast<unsigned long long>(dados.block_count()));
        std::printf("RESUMO seek1 id=%d encontrados=%d blocos_lidos_indice=%llu blocos_lidos_dados=%llu "
                    "total_blocos_indice=%llu total_blocos_dados=%llu altura=%u\n",
                    id, achou ? 1 : 0, static_cast<unsigned long long>(li), static_cast<unsigned long long>(ld),
                    static_cast<unsigned long long>(idx.block_count()),
                    static_cast<unsigned long long>(dados.block_count()), idx.height());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
