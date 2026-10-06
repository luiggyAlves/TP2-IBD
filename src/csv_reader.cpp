#include "csv_reader.hpp"

#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace {

// Divide uma linha do CSV em campos, respeitando aspas e o escape "".
// Devolve false se a linha não tiver o formato esperado.
bool split_fields(const std::string& line, std::vector<std::string>& out) {
    out.clear();
    std::string campo;
    bool dentro_de_aspas = false;
    bool campo_iniciado = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (dentro_de_aspas) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {  // aspas escapada
                    campo.push_back('"');
                    ++i;
                } else {
                    dentro_de_aspas = false;
                }
            } else {
                campo.push_back(c);
            }
        } else if (c == '"') {
            dentro_de_aspas = true;
            campo_iniciado = true;
        } else if (c == ';') {
            out.push_back(campo);
            campo.clear();
            campo_iniciado = false;
        } else if (c != '\r') {
            campo.push_back(c);
        }
    }
    if (dentro_de_aspas) return false;  // linha continua na próxima (não esperado)
    if (campo_iniciado || !campo.empty() || !out.empty()) out.push_back(campo);
    return true;
}

int to_int(const std::string& s) { return std::atoi(s.c_str()); }

}  // namespace

CsvReader::CsvReader(const std::string& path) : in_(path), path_(path) {
    if (!in_) throw std::runtime_error("nao foi possivel abrir o CSV de entrada: " + path);
}

bool CsvReader::next(Article& out) {
    std::string line;
    std::vector<std::string> campos;
    while (std::getline(in_, line)) {
        ++lines_;
        if (line.empty()) continue;
        if (!split_fields(line, campos) || campos.size() < 7) {
            ++skipped_;
            continue;
        }
        out.id = to_int(campos[0]);
        out.titulo = campos[1];
        out.ano = to_int(campos[2]);
        out.autores = campos[3];
        out.citacoes = to_int(campos[4]);
        out.atualizacao = campos[5];
        out.snippet = campos[6];
        return true;
    }
    return false;
}
