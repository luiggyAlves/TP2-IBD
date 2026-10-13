// config.hpp — constantes de projeto e caminhos dos arquivos do TP2.
//
// Tudo o que é "decisão de projeto" e aparece em mais de um programa fica aqui,
// para que o relatório (Seção 7.1) possa apontar um único lugar no código.

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstddef>
#include <cstdlib>
#include <string>

namespace tp2 {

// ---------------------------------------------------------------------------
// Parâmetros de projeto
// ---------------------------------------------------------------------------

// Quantos bytes do título entram na chave do índice secundário (B+Tree de
// Título). A chave tem tamanho fixo para que a ordem da árvore possa ser
// calculada a partir de BLOCK_SIZE. Títulos maiores que isso são truncados
// NA CHAVE (nunca no arquivo de dados); a busca confere o título completo no
// registro lido do arquivo de dados, então o resultado continua exato.
// 64 bytes: medido sobre a amostra, só 457 prefixos distintos de 64 bytes são
// compartilhados por títulos diferentes (ver relatório, Seção 7.1).
constexpr std::size_t kTitlePrefixLen = 64;

// Taxa de ocupação alvo do hashing estático. O número de buckets N é escolhido
// na carga como o menor primo >= (bytes totais dos registros) /
// (bytes úteis por bloco * kHashLoadFactor). Valores menores reduzem overflow
// e aumentam o arquivo; valores maiores fazem o contrário.
constexpr double kHashLoadFactor = 0.80;

// ---------------------------------------------------------------------------
// Caminhos dos arquivos persistentes
// ---------------------------------------------------------------------------

// Diretório onde ficam o arquivo de dados e os arquivos de índice.
// Padrão: /data/db (volume montado no contêiner, ver data/README.md).
// Pode ser trocado pela variável de ambiente TP2_DB_DIR — útil para rodar os
// binários fora do Docker (ex.: TP2_DB_DIR=data/db ./bin/findrec 5).
inline std::string db_dir() {
    const char* env = std::getenv("TP2_DB_DIR");
    if (env != nullptr && env[0] != '\0') return env;
    return "/data/db";
}

// Arquivo de dados organizado por hashing (Parte 1 e Parte 2).
inline std::string data_path() { return db_dir() + "/artigos.dat"; }

// Índice primário: B+Tree sobre o ID.
inline std::string primary_index_path() { return db_dir() + "/idx_id.bpt"; }

// Índice secundário: B+Tree sobre o Título.
inline std::string secondary_index_path() { return db_dir() + "/idx_titulo.bpt"; }

// Rastreamento: com TP2_TRACE=1 no ambiente, os programas imprimem em stderr
// cada bloco lido nas buscas (útil para conferir as contagens à mão).
inline bool trace_enabled() {
    static const bool ligado = [] {
        const char* env = std::getenv("TP2_TRACE");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    return ligado;
}

}  // namespace tp2

#endif  // CONFIG_HPP
