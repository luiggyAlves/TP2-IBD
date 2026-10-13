// bptree.cpp — implementação da B+Tree em disco.
// O layout dos nós e o algoritmo estão documentados em bptree.hpp.

#include "bptree.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "bytes.hpp"

namespace tp2 {

namespace {

constexpr std::uint32_t kNodeMagic = 0x31545042;  // "BPT1" em little-endian

// Deslocamentos do cabeçalho do nó.
constexpr std::size_t kOffMagic = 0;
constexpr std::size_t kOffIsLeaf = 4;
constexpr std::size_t kOffN = 6;
constexpr std::size_t kOffKeySize = 8;
constexpr std::size_t kOffNext = 12;
constexpr std::size_t kOffHeight = 16;
constexpr std::size_t kOffEntries = 20;
constexpr std::size_t kOffFirstLeaf = 24;
constexpr std::size_t kOffBlockSize = 28;

constexpr std::uint32_t kRoot = 0;  // a raiz mora sempre no bloco 0

}  // namespace

// ===========================================================================
// Capacidades (ordem da árvore)
// ===========================================================================

std::size_t BPlusTree::leaf_capacity(std::size_t key_size) {
    // Cada entrada de folha ocupa key_size + 6 bytes (chave + RID).
    return (blockio::kBlockSize - kHeaderSize) / (key_size + kRidSize);
}

std::size_t BPlusTree::internal_capacity(std::size_t key_size) {
    // n chaves + (n + 1) filhos: 32 + n*key_size + (n+1)*4 <= BLOCK_SIZE.
    return (blockio::kBlockSize - kHeaderSize - kChildSize) / (key_size + kChildSize);
}

// ===========================================================================
// Leitura e escrita de nós
// ===========================================================================

BPlusTree::Node BPlusTree::read_node(std::uint32_t blk) {
    file_.read_block(blk, buf_.data());
    const std::uint8_t* b = buf_.data();
    if (get_u32(b + kOffMagic) != kNodeMagic)
        throw std::runtime_error("bloco " + std::to_string(blk) + " nao e um no de B+Tree (" + file_.path() + ")");

    const std::size_t ks = get_u16(b + kOffKeySize);
    if (key_size_ == 0) key_size_ = ks;  // primeira leitura após open()
    if (ks != key_size_) throw std::runtime_error("tamanho de chave inconsistente no indice");

    // A raiz carrega os metadados da árvore. Durante a construção os valores
    // em memória são os corretos, então só os carregamos em modo consulta.
    if (blk == kRoot && !building_) {
        height_ = get_u32(b + kOffHeight);
        entries_ = get_u32(b + kOffEntries);
        first_leaf_ = get_u32(b + kOffFirstLeaf);
        if (get_u32(b + kOffBlockSize) != blockio::kBlockSize) {
            throw std::runtime_error("indice criado com BLOCK_SIZE=" + std::to_string(get_u32(b + kOffBlockSize)) +
                                     ", mas o programa foi compilado com BLOCK_SIZE=" +
                                     std::to_string(blockio::kBlockSize) + "; refaca o upload");
        }
    }

    Node node;
    node.leaf = b[kOffIsLeaf] != 0;
    node.next = get_u32(b + kOffNext);
    const std::size_t n = get_u16(b + kOffN);
    if (trace_enabled() && !building_)
        std::fprintf(stderr, "[trace] %s: le bloco %u (%s, %zu chaves%s)\n", file_.path().c_str(), blk,
                     node.leaf ? "folha" : "no interno", n, blk == kRoot ? ", raiz" : "");
    node.keys.resize(n * ks);

    const std::uint8_t* p = b + kHeaderSize;
    if (node.leaf) {
        node.rids.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            std::memcpy(&node.keys[i * ks], p, ks);
            p += ks;
            node.rids[i].block = get_u32(p);
            node.rids[i].offset = get_u16(p + 4);
            p += kRidSize;
        }
    } else {
        node.children.resize(n + 1);
        node.children[0] = get_u32(p);
        p += kChildSize;
        for (std::size_t i = 0; i < n; ++i) {
            std::memcpy(&node.keys[i * ks], p, ks);
            p += ks;
            node.children[i + 1] = get_u32(p);
            p += kChildSize;
        }
    }
    return node;
}

