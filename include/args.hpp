// args.hpp — leitura dos argumentos numéricos (IDs) da linha de comando.
//
// O ID é um int de 32 bits no arquivo de dados e nas chaves do índice. Um
// argumento fora dessa faixa (ex.: 9999999999) é RECUSADO, em vez de ser
// convertido silenciosamente para outro número.

#ifndef ARGS_HPP
#define ARGS_HPP

#include <cerrno>
#include <climits>
#include <cstdlib>

namespace tp2 {

// Converte 's' para int. Devolve false se 's' não for um inteiro completo
// (ex.: "12abc", "") ou se estiver fora de [INT_MIN, INT_MAX].
inline bool parse_id(const char* s, int& out) {
    char* fim = nullptr;
    errno = 0;
    const long long v = std::strtoll(s, &fim, 10);
    if (fim == s || *fim != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) return false;
    out = static_cast<int>(v);
    return true;
}

}  // namespace tp2

#endif  // ARGS_HPP
