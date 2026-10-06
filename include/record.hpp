// Representação de um registro de artigo, tal como lido do CSV de entrada.
// A forma como esse registro é gravado no arquivo de dados paginado é uma
// decisão de projeto de quem implementa (ver Seção 7.1 do enunciado).

#ifndef RECORD_HPP
#define RECORD_HPP

#include <string>

struct Article {
    int id = 0;
    std::string titulo;
    int ano = 0;
    std::string autores;      // separados por '|'
    int citacoes = 0;
    std::string atualizacao;  // "AAAA-MM-DD HH:MM:SS"
    std::string snippet;
};

#endif  // RECORD_HPP