void BPlusTree::write_node(std::uint32_t blk, const Node& node) {
    const std::size_t n = num_keys(node);
    // Verificação de segurança: nunca gravar um nó maior que o bloco.
    if (node.leaf ? n > leaf_capacity(key_size_) : n > internal_capacity(key_size_))
        throw std::logic_error("no excede a capacidade do bloco");

    std::fill(buf_.begin(), buf_.end(), 0);
    std::uint8_t* b = buf_.data();
    put_u32(b + kOffMagic, kNodeMagic);
    b[kOffIsLeaf] = node.leaf ? 1 : 0;
    put_u16(b + kOffN, static_cast<std::uint16_t>(n));
    put_u16(b + kOffKeySize, static_cast<std::uint16_t>(key_size_));
    put_u32(b + kOffNext, node.next);
    if (blk == kRoot) {  // metadados só na raiz
        put_u32(b + kOffHeight, height_);
        put_u32(b + kOffEntries, entries_);
        put_u32(b + kOffFirstLeaf, first_leaf_);
        put_u32(b + kOffBlockSize, static_cast<std::uint32_t>(blockio::kBlockSize));
    }

    std::uint8_t* p = b + kHeaderSize;
    if (node.leaf) {
        for (std::size_t i = 0; i < n; ++i) {
            std::memcpy(p, key_ptr(node, i), key_size_);
            p += key_size_;
            put_u32(p, node.rids[i].block);
            put_u16(p + 4, node.rids[i].offset);
            p += kRidSize;
        }
    } else {
        put_u32(p, node.children[0]);
        p += kChildSize;
        for (std::size_t i = 0; i < n; ++i) {
            std::memcpy(p, key_ptr(node, i), key_size_);
            p += key_size_;
            put_u32(p, node.children[i + 1]);
            p += kChildSize;
        }
    }
    file_.write_block(blk, buf_.data());
}

std::uint32_t BPlusTree::new_block() {
    // O próximo bloco livre é sempre o fim do arquivo (não há remoção).
    // Não é preciso gravar zeros antes: o nó é gravado logo em seguida.
    return static_cast<std::uint32_t>(file_.block_count());
}

// ===========================================================================
// Comparação e posicionamento dentro de um nó (em memória, sem I/O)
// ===========================================================================

int BPlusTree::compare(const std::uint8_t* a, const std::uint8_t* b) const {
    return std::memcmp(a, b, key_size_);
}

