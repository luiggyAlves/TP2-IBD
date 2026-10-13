// datafile.cpp — implementação do arquivo de dados por hashing.
// O layout completo dos blocos e registros está documentado em datafile.hpp.

#include "datafile.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "bytes.hpp"
#include "config.hpp"

namespace tp2 {

namespace {

// Assinatura gravada no início do bloco 0, para reconhecer o arquivo.
constexpr char kMagic[8] = {'T', 'P', '2', 'H', 'A', 'S', 'H', '\0'};

// Deslocamentos dos campos dentro do registro serializado.
constexpr std::size_t kOffLen = 0;
constexpr std::size_t kOffId = 2;
constexpr std::size_t kOffAno = 6;
constexpr std::size_t kOffCit = 10;
constexpr std::size_t kOffData = 14;
constexpr std::size_t kOffLenTit = 33;
constexpr std::size_t kOffLenAut = 35;
constexpr std::size_t kOffLenSnip = 37;
static_assert(kOffLenSnip + 2 == kRecordFixedSize, "layout do registro inconsistente");

// Deslocamentos dos campos dentro do cabeçalho do arquivo (bloco 0).
constexpr std::size_t kHdrMagic = 0;
constexpr std::size_t kHdrBlockSize = 8;
constexpr std::size_t kHdrBuckets = 12;
constexpr std::size_t kHdrRecords = 16;
constexpr std::size_t kHdrOverflow = 24;
constexpr std::size_t kHdrRecordBytes = 32;

}  // namespace

// ===========================================================================
// Serialização de registros
// ===========================================================================

std::size_t record_size(const Article& a) {
    return kRecordFixedSize + a.titulo.size() + a.autores.size() + a.snippet.size();
}

bool fit_record(Article& a, std::size_t max_size) {
    std::size_t size = record_size(a);
    if (size <= max_size) return false;
    // Corta o excesso de cada campo, do menos importante para a busca
    // (snippet) ao mais importante (título).
    for (std::string* campo : {&a.snippet, &a.autores, &a.titulo}) {
        const std::size_t excesso = size - max_size;
        const std::size_t corte = std::min(excesso, campo->size());
        campo->resize(campo->size() - corte);
        size -= corte;
        if (size <= max_size) break;
    }
    return true;
}

void encode_record(const Article& a, std::uint8_t* dst) {
    const std::size_t len = record_size(a);
    put_u16(dst + kOffLen, static_cast<std::uint16_t>(len));
    put_i32(dst + kOffId, a.id);
    put_i32(dst + kOffAno, a.ano);
    put_i32(dst + kOffCit, a.citacoes);
    // Data/hora: campo de tamanho fixo (19 bytes), preenchido com '\0'.
    std::memset(dst + kOffData, 0, kDateLen);
    std::memcpy(dst + kOffData, a.atualizacao.data(), std::min(a.atualizacao.size(), kDateLen));
    put_u16(dst + kOffLenTit, static_cast<std::uint16_t>(a.titulo.size()));
    put_u16(dst + kOffLenAut, static_cast<std::uint16_t>(a.autores.size()));
    put_u16(dst + kOffLenSnip, static_cast<std::uint16_t>(a.snippet.size()));
    // Os três textos de tamanho variável vêm logo após a parte fixa.
    std::uint8_t* p = dst + kRecordFixedSize;
    std::memcpy(p, a.titulo.data(), a.titulo.size());
    p += a.titulo.size();
    std::memcpy(p, a.autores.data(), a.autores.size());
    p += a.autores.size();
    std::memcpy(p, a.snippet.data(), a.snippet.size());
}

std::size_t decode_record(const std::uint8_t* src, Article& out) {
    const std::size_t len = get_u16(src + kOffLen);
    out.id = get_i32(src + kOffId);
    out.ano = get_i32(src + kOffAno);
    out.citacoes = get_i32(src + kOffCit);
    // A data pode ter menos de 19 bytes (preenchida com '\0').
    const char* data = reinterpret_cast<const char*>(src + kOffData);
    out.atualizacao.assign(data, strnlen(data, kDateLen));
    const std::size_t lt = get_u16(src + kOffLenTit);
    const std::size_t la = get_u16(src + kOffLenAut);
    const std::size_t ls = get_u16(src + kOffLenSnip);
    const char* p = reinterpret_cast<const char*>(src + kRecordFixedSize);
    out.titulo.assign(p, lt);
    out.autores.assign(p + lt, la);
    out.snippet.assign(p + lt + la, ls);
    return len;
}

int peek_record_id(const std::uint8_t* src) { return get_i32(src + kOffId); }

// ===========================================================================
// Cabeçalho de bloco de dados
// ===========================================================================

DataBlockHeader get_block_header(const std::uint8_t* block) {
    DataBlockHeader h;
    h.next = get_u32(block + 0);
    h.bucket = get_u32(block + 4);
    h.count = get_u16(block + 8);
    h.used = get_u16(block + 10);
    return h;
}

void put_block_header(std::uint8_t* block, const DataBlockHeader& h) {
    put_u32(block + 0, h.next);
    put_u32(block + 4, h.bucket);
    put_u16(block + 8, h.count);
    put_u16(block + 10, h.used);
    put_u32(block + 12, 0);  // reservado
}

void for_each_record_in_block(const std::uint8_t* block,
                              const std::function<void(std::uint16_t, const std::uint8_t*)>& fn) {
    const DataBlockHeader h = get_block_header(block);
    std::size_t off = kDataBlockHeaderSize;
    const std::size_t end = kDataBlockHeaderSize + h.used;
    for (std::uint16_t i = 0; i < h.count && off < end; ++i) {
        const std::uint8_t* rec = block + off;
        const std::size_t len = get_u16(rec + kOffLen);
        if (len < kRecordFixedSize || off + len > end) {
            throw std::runtime_error("bloco de dados corrompido (registro com tamanho invalido)");
        }
        fn(static_cast<std::uint16_t>(off), rec);
        off += len;  // registros ficam colados uns aos outros
    }
}

// ===========================================================================
// Escolha de N
// ===========================================================================

std::uint32_t next_prime(std::uint32_t n) {
    auto primo = [](std::uint32_t x) {
        if (x < 2) return false;
        for (std::uint32_t d = 2; static_cast<std::uint64_t>(d) * d <= x; ++d)
            if (x % d == 0) return false;
        return true;
    };
    while (!primo(n)) ++n;
    return n;
}

// ===========================================================================
// HashFile — construção
// ===========================================================================

void HashFile::create(const std::string& path, std::uint32_t num_buckets) {
    if (num_buckets == 0) throw std::runtime_error("numero de buckets deve ser > 0");
    file_.open(path, blockio::Mode::kTruncate);
    header_ = HashFileHeader{};
    header_.block_size = static_cast<std::uint32_t>(blockio::kBlockSize);
    header_.num_buckets = num_buckets;

    tail_block_.assign(num_buckets, 0);
    chain_len_.assign(num_buckets, 1);
    bucket_recs_.assign(num_buckets, 0);
    block_recs_.assign(1 + num_buckets, 0);

    // Bloco 0: cabeçalho provisório (o definitivo é gravado em finish()).
    write_header();

    // Blocos 1..N: bloco primário vazio de cada bucket.
    for (std::uint32_t b = 0; b < num_buckets; ++b) {
        std::fill(buf_.begin(), buf_.end(), 0);
        DataBlockHeader h;
        h.bucket = b;
        put_block_header(buf_.data(), h);
        file_.write_block(bucket_block(b), buf_.data());
        tail_block_[b] = bucket_block(b);
    }
    next_free_block_ = 1 + num_buckets;  // overflow começa logo após os primários
}

Rid HashFile::insert(const Article& a) {
    const std::size_t size = record_size(a);
    if (size > kDataPayload) throw std::runtime_error("registro maior que o bloco (use fit_record)");

    const std::uint32_t b = hash_bucket(a.id, header_.num_buckets);
    std::uint32_t blk = tail_block_[b];

    // Lê o último bloco da cadeia do bucket.
    file_.read_block(blk, buf_.data());
    DataBlockHeader h = get_block_header(buf_.data());

    if (h.used + size > kDataPayload) {
        // Não cabe: aloca um bloco de overflow no fim do arquivo e o encadeia.
        const std::uint32_t novo = next_free_block_++;
        h.next = novo;
        put_block_header(buf_.data(), h);
        file_.write_block(blk, buf_.data());  // grava o ponteiro 'next' no bloco antigo

        std::fill(buf_.begin(), buf_.end(), 0);
        h = DataBlockHeader{};
        h.bucket = b;
        blk = novo;
        tail_block_[b] = novo;
        ++chain_len_[b];
        ++header_.overflow_blocks;
        block_recs_.push_back(0);
    }

    // Acrescenta o registro no fim da área ocupada do bloco.
    const std::uint16_t off = static_cast<std::uint16_t>(kDataBlockHeaderSize + h.used);
    encode_record(a, buf_.data() + off);
    h.used = static_cast<std::uint16_t>(h.used + size);
    ++h.count;
    put_block_header(buf_.data(), h);
    file_.write_block(blk, buf_.data());

    ++header_.num_records;
    header_.record_bytes += size;
    ++bucket_recs_[b];
    ++block_recs_[blk];
    return Rid{blk, off};
}

void HashFile::write_header() {
    std::fill(buf_.begin(), buf_.end(), 0);
    std::memcpy(buf_.data() + kHdrMagic, kMagic, sizeof kMagic);
    put_u32(buf_.data() + kHdrBlockSize, header_.block_size);
    put_u32(buf_.data() + kHdrBuckets, header_.num_buckets);
    put_u64(buf_.data() + kHdrRecords, header_.num_records);
    put_u32(buf_.data() + kHdrOverflow, header_.overflow_blocks);
    put_u64(buf_.data() + kHdrRecordBytes, header_.record_bytes);
    file_.write_block(0, buf_.data());
}

LoadStats HashFile::finish() {
    write_header();

    LoadStats s;
    s.records = header_.num_records;
    s.record_bytes = header_.record_bytes;
    s.num_buckets = header_.num_buckets;
    s.overflow_blocks = header_.overflow_blocks;
    s.data_blocks = static_cast<std::uint64_t>(header_.num_buckets) + header_.overflow_blocks;
    s.total_blocks = file_.block_count();

    // Registros por bloco (todos os blocos de dados, primários e overflow).
    s.min_recs_per_block = UINT32_MAX;
    for (std::size_t blk = 1; blk < block_recs_.size(); ++blk) {
        s.min_recs_per_block = std::min<std::uint32_t>(s.min_recs_per_block, block_recs_[blk]);
        s.max_recs_per_block = std::max<std::uint32_t>(s.max_recs_per_block, block_recs_[blk]);
    }

    // Distribuição dos registros pelos buckets (concentração, Seção 7.6).
    for (std::uint32_t b = 0; b < header_.num_buckets; ++b) {
        const std::uint32_t c = chain_len_[b];
        const std::uint32_t r = bucket_recs_[b];
        if (r == 0) ++s.empty_buckets;
        if (c > 1) ++s.buckets_with_overflow;
        s.max_chain_len = std::max(s.max_chain_len, c);
        s.max_recs_per_bucket = std::max(s.max_recs_per_bucket, r);
        if (s.chain_len_hist.size() <= c) s.chain_len_hist.resize(c + 1, 0);
        ++s.chain_len_hist[c];
        if (s.bucket_size_hist.size() <= r) s.bucket_size_hist.resize(r + 1, 0);
        ++s.bucket_size_hist[r];
    }
    return s;
}

// ===========================================================================
// HashFile — consulta
// ===========================================================================

void HashFile::open(const std::string& path) {
    file_.open(path, blockio::Mode::kRead);
    if (file_.block_count() == 0) throw std::runtime_error("arquivo de dados vazio: " + path);

    // Lê o cabeçalho (bloco 0): é daqui que vem N, necessário para o hashing.
    file_.read_block(0, buf_.data());
    if (trace_enabled()) std::fprintf(stderr, "[trace] %s: le bloco 0 (cabecalho)\n", path.c_str());
    if (std::memcmp(buf_.data() + kHdrMagic, kMagic, sizeof kMagic) != 0)
        throw std::runtime_error("arquivo nao e um arquivo de dados do TP2: " + path);
    header_.block_size = get_u32(buf_.data() + kHdrBlockSize);
    header_.num_buckets = get_u32(buf_.data() + kHdrBuckets);
    header_.num_records = get_u64(buf_.data() + kHdrRecords);
    header_.overflow_blocks = get_u32(buf_.data() + kHdrOverflow);
    header_.record_bytes = get_u64(buf_.data() + kHdrRecordBytes);

    if (header_.block_size != blockio::kBlockSize) {
        throw std::runtime_error("arquivo criado com BLOCK_SIZE=" + std::to_string(header_.block_size) +
                                 ", mas o programa foi compilado com BLOCK_SIZE=" +
                                 std::to_string(blockio::kBlockSize) + "; refaca o upload");
    }
    if (header_.num_buckets == 0) throw std::runtime_error("cabecalho invalido (N = 0)");
}

bool HashFile::find(int id, Article& out, Rid& rid, std::uint32_t& chain_read) {
    chain_read = 0;
    // Começa pelo bloco primário do bucket h(ID) e segue a cadeia de overflow.
    std::uint32_t blk = bucket_block(hash_bucket(id, header_.num_buckets));
    while (blk != 0) {  // 0 = fim da cadeia (o bloco 0 é o cabeçalho)
        file_.read_block(blk, buf_.data());
        ++chain_read;
        if (trace_enabled())
            std::fprintf(stderr, "[trace] %s: le bloco %u (%s do bucket)\n", file_.path().c_str(), blk,
                         chain_read == 1 ? "primario" : "overflow");
        bool achou = false;
        for_each_record_in_block(buf_.data(), [&](std::uint16_t off, const std::uint8_t* rec) {
            if (!achou && peek_record_id(rec) == id) {
                decode_record(rec, out);
                rid = Rid{blk, off};
                achou = true;
            }
        });
        // O ID é chave: ao encontrá-lo, não há por que ler o resto da cadeia.
        if (achou) return true;
        blk = get_block_header(buf_.data()).next;
    }
    return false;
}

void HashFile::scan(const std::function<void(const Rid&, const std::uint8_t*)>& fn) {
    const std::uint64_t total = file_.block_count();
    // Ordem física: primários e depois overflow. A organização hash não
    // guarda nenhuma ordem de chave, então TODOS os blocos precisam ser lidos.
    for (std::uint64_t blk = 1; blk < total; ++blk) {
        file_.read_block(blk, buf_.data());
        for_each_record_in_block(buf_.data(), [&](std::uint16_t off, const std::uint8_t* rec) {
            fn(Rid{static_cast<std::uint32_t>(blk), off}, rec);
        });
    }
}

bool read_record_at(blockio::BlockFile& f, const Rid& rid, Article& out) {
    if (rid.block == 0 || rid.block >= f.block_count()) return false;
    std::vector<std::uint8_t> buf(blockio::kBlockSize);
    f.read_block(rid.block, buf.data());
    if (trace_enabled())
        std::fprintf(stderr, "[trace] %s: le bloco %u (registro no deslocamento %u)\n", f.path().c_str(), rid.block,
                     rid.offset);
    const DataBlockHeader h = get_block_header(buf.data());
    if (rid.offset < kDataBlockHeaderSize || rid.offset + kRecordFixedSize > kDataBlockHeaderSize + h.used)
        return false;
    decode_record(buf.data() + rid.offset, out);
    return true;
}

// ===========================================================================
// Impressão
// ===========================================================================

void print_record(const Article& a, const Rid& rid) {
    std::printf("  ID          : %d\n", a.id);
    std::printf("  Titulo      : %s\n", a.titulo.c_str());
    std::printf("  Ano         : %d\n", a.ano);
    std::printf("  Autores     : %s\n", a.autores.c_str());
    std::printf("  Citacoes    : %d\n", a.citacoes);
    std::printf("  Atualizacao : %s\n", a.atualizacao.c_str());
    std::printf("  Snippet     : %s\n", a.snippet.c_str());
    std::printf("  (RID: bloco %u, deslocamento %u)\n", rid.block, rid.offset);
}

void print_record_line(const Article& a) {
    std::printf("%d | %s | %d | %s | %d | %s | %s\n", a.id, a.titulo.c_str(), a.ano, a.autores.c_str(),
                a.citacoes, a.atualizacao.c_str(), a.snippet.c_str());
}

}  // namespace tp2
