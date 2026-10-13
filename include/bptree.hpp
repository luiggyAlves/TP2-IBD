// bptree.hpp — B+Tree armazenada em memória secundária (Parte 2).
//
// Uma mesma implementação atende aos dois índices do trabalho. A árvore
// trata cada chave como uma sequência de 'key_size' bytes comparada com
// memcmp; quem usa a árvore é responsável por codificar a chave de modo que a
// ordem dos bytes seja a ordem desejada (ver encode_id_key e
// encode_title_key, no fim deste arquivo).
//
// ---------------------------------------------------------------------------
// Layout de um nó (1 nó = 1 bloco de BLOCK_SIZE bytes)
// ---------------------------------------------------------------------------
//
// Cabeçalho (32 bytes, em todo nó):
//   offset  tamanho  campo
//   0       4        magic       : 'BPT1', para detectar arquivo errado
//   4       1        is_leaf     : 1 = folha, 0 = nó interno
//   5       1        reservado
//   6       2        n           : número de chaves no nó
//   8       2        key_size    : tamanho da chave em bytes
//   10      2        reservado
//   12      4        next        : (folha) próxima folha à direita; 0 = nenhuma
//   16      4        height      : (só na raiz) altura da árvore em níveis
//   20      4        entries     : (só na raiz) total de entradas nas folhas
//   24      4        first_leaf  : (só na raiz) folha mais à esquerda
//   28      4        block_size  : (só na raiz) BLOCK_SIZE da criação
//
// Folha: n entradas (chave, RID), coladas a partir do byte 32:
//   [chave 0 | RID 0][chave 1 | RID 1] ...   RID = bloco (4) + deslocamento (2)
//
// Nó interno: n chaves e n+1 ponteiros (números de bloco), a partir do byte 32:
//   [filho 0][chave 0 | filho 1][chave 1 | filho 2] ...
//   Todas as chaves do filho i são < chave i <= todas as chaves do filho i+1.
//
// A raiz fica SEMPRE no bloco 0. Quando a raiz se divide, suas duas metades
// vão para blocos novos e o bloco 0 é regravado como a nova raiz. Assim a
// busca começa no bloco 0 sem precisar ler um bloco de cabeçalho separado, e
// os metadados da árvore (altura, total de entradas) ficam no próprio nó raiz.
// Como o bloco 0 é sempre a raiz, o valor 0 serve de "ponteiro nulo" em next.
//
// ---------------------------------------------------------------------------
// Ordem
// ---------------------------------------------------------------------------
//   capacidade da folha  = floor((BLOCK_SIZE - 32) / (key_size + 6))
//   chaves no nó interno = floor((BLOCK_SIZE - 32 - 4) / (key_size + 4))
//   ordem (máx. filhos)  = chaves no nó interno + 1
//
// Exemplos com BLOCK_SIZE = 4096:
//   índice primário   (chave 4 bytes)  : folha 406 entradas, interno 507 chaves (ordem 508)
//   índice secundário (chave 70 bytes) : folha  53 entradas, interno  54 chaves (ordem 55)
//
// ---------------------------------------------------------------------------
// Inserção
// ---------------------------------------------------------------------------
// Algoritmo clássico: desce da raiz até a folha guardando o caminho, insere a
// entrada em ordem; se a folha estourar, divide-a ao meio (a primeira chave da
// metade direita é COPIADA para o pai); se o pai estourar, divide-o (a chave
// do meio SOBE para o avô, sem ficar no nó) e assim por diante até a raiz.
// Toda leitura e escrita de nó passa pela camada blockio e é contada.

#ifndef BPTREE_HPP
#define BPTREE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "blockio.hpp"
#include "config.hpp"
#include "datafile.hpp"

namespace tp2 {

class BPlusTree {
  public:
    static constexpr std::size_t kHeaderSize = 32;
    static constexpr std::size_t kRidSize = 6;    // bloco (4) + deslocamento (2)
    static constexpr std::size_t kChildSize = 4;  // número de bloco

    // Capacidades calculadas a partir de BLOCK_SIZE e do tamanho da chave.
    static std::size_t leaf_capacity(std::size_t key_size);
    static std::size_t internal_capacity(std::size_t key_size);  // máx. de chaves

    // Nó decodificado em memória. Usado tanto na inserção quanto na busca.
    struct Node {
        bool leaf = true;
        std::uint32_t next = 0;               // só folhas
        std::vector<std::uint8_t> keys;       // n chaves de key_size bytes, coladas
        std::vector<std::uint32_t> children;  // só internos: n + 1 filhos
        std::vector<Rid> rids;                // só folhas: n RIDs
    };

    // Posição dentro das folhas, usada para percorrer entradas em ordem.
    struct Cursor {
        bool valid = false;     // false = passou do fim da árvore
        std::uint32_t leaf = 0; // bloco da folha atual
        std::size_t pos = 0;    // entrada atual dentro da folha
        Node node;              // conteúdo da folha atual (já lido)
    };

    // ---- construção ----

    // Cria a árvore vazia: uma única folha (a raiz) no bloco 0.
    void create(const std::string& path, std::size_t key_size);

    // Reduz as capacidades dos nós (apenas para testes: força muitas divisões
    // com poucas chaves). Os valores precisam ser <= capacidade física e >= 3.
    void set_capacity_for_test(std::size_t leaf_cap, std::size_t internal_cap);

    // Insere (chave, RID). Devolve false se a chave já existir (nada é gravado).
    bool insert(const std::uint8_t* key, const Rid& rid);

    // Atualiza os metadados gravados na raiz (1 leitura + 1 escrita).
    void finish();

