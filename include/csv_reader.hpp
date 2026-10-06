// Leitor do arquivo-texto de entrada (artigo-amostra.csv).
//
// O CSV usa ';' como separador e aspas duplas delimitando cada campo.
// Aspas internas aparecem duplicadas ("").
//
// Este leitor serve apenas à carga inicial: ele não deve ser usado pelos
// programas de consulta, que acessam o arquivo de dados pela camada blockio.

#ifndef CSV_READER_HPP
#define CSV_READER_HPP

#include <fstream>
#include <string>

#include "record.hpp"

class CsvReader {
  public:
    explicit CsvReader(const std::string& path);

    // Lê o próximo registro. Devolve false no fim do arquivo.
    // Linhas malformadas são puladas e contabilizadas em skipped().
    bool next(Article& out);

    std::uint64_t lines_read() const { return lines_; }
    std::uint64_t skipped() const { return skipped_; }

  private:
    std::ifstream in_;
    std::string path_;
    std::uint64_t lines_ = 0;
    std::uint64_t skipped_ = 0;
};

#endif  // CSV_READER_HPP
