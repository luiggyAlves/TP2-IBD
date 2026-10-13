// findrec <ID> — busca por ID diretamente no arquivo de dados, pela
// organização por hashing (Parte 1).
//
// Custo esperado (blocos lidos no arquivo de dados):
//   1 (cabeçalho, de onde vem N) + 1 (bloco primário do bucket h(ID))
//   + blocos de overflow percorridos até achar o ID (ou até o fim da cadeia,
//   se o ID não existir).

#include <cstdio>
#include <cstdlib>
#include <string>

#include "args.hpp"
#include "blockio.hpp"
#include "config.hpp"
#include "datafile.hpp"

using namespace tp2;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: findrec <ID>\n");
        return 2;
    }
    int id = 0;
    if (!parse_id(argv[1], id)) {
        std::fprintf(stderr, "erro: ID invalido: '%s' (esperado um inteiro de 32 bits)\n", argv[1]);
        return 2;
    }

    try {
        HashFile hf;
        hf.open(data_path());  // lê o cabeçalho: 1 bloco
        const std::uint32_t n = hf.header().num_buckets;
        const std::uint32_t bucket = hash_bucket(id, n);

        Article a;
        Rid rid;
        std::uint32_t cadeia = 0;
        const bool achou = hf.find(id, a, rid, cadeia);

        std::printf("== findrec %d ==\n", id);
        std::printf("arquivo de dados: %s\n", data_path().c_str());
        std::printf("hashing: h(%d) = %d mod %u = bucket %u (bloco primario %u)\n", id, id, n, bucket,
                    bucket_block(bucket));
        if (achou) {
            std::printf("registro encontrado:\n");
            print_record(a, rid);
        } else {
            std::printf("AVISO: nenhum registro com ID %d foi encontrado.\n", id);
        }
        const auto lidos = hf.file().stats().reads;
        std::printf("blocos lidos no arquivo de dados: %llu (1 cabecalho + %u do bucket: 1 primario + %u overflow)\n",
                    static_cast<unsigned long long>(lidos), cadeia, cadeia > 0 ? cadeia - 1 : 0);
        std::printf("total de blocos do arquivo de dados: %llu\n",
                    static_cast<unsigned long long>(hf.file().block_count()));
        std::printf("RESUMO findrec id=%d encontrados=%d blocos_lidos_dados=%llu total_blocos_dados=%llu\n", id,
                    achou ? 1 : 0, static_cast<unsigned long long>(lidos),
                    static_cast<unsigned long long>(hf.file().block_count()));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
