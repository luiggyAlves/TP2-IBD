// Testes da B+Tree em disco (bptree.hpp / bptree.cpp).
// Compile e rode com: make test
//
// Estratégia: inserir chaves na árvore e, em paralelo, num std::map (que serve
// de "gabarito"); depois conferir buscas exatas, buscas por intervalo e a
// estrutura física da árvore (ordenação, limites dos separadores, ocupação
// mínima dos nós, todas as folhas na mesma profundidade, encadeamento das
// folhas e contagem de blocos lidos).

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "blockio.hpp"
#include "bptree.hpp"
#include "check.hpp"

using namespace tp2;
using Key = std::vector<std::uint8_t>;

namespace {

Key id_key(int id) {
    Key k(kIdKeySize);
    encode_id_key(id, k.data());
    return k;
}

// ---------------------------------------------------------------------------
// Validação estrutural completa da árvore (lê todos os nós).
// ---------------------------------------------------------------------------
struct Validator {
    BPlusTree& t;
    std::size_t leaf_cap, int_cap;
    std::uint32_t leaves = 0, internals = 0;
    int leaf_depth = -1;

    // Confere o nó 'blk' e sua subárvore. 'lo'/'hi' são os separadores que o
    // limitam (vazio = sem limite): toda chave k deve satisfazer lo <= k < hi.
    void visit(std::uint32_t blk, int depth, const Key& lo, const Key& hi) {
        const BPlusTree::Node n = t.read_node(blk);
        const std::size_t ks = t.key_size();
        const std::size_t nk = n.keys.size() / ks;
        auto key = [&](std::size_t i) { return Key(n.keys.begin() + i * ks, n.keys.begin() + (i + 1) * ks); };

        for (std::size_t i = 0; i < nk; ++i) {
            if (i > 0) CHECK(key(i - 1) < key(i));  // ordem estrita dentro do nó
            if (!lo.empty()) CHECK(!(key(i) < lo));
            if (!hi.empty()) CHECK(key(i) < hi);
        }
        if (n.leaf) {
            ++leaves;
            CHECK(n.rids.size() == nk);
            if (leaf_depth < 0) leaf_depth = depth;
            CHECK(depth == leaf_depth);  // todas as folhas no mesmo nível
            if (blk != 0) CHECK(nk >= (leaf_cap + 1) / 2);  // ocupação mínima (exceto raiz)
            CHECK(nk <= leaf_cap);
        } else {
            ++internals;
            CHECK(n.children.size() == nk + 1);
            CHECK(nk >= 1);
            if (blk != 0) CHECK(nk >= int_cap / 2);
            CHECK(nk <= int_cap);
            for (std::size_t i = 0; i <= nk; ++i) {
                const Key clo = i == 0 ? lo : key(i - 1);
                const Key chi = i == nk ? hi : key(i);
                visit(n.children[i], depth + 1, clo, chi);
            }
        }
    }
};

// Confere a árvore inteira contra o gabarito.
void validate_tree(BPlusTree& t, const std::map<Key, Rid>& gabarito, std::size_t leaf_cap, std::size_t int_cap) {
    Validator v{t, leaf_cap, int_cap};
    v.visit(0, 1, Key{}, Key{});
    CHECK(static_cast<std::uint32_t>(v.leaf_depth) == t.height());
    CHECK(v.leaves + v.internals == t.block_count());  // nenhum bloco perdido
    CHECK(t.entries() == gabarito.size());

    // Encadeamento: percorrer as folhas da esquerda para a direita devolve
    // todas as entradas, em ordem, uma única vez.
    Key menor(t.key_size(), 0);
    auto it = gabarito.begin();
    std::size_t n = 0;
    for (BPlusTree::Cursor c = t.lower_bound(menor.data()); c.valid; t.next(c), ++it, ++n) {
        if (it == gabarito.end()) break;
        CHECK(std::memcmp(t.key_at(c), it->first.data(), t.key_size()) == 0);
        CHECK(t.rid_at(c).block == it->second.block && t.rid_at(c).offset == it->second.offset);
    }
    CHECK(n == gabarito.size());
}

// ---------------------------------------------------------------------------
// Testes
// ---------------------------------------------------------------------------

void test_capacities() {
    const std::size_t B = blockio::kBlockSize;
    CHECK(BPlusTree::leaf_capacity(4) == (B - 32) / 10);
    CHECK(BPlusTree::internal_capacity(4) == (B - 36) / 8);
    CHECK(BPlusTree::leaf_capacity(kTitleKeySize) == (B - 32) / (kTitleKeySize + 6));
    CHECK(BPlusTree::internal_capacity(kTitleKeySize) == (B - 36) / (kTitleKeySize + 4));
    if (B == 4096) {  // valores citados no relatório
        CHECK(BPlusTree::leaf_capacity(4) == 406);
        CHECK(BPlusTree::internal_capacity(4) == 507);
        CHECK(BPlusTree::leaf_capacity(70) == 53);
        CHECK(BPlusTree::internal_capacity(70) == 54);
    }
    // Um nó cheio sempre cabe no bloco.
    for (std::size_t ks : {std::size_t{4}, std::size_t{70}}) {
        CHECK(32 + BPlusTree::leaf_capacity(ks) * (ks + 6) <= B);
        CHECK(32 + BPlusTree::internal_capacity(ks) * (ks + 4) + 4 <= B);
    }
}

void test_key_encoding() {
    // memcmp das chaves codificadas deve ordenar como os inteiros.
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> d(-2000000000, 2000000000);
    for (int i = 0; i < 20000; ++i) {
        const int a = d(rng), b = i % 3 == 0 ? a : d(rng);
        const int c = std::memcmp(id_key(a).data(), id_key(b).data(), kIdKeySize);
        CHECK((a < b) == (c < 0));
        CHECK((a == b) == (c == 0));
        CHECK(decode_id_key(id_key(a).data()) == a);
    }
    CHECK(id_key(-1) < id_key(0));
    CHECK(id_key(0) < id_key(1));

    // Chave de título: prefixo de 64 bytes + RID.
    Key k1(kTitleKeySize), k2(kTitleKeySize), busca(kTitleKeySize);
    const std::string longo(100, 'x');
    encode_title_key(longo, Rid{5, 16}, k1.data());
    encode_title_key(longo + "diferente no fim", Rid{3, 16}, k2.data());
    CHECK(title_prefix_equal(k1.data(), k2.data()));  // diferem só após 64 bytes
    CHECK(k2 < k1);                                   // desempate pelo RID
    encode_title_search_key(longo, busca.data());
    CHECK(busca < k1 && busca < k2);                  // chave de busca vem antes
    Key k3(kTitleKeySize);
    encode_title_key("abc", Rid{1, 16}, k3.data());
    Key k4(kTitleKeySize);
    encode_title_key("abcd", Rid{1, 16}, k4.data());
    CHECK(k3 < k4);                                   // prefixo menor ordena antes
    CHECK(!title_prefix_equal(k3.data(), k4.data()));
}

void test_empty_tree(const std::string& dir) {
    const std::string path = dir + "/vazia.bpt";
    {
        BPlusTree t;
        t.create(path, kIdKeySize);
        t.finish();
    }
    BPlusTree t;
    t.open(path);
    Rid r;
    CHECK(!t.find(id_key(10).data(), r));
    CHECK(t.stats().reads == 1);  // só a raiz
    CHECK(t.height() == 1);
    CHECK(t.entries() == 0);
    CHECK(!t.lower_bound(id_key(0).data()).valid);
    std::remove(path.c_str());
}

// Inserções em ordem aleatória com nós pequenos: muitas divisões, árvore alta.
void test_random_small_nodes(const std::string& dir) {
    const std::string path = dir + "/pequena.bpt";
    const std::size_t LC = 4, IC = 3;
    std::map<Key, Rid> gabarito;
    std::vector<int> ids(3000);
    std::iota(ids.begin(), ids.end(), -500);
    for (int& x : ids) x *= 7;  // deixa buracos entre as chaves
    std::mt19937 rng(42);
    std::shuffle(ids.begin(), ids.end(), rng);

    {
        BPlusTree t;
        t.create(path, kIdKeySize);
        t.set_capacity_for_test(LC, IC);
        for (int id : ids) {
            const Rid rid{static_cast<std::uint32_t>(id + 100000), static_cast<std::uint16_t>(id & 0xFFF)};
            CHECK(t.insert(id_key(id).data(), rid));
            gabarito[id_key(id)] = rid;
        }
        // Chave repetida é recusada e não altera nada.
        CHECK(!t.insert(id_key(ids[0]).data(), Rid{1, 1}));
        CHECK(t.entries() == ids.size());
        t.finish();
        CHECK(t.height() >= 6);  // 3000 chaves com nós de 4 entradas
        CHECK(t.leaves() + t.internals() == t.block_count());
    }

    BPlusTree t;
    t.open(path);
    Rid r;
    CHECK(t.find(id_key(ids[0]).data(), r));  // lê a raiz e carrega metadados
    validate_tree(t, gabarito, LC, IC);

    // Busca exata: lê exatamente 'altura' blocos, ache ou não.
    for (int i = 0; i < 500; ++i) {
        const int id = ids[i];
        t.reset_stats();
        Rid rid;
        CHECK(t.find(id_key(id).data(), rid));
        CHECK(rid.block == gabarito[id_key(id)].block && rid.offset == gabarito[id_key(id)].offset);
        CHECK(t.stats().reads == t.height());
        t.reset_stats();
        CHECK(!t.find(id_key(id + 1).data(), rid));  // id+1 nunca é múltiplo de 7
        CHECK(t.stats().reads == t.height());
    }

    // Busca por intervalo contra o gabarito.
    std::uniform_int_distribution<int> d(-4000, 22000);
    for (int q = 0; q < 300; ++q) {
        int a = d(rng), b = d(rng);
        if (a > b) std::swap(a, b);
        std::vector<int> esperado;
        for (auto it = gabarito.lower_bound(id_key(a)); it != gabarito.end() && !(id_key(b) < it->first); ++it)
            esperado.push_back(decode_id_key(it->first.data()));
        std::vector<int> obtido;
        for (BPlusTree::Cursor c = t.lower_bound(id_key(a).data()); c.valid; t.next(c)) {
            const int id = decode_id_key(t.key_at(c));
            if (id > b) break;
            obtido.push_back(id);
        }
        CHECK(obtido == esperado);
    }
    // Intervalo inteiramente após a maior chave e antes da menor.
    CHECK(!t.lower_bound(id_key(1000000).data()).valid);
    BPlusTree::Cursor c = t.lower_bound(id_key(-1000000).data());
    CHECK(c.valid && decode_id_key(t.key_at(c)) == -3500);
    std::remove(path.c_str());
}

// Capacidade real e inserção crescente: folhas ficam ~50% cheias.
void test_sequential_real_capacity(const std::string& dir) {
    const std::string path = dir + "/seq.bpt";
    std::map<Key, Rid> gabarito;
    BPlusTree t;
    t.create(path, kIdKeySize);
    const int N = 120000;
    for (int i = 0; i < N; ++i) {
        const Rid rid{static_cast<std::uint32_t>(i + 1), 16};
        CHECK(t.insert(id_key(i * 5).data(), rid));
        gabarito[id_key(i * 5)] = rid;
    }
    t.finish();
    const std::size_t cap = BPlusTree::leaf_capacity(kIdKeySize);
    const double media = static_cast<double>(N) / t.leaves();
    // Cada divisão deixa ~metade da folha vazia e, com chaves crescentes, a
    // metade esquerda nunca mais recebe inserções: ocupação média ~50%.
    CHECK(media >= 0.49 * cap && media <= 0.52 * cap);
    validate_tree(t, gabarito, cap, BPlusTree::internal_capacity(kIdKeySize));
    std::remove(path.c_str());
}

// Índice secundário: títulos repetidos e prefixos iguais.
void test_title_duplicates(const std::string& dir) {
    const std::string path = dir + "/titulos.bpt";
    BPlusTree t;
    t.create(path, kTitleKeySize);
    t.set_capacity_for_test(5, 4);  // nós pequenos: duplicados se espalham por várias folhas

    const std::string repetido = "Review of particle physics particle data group";
    const std::string base(70, 'p');  // dois títulos que só diferem após o byte 64
    const std::vector<std::string> titulos = {repetido, base + "A", base + "B", "Outro", "Zeta", "Alfa"};
    std::map<std::string, std::multiset<std::uint32_t>> por_prefixo;
    std::map<Key, Rid> gabarito;
    std::mt19937 rng(3);
    std::uint32_t blk = 1;
    for (int i = 0; i < 400; ++i) {
        const std::string& tit = titulos[rng() % titulos.size()];
        const Rid rid{blk++, 16};
        Key k(kTitleKeySize);
        encode_title_key(tit, rid, k.data());
        CHECK(t.insert(k.data(), rid));  // mesmo título, RIDs diferentes: chaves distintas
        gabarito[k] = rid;
        por_prefixo[tit.substr(0, kTitlePrefixLen)].insert(rid.block);
    }
    t.finish();
    validate_tree(t, gabarito, 5, 4);

    // Para cada título, a varredura a partir da chave de busca devolve
    // exatamente os RIDs com aquele prefixo.
    for (const std::string& tit : titulos) {
        Key busca(kTitleKeySize);
        encode_title_search_key(tit, busca.data());
        std::multiset<std::uint32_t> obtido;
        for (BPlusTree::Cursor c = t.lower_bound(busca.data()); c.valid; t.next(c)) {
            if (!title_prefix_equal(t.key_at(c), busca.data())) break;
            obtido.insert(t.rid_at(c).block);
        }
        CHECK(obtido == por_prefixo[tit.substr(0, kTitlePrefixLen)]);
    }
    // Título inexistente: nenhuma entrada.
    Key busca(kTitleKeySize);
    encode_title_search_key("Nao existe", busca.data());
    BPlusTree::Cursor c = t.lower_bound(busca.data());
    CHECK(!c.valid || !title_prefix_equal(t.key_at(c), busca.data()));
    std::remove(path.c_str());
}

}  // namespace

int main() {
    const std::string dir = testing::temp_dir();
    test_capacities();
    test_key_encoding();
    test_empty_tree(dir);
    test_random_small_nodes(dir);
    test_sequential_real_capacity(dir);
    test_title_duplicates(dir);
    ::rmdir(dir.c_str());
    return testing::tests_done("test_bptree");
}
