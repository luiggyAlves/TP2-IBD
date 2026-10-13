// Testes do arquivo de dados por hashing (datafile.hpp / datafile.cpp).
// Compile e rode com: make test
//
// Cobre: serialização de registros, truncamento, escolha de N, inserção com
// overflow, busca por hashing (inclusive contagem exata de blocos lidos),
// varredura sequencial e leitura direta por RID.

#include <unistd.h>

#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "blockio.hpp"
#include "check.hpp"
#include "datafile.hpp"

using namespace tp2;

namespace {

Article make_article(int id, std::size_t snippet_len) {
    Article a;
    a.id = id;
    a.titulo = "Titulo do artigo " + std::to_string(id);
    a.ano = 2000 + id % 20;
    a.autores = "Autor A|Autor B " + std::to_string(id % 7);
    a.citacoes = id % 100;
    a.atualizacao = "2016-07-28 09:36:29";
    a.snippet = std::string(snippet_len, static_cast<char>('a' + id % 26));
    return a;
}

bool same(const Article& x, const Article& y) {
    return x.id == y.id && x.titulo == y.titulo && x.ano == y.ano && x.autores == y.autores &&
           x.citacoes == y.citacoes && x.atualizacao == y.atualizacao && x.snippet == y.snippet;
}

// --- serialização ---------------------------------------------------------
void test_encode_decode() {
    std::vector<std::uint8_t> buf(kDataPayload);

    // Registro comum, com acentos (UTF-8) e aspas.
    Article a = make_article(42, 300);
    a.titulo = "Ação \"rápida\" em árvores B+";
    CHECK(record_size(a) == kRecordFixedSize + a.titulo.size() + a.autores.size() + a.snippet.size());
    encode_record(a, buf.data());
    Article b;
    CHECK(decode_record(buf.data(), b) == record_size(a));
    CHECK(same(a, b));
    CHECK(peek_record_id(buf.data()) == 42);

    // Campos vazios e data curta (completada com '\0' e recuperada sem lixo).
    Article c;
    c.id = -7;
    c.atualizacao = "NULL";
    encode_record(c, buf.data());
    Article d;
    decode_record(buf.data(), d);
    CHECK(same(c, d));
    CHECK(record_size(c) == kRecordFixedSize);

    // Data maior que 19 bytes é cortada em 19.
    Article e = make_article(1, 10);
    e.atualizacao = "2016-07-28 09:36:29.123456";
    encode_record(e, buf.data());
    Article f;
    decode_record(buf.data(), f);
    CHECK(f.atualizacao == "2016-07-28 09:36:29");
}

// --- truncamento ----------------------------------------------------------
void test_fit_record() {
    Article a = make_article(1, 100);
    CHECK(!fit_record(a, kDataPayload));  // cabe: nada muda
    CHECK(a.snippet.size() == 100);

    Article big = make_article(2, 10000);  // snippet enorme
    CHECK(fit_record(big, kDataPayload));
    CHECK(record_size(big) == kDataPayload);
    CHECK(big.titulo == "Titulo do artigo 2");  // título preservado

    Article huge = make_article(3, 10);
    huge.autores = std::string(10000, 'x');
    huge.titulo = std::string(10000, 't');
    CHECK(fit_record(huge, 200));
    CHECK(record_size(huge) == 200);
    CHECK(huge.snippet.empty());  // snippet é cortado primeiro
}

// --- funções auxiliares do hashing ----------------------------------------
void test_hash_helpers() {
    CHECK(next_prime(0) == 2);
    CHECK(next_prime(2) == 2);
    CHECK(next_prime(33148) == 33149);
    CHECK(next_prime(33150) == 33151);
    CHECK(hash_bucket(10, 7) == 3);
    CHECK(hash_bucket(-1, 7) < 7);  // IDs negativos não geram bucket inválido
    CHECK(bucket_block(0) == 1);    // bloco 0 é o cabeçalho
}

// --- arquivo hash completo -----------------------------------------------
void test_hash_file(const std::string& dir) {
    const std::string path = dir + "/hash.dat";
    const std::uint32_t N = 5;  // poucos buckets: força cadeias de overflow longas
    std::map<int, Article> esperado;
    std::map<int, Rid> rids;

    {
        HashFile hf;
        hf.create(path, N);
        CHECK(hf.file().block_count() == 1 + N);
        std::mt19937 rng(123);
        for (int i = 0; i < 400; ++i) {
            const int id = i * 3 + 1;
            Article a = make_article(id, 50 + rng() % 600);
            rids[id] = hf.insert(a);
            esperado[id] = a;
        }
        const LoadStats s = hf.finish();
        CHECK(s.records == 400);
        CHECK(s.num_buckets == N);
        CHECK(s.overflow_blocks > 0);
        CHECK(s.total_blocks == 1 + N + s.overflow_blocks);
        CHECK(s.data_blocks == N + s.overflow_blocks);
        std::uint32_t soma = 0;
        for (std::size_t k = 0; k < s.chain_len_hist.size(); ++k) soma += s.chain_len_hist[k];
        CHECK(soma == N);
        std::uint32_t soma_recs = 0;
        for (std::size_t k = 0; k < s.bucket_size_hist.size(); ++k)
            soma_recs += static_cast<std::uint32_t>(k) * s.bucket_size_hist[k];
        CHECK(soma_recs == 400);
    }

    HashFile hf;
    hf.open(path);
    CHECK(hf.file().stats().reads == 1);  // abrir lê só o cabeçalho
    CHECK(hf.header().num_buckets == N);
    CHECK(hf.header().num_records == 400);

    // Toda chave inserida é encontrada, no RID devolvido na inserção, e o
    // número de blocos lidos é exatamente a posição do bloco na cadeia.
    for (const auto& [id, art] : esperado) {
        hf.file().reset_stats();
        Article got;
        Rid rid;
        std::uint32_t cadeia = 0;
        CHECK(hf.find(id, got, rid, cadeia));
        CHECK(same(got, art));
        CHECK(rid.block == rids[id].block && rid.offset == rids[id].offset);
        CHECK(hf.file().stats().reads == cadeia);
        CHECK(cadeia >= 1);
        // O bloco onde o registro está pertence ao bucket h(ID).
        std::vector<std::uint8_t> bloco(blockio::kBlockSize);
        hf.file().read_block(rid.block, bloco.data());
        CHECK(get_block_header(bloco.data()).bucket == hash_bucket(id, N));
    }

    // Chave inexistente: lê a cadeia inteira do bucket e não acha.
    {
        hf.file().reset_stats();
        Article got;
        Rid rid;
        std::uint32_t cadeia = 0;
        CHECK(!hf.find(2, got, rid, cadeia));  // IDs inseridos são 1, 4, 7, ...
        CHECK(cadeia >= 2);                    // com N = 5 todo bucket transborda
        CHECK(hf.file().stats().reads == cadeia);
    }

    // Varredura: lê todos os blocos de dados e vê cada registro uma vez.
    {
        hf.file().reset_stats();
        std::set<int> vistos;
        std::size_t total = 0;
        hf.scan([&](const Rid& rid, const std::uint8_t* rec) {
            const int id = peek_record_id(rec);
            vistos.insert(id);
            ++total;
            CHECK(rid.block == rids[id].block && rid.offset == rids[id].offset);
        });
        CHECK(total == 400);
        CHECK(vistos.size() == 400);
        CHECK(hf.file().stats().reads == hf.file().block_count() - 1);
    }

    // Leitura direta por RID (caminho usado pelos índices): 1 bloco.
    {
        blockio::BlockFile f;
        f.open(path, blockio::Mode::kRead);
        Article got;
        CHECK(read_record_at(f, rids[100], got));
        CHECK(same(got, esperado[100]));
        CHECK(f.stats().reads == 1);
        CHECK(!read_record_at(f, Rid{0, 0}, got));                      // bloco 0 é cabeçalho
        CHECK(!read_record_at(f, Rid{9999999, 16}, got));               // fora do arquivo
        // Deslocamento logo após a área ocupada do bloco: não há registro ali.
        std::vector<std::uint8_t> bloco(blockio::kBlockSize);
        f.read_block(rids[100].block, bloco.data());
        const std::uint16_t fim_usado =
            static_cast<std::uint16_t>(kDataBlockHeaderSize + get_block_header(bloco.data()).used);
        CHECK(!read_record_at(f, Rid{rids[100].block, fim_usado}, got));
    }
    std::remove(path.c_str());
}

// --- arquivo com BLOCK_SIZE diferente é recusado ---------------------------
void test_wrong_block_size(const std::string& dir) {
    const std::string path = dir + "/bad.dat";
    {
        HashFile hf;
        hf.create(path, 3);
        hf.finish();
    }
    // Adultera o campo block_size do cabeçalho.
    {
        blockio::BlockFile f;
        f.open(path, blockio::Mode::kReadWrite);
        std::vector<std::uint8_t> b(blockio::kBlockSize);
        f.read_block(0, b.data());
        b[8] ^= 0xFF;
        f.write_block(0, b.data());
    }
    bool lancou = false;
    try {
        HashFile hf;
        hf.open(path);
    } catch (const std::exception&) {
        lancou = true;
    }
    CHECK(lancou);
    std::remove(path.c_str());
}

}  // namespace

int main() {
    const std::string dir = testing::temp_dir();
    test_encode_decode();
    test_fit_record();
    test_hash_helpers();
    test_hash_file(dir);
    test_wrong_block_size(dir);
    ::rmdir(dir.c_str());
    return testing::tests_done("test_datafile");
}
