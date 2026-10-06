// upload <arquivo.csv> — carga inicial dos dados (Parte 1 do enunciado).
//
// Deve criar o arquivo de dados organizado por hashing e informar ao final:
// registros carregados, blocos do arquivo de dados, registros por bloco no
// layout adotado e taxa de ocupação dos blocos.
//
// O trecho abaixo existe apenas para mostrar o uso das duas peças fornecidas
// (CsvReader e blockio::BlockFile). Substitua-o pela sua implementação.

#include <cstdio>
#include <string>

#include "blockio.hpp"
#include "csv_reader.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s <arquivo.csv>\n", argv[0]);
        return 2;
    }
    const std::string csv_path = argv[1];
    const std::string data_path = "/data/db/artigos.dat";

    try {
        CsvReader reader(csv_path);
        Article a;
        std::uint64_t n = 0;
        while (reader.next(a)) {
            ++n;
            // TODO: gravar o registro no arquivo de dados por hashing.
        }
        std::printf("registros lidos do CSV: %llu\n", static_cast<unsigned long long>(n));
        std::printf("linhas ignoradas: %llu\n", static_cast<unsigned long long>(reader.skipped()));
        std::printf("tamanho do bloco: %zu bytes\n", blockio::kBlockSize);
        std::printf("arquivo de dados previsto: %s\n", data_path.c_str());
        std::fprintf(stderr, "upload ainda nao implementado\n");
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