    // ---- consulta ----

    // Abre uma árvore existente (não lê nenhum bloco; a primeira busca lê a raiz).
    void open(const std::string& path);

    // Busca exata: desce da raiz até a folha (altura leituras) e procura a chave.
    bool find(const std::uint8_t* key, Rid& rid);

    // Desce até a folha e posiciona o cursor na primeira entrada com chave >= key.
    // Se todas as entradas daquela folha forem menores que key, avança para a
    // folha seguinte (1 leitura a mais). Cursor inválido = não há tal entrada.
    Cursor lower_bound(const std::uint8_t* key);

    // Avança o cursor para a próxima entrada, lendo a próxima folha encadeada
    // quando a atual acaba. Devolve false (e invalida o cursor) no fim.
    bool next(Cursor& c);

    // Chave e RID da entrada sob o cursor.
    const std::uint8_t* key_at(const Cursor& c) const { return &c.node.keys[c.pos * key_size_]; }
    Rid rid_at(const Cursor& c) const { return c.node.rids[c.pos]; }

    // ---- informações ----
    std::size_t key_size() const { return key_size_; }
    std::uint32_t height() const { return height_; }        // válido após ler a raiz
    std::uint32_t entries() const { return entries_; }      // válido após ler a raiz
    std::uint32_t leaves() const { return leaves_; }        // contado durante a construção
    std::uint32_t internals() const { return internals_; }  // contado durante a construção
    std::size_t leaf_cap() const { return leaf_cap_; }
    std::size_t internal_cap() const { return internal_cap_; }
    std::uint64_t block_count() const { return file_.block_count(); }
    const blockio::Stats& stats() const { return file_.stats(); }
    void reset_stats() { file_.reset_stats(); }
    const std::string& path() const { return file_.path(); }

    // Lê e decodifica um nó (1 leitura). Público para os testes de estrutura.
    Node read_node(std::uint32_t blk);

  private:
    blockio::BlockFile file_;
    std::vector<std::uint8_t> buf_ = std::vector<std::uint8_t>(blockio::kBlockSize);
    std::size_t key_size_ = 0;
    std::size_t leaf_cap_ = 0;
    std::size_t internal_cap_ = 0;
    bool building_ = false;  // true durante a construção: metadados vêm da memória

    // Metadados (persistidos no nó raiz).
    std::uint32_t height_ = 0;
    std::uint32_t entries_ = 0;
    std::uint32_t first_leaf_ = 0;

    // Contadores mantidos em memória durante a construção (para o relatório).
    std::uint32_t leaves_ = 0;
    std::uint32_t internals_ = 0;

    void write_node(std::uint32_t blk, const Node& node);
    std::uint32_t new_block();
    int compare(const std::uint8_t* a, const std::uint8_t* b) const;
    std::size_t num_keys(const Node& n) const { return n.keys.size() / key_size_; }
    const std::uint8_t* key_ptr(const Node& n, std::size_t i) const { return &n.keys[i * key_size_]; }

    // Índice do filho a seguir em um nó interno: quantidade de chaves <= key.
    std::size_t child_index(const Node& n, const std::uint8_t* key) const;
    // Primeira posição da folha com chave >= key.
    std::size_t leaf_position(const Node& n, const std::uint8_t* key) const;

    // Desce da raiz até a folha que deve conter 'key'.
    // Se 'path' não for nulo, guarda (bloco, nó, filho escolhido) de cada nível interno.
    struct PathStep {
        std::uint32_t blk;
        Node node;
        std::size_t child;
    };
    std::uint32_t descend(const std::uint8_t* key, Node& leaf, std::vector<PathStep>* path);

    // Divisão da raiz: 'left' e 'right' vão para blocos novos e o bloco 0
    // passa a ser um nó interno com uma chave (sep) e dois filhos.
    void split_root(Node& left, Node& right, const std::vector<std::uint8_t>& sep);

    // Se o cursor passou da última entrada da folha, avança pelas folhas
    // encadeadas até achar uma entrada (ou invalida o cursor no fim da árvore).
    bool skip_exhausted_leaves(Cursor& c);
};

// ---------------------------------------------------------------------------
// Codificação das chaves dos dois índices
// ---------------------------------------------------------------------------

// Índice primário: chave = ID (4 bytes, big-endian com o bit de sinal
// invertido, para que memcmp ordene como inteiros com sinal).
constexpr std::size_t kIdKeySize = 4;
void encode_id_key(int id, std::uint8_t* out);
int decode_id_key(const std::uint8_t* key);

// Índice secundário: chave composta = (primeiros kTitlePrefixLen bytes do
// título, completados com '\0') + RID (6 bytes, big-endian).
// Acrescentar o RID torna cada chave única mesmo quando vários registros têm o
// mesmo título: os duplicados ficam lado a lado nas folhas, ordenados pelo
// RID, e a árvore não precisa de nenhum tratamento especial para eles.
constexpr std::size_t kTitleKeySize = kTitlePrefixLen + 6;
void encode_title_key(const std::string& titulo, const Rid& rid, std::uint8_t* out);

// Chave de busca: prefixo do título + RID (0, 0), menor que qualquer RID real
// (o bloco 0 do arquivo de dados é o cabeçalho, nunca guarda registro).
inline void encode_title_search_key(const std::string& titulo, std::uint8_t* out) {
    encode_title_key(titulo, Rid{0, 0}, out);
}

// true se a chave do índice tem o mesmo prefixo de título que 'search_key'.
bool title_prefix_equal(const std::uint8_t* key, const std::uint8_t* search_key);

}  // namespace tp2

#endif  // BPTREE_HPP