std::size_t BPlusTree::child_index(const Node& n, const std::uint8_t* key) const {
    // Busca binária pela primeira chave > key; o filho a seguir é esse índice.
    std::size_t lo = 0, hi = num_keys(n);
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (compare(key_ptr(n, mid), key) <= 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

std::size_t BPlusTree::leaf_position(const Node& n, const std::uint8_t* key) const {
    // Busca binária pela primeira chave >= key.
    std::size_t lo = 0, hi = num_keys(n);
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (compare(key_ptr(n, mid), key) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

// ===========================================================================
// Construção
// ===========================================================================

void BPlusTree::create(const std::string& path, std::size_t key_size) {
    file_.open(path, blockio::Mode::kTruncate);
    key_size_ = key_size;
    leaf_cap_ = leaf_capacity(key_size);
    internal_cap_ = internal_capacity(key_size);
    if (leaf_cap_ < 3 || internal_cap_ < 3)
        throw std::runtime_error("chave grande demais para o BLOCK_SIZE: no comportaria menos de 3 chaves");
    building_ = true;
    height_ = 1;
    entries_ = 0;
    first_leaf_ = kRoot;
    leaves_ = 1;
    internals_ = 0;
    write_node(kRoot, Node{});  // árvore vazia: a raiz é uma folha sem entradas
}

void BPlusTree::set_capacity_for_test(std::size_t leaf_cap, std::size_t internal_cap) {
    if (leaf_cap < 3 || internal_cap < 3 || leaf_cap > leaf_capacity(key_size_) ||
        internal_cap > internal_capacity(key_size_))
        throw std::invalid_argument("capacidade de teste invalida");
    leaf_cap_ = leaf_cap;
    internal_cap_ = internal_cap;
}

std::uint32_t BPlusTree::descend(const std::uint8_t* key, Node& leaf, std::vector<PathStep>* path) {
    std::uint32_t blk = kRoot;
    Node node = read_node(blk);
    while (!node.leaf) {
        const std::size_t idx = child_index(node, key);
        const std::uint32_t child = node.children[idx];
        if (path != nullptr) path->push_back(PathStep{blk, std::move(node), idx});
        blk = child;
        node = read_node(blk);
    }
    leaf = std::move(node);
    return blk;
}

void BPlusTree::split_root(Node& left, Node& right, const std::vector<std::uint8_t>& sep) {
    const std::uint32_t lb = new_block();
    const std::uint32_t rb = lb + 1;
    if (left.leaf) {
        // A raiz era a única folha: as duas metades viram as duas primeiras
        // folhas, encadeadas, e a da esquerda passa a ser a primeira folha.
        right.next = left.next;  // (sempre 0 aqui)
        left.next = rb;
        first_leaf_ = lb;
        leaves_ += 1;     // 1 folha virou 2
        internals_ += 1;  // a nova raiz
    } else {
        internals_ += 2;  // raiz antiga virou 2 nós + a nova raiz (que reaproveita o bloco 0)
    }
    write_node(lb, left);
    write_node(rb, right);

    Node root;
    root.leaf = false;
    root.keys = sep;
    root.children = {lb, rb};
    ++height_;
    write_node(kRoot, root);
}

bool BPlusTree::insert(const std::uint8_t* key, const Rid& rid) {
    // 1) Desce até a folha, guardando o caminho (os pais podem ser alterados).
    std::vector<PathStep> path;
    Node leaf;
    const std::uint32_t leaf_blk = descend(key, leaf, &path);

    // 2) Insere a entrada na posição ordenada da folha.
    const std::size_t pos = leaf_position(leaf, key);
    if (pos < num_keys(leaf) && compare(key_ptr(leaf, pos), key) == 0) return false;  // duplicada
    leaf.keys.insert(leaf.keys.begin() + static_cast<std::ptrdiff_t>(pos * key_size_), key, key + key_size_);
    leaf.rids.insert(leaf.rids.begin() + static_cast<std::ptrdiff_t>(pos), rid);
    ++entries_;

    // 3) Se coube, basta regravar a folha.
    if (num_keys(leaf) <= leaf_cap_) {
        write_node(leaf_blk, leaf);
        return true;
    }

    // 4) Folha estourou (leaf_cap + 1 entradas): divide ao meio.
    const std::size_t n = num_keys(leaf);
    const std::size_t left_n = (n + 1) / 2;
    Node right;
    right.leaf = true;
    right.keys.assign(leaf.keys.begin() + static_cast<std::ptrdiff_t>(left_n * key_size_), leaf.keys.end());
    right.rids.assign(leaf.rids.begin() + static_cast<std::ptrdiff_t>(left_n), leaf.rids.end());
    leaf.keys.resize(left_n * key_size_);
    leaf.rids.resize(left_n);
    // Separador: primeira chave da metade direita, COPIADA para o pai.
    std::vector<std::uint8_t> sep(right.keys.begin(), right.keys.begin() + static_cast<std::ptrdiff_t>(key_size_));

    if (leaf_blk == kRoot) {
        split_root(leaf, right, sep);
        return true;
    }
    const std::uint32_t right_blk = new_block();
    right.next = leaf.next;  // mantém o encadeamento das folhas
    leaf.next = right_blk;
    ++leaves_;
    write_node(right_blk, right);
    write_node(leaf_blk, leaf);

    // 5) Sobe pelo caminho inserindo (separador, novo filho) nos pais.
    std::uint32_t new_child = right_blk;
    while (!path.empty()) {
        PathStep step = std::move(path.back());
        path.pop_back();
        Node& parent = step.node;
        parent.keys.insert(parent.keys.begin() + static_cast<std::ptrdiff_t>(step.child * key_size_), sep.begin(),
                           sep.end());
        parent.children.insert(parent.children.begin() + static_cast<std::ptrdiff_t>(step.child + 1), new_child);

        if (num_keys(parent) <= internal_cap_) {
            write_node(step.blk, parent);
            return true;
        }

        // Nó interno estourou: a chave do meio SOBE e não fica em nenhum dos lados.
        const std::size_t pn = num_keys(parent);
        const std::size_t mid = pn / 2;
        Node pright;
        pright.leaf = false;
        pright.keys.assign(parent.keys.begin() + static_cast<std::ptrdiff_t>((mid + 1) * key_size_), parent.keys.end());
        pright.children.assign(parent.children.begin() + static_cast<std::ptrdiff_t>(mid + 1), parent.children.end());
        sep.assign(parent.keys.begin() + static_cast<std::ptrdiff_t>(mid * key_size_),
                   parent.keys.begin() + static_cast<std::ptrdiff_t>((mid + 1) * key_size_));
        parent.keys.resize(mid * key_size_);
        parent.children.resize(mid + 1);

        if (step.blk == kRoot) {
            split_root(parent, pright, sep);
            return true;
        }
        const std::uint32_t pright_blk = new_block();
        ++internals_;
        write_node(pright_blk, pright);
        write_node(step.blk, parent);
        new_child = pright_blk;
    }
    return true;  // não chega aqui: a raiz sempre absorve ou se divide
}

void BPlusTree::finish() {
    // Regrava a raiz para persistir os metadados atualizados (total de entradas).
    Node root = read_node(kRoot);
    write_node(kRoot, root);
    building_ = false;
}

// ===========================================================================
// Consulta
// ===========================================================================

void BPlusTree::open(const std::string& path) {
    file_.open(path, blockio::Mode::kRead);
    if (file_.block_count() == 0) throw std::runtime_error("indice vazio: " + path);
    building_ = false;
    key_size_ = 0;  // descoberto na leitura da raiz
}

bool BPlusTree::find(const std::uint8_t* key, Rid& rid) {
    Node leaf;
    descend(key, leaf, nullptr);
    // Se a chave existir, está nesta folha: não é preciso ler a vizinha.
    const std::size_t pos = leaf_position(leaf, key);
    if (pos < num_keys(leaf) && compare(key_ptr(leaf, pos), key) == 0) {
        rid = leaf.rids[pos];
        return true;
    }
    return false;
}

BPlusTree::Cursor BPlusTree::lower_bound(const std::uint8_t* key) {
    Cursor c;
    c.leaf = descend(key, c.node, nullptr);
    c.pos = leaf_position(c.node, key);
    c.valid = true;
    // Se a folha acabou antes de achar uma chave >= key, a próxima entrada
    // (se houver) é a primeira da folha seguinte.
    skip_exhausted_leaves(c);
    return c;
}

bool BPlusTree::next(Cursor& c) {
    if (!c.valid) return false;
    ++c.pos;
    return skip_exhausted_leaves(c);
}

bool BPlusTree::skip_exhausted_leaves(Cursor& c) {
    // Enquanto o cursor estiver além da última entrada da folha, segue o
    // ponteiro 'next' para a folha à direita (1 leitura por folha).
    while (c.pos >= num_keys(c.node)) {
        if (c.node.next == 0) {  // era a última folha: fim da árvore
            c.valid = false;
            return false;
        }
        c.leaf = c.node.next;
        c.node = read_node(c.leaf);
        c.pos = 0;
    }
    return true;
}

// ===========================================================================
// Codificação de chaves
// ===========================================================================

void encode_id_key(int id, std::uint8_t* out) {
    // Inverter o bit de sinal leva INT_MIN..INT_MAX para 0..UINT_MAX
    // preservando a ordem; em big-endian, memcmp passa a ordenar numericamente.
    put_be32(out, static_cast<std::uint32_t>(id) ^ 0x80000000u);
}

int decode_id_key(const std::uint8_t* key) {
    return static_cast<int>(get_be32(key) ^ 0x80000000u);
}

void encode_title_key(const std::string& titulo, const Rid& rid, std::uint8_t* out) {
    std::memset(out, 0, kTitlePrefixLen);
    std::memcpy(out, titulo.data(), std::min(titulo.size(), kTitlePrefixLen));
    put_be32(out + kTitlePrefixLen, rid.block);
    put_be16(out + kTitlePrefixLen + 4, rid.offset);
}

bool title_prefix_equal(const std::uint8_t* key, const std::uint8_t* search_key) {
    return std::memcmp(key, search_key, kTitlePrefixLen) == 0;
}

}  // namespace tp2
