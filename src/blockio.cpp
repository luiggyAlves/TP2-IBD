#include "blockio.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace blockio {
namespace {

std::runtime_error erro(const std::string& contexto, const std::string& path) {
    return std::runtime_error(contexto + " (" + path + "): " + std::strerror(errno));
}

}  // namespace

BlockFile::~BlockFile() { close(); }

void BlockFile::open(const std::string& path, Mode mode) {
    close();
    int flags = 0;
    switch (mode) {
        case Mode::kRead:      flags = O_RDONLY; break;
        case Mode::kReadWrite: flags = O_RDWR | O_CREAT; break;
        case Mode::kTruncate:  flags = O_RDWR | O_CREAT | O_TRUNC; break;
    }
    fd_ = ::open(path.c_str(), flags, 0644);
    if (fd_ < 0) throw erro("nao foi possivel abrir o arquivo", path);
    path_ = path;
    stats_ = Stats{};
}

void BlockFile::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void BlockFile::read_block(std::uint64_t index, void* buffer) {
    if (fd_ < 0) throw std::runtime_error("read_block: arquivo nao esta aberto");
    const off_t offset = static_cast<off_t>(index) * static_cast<off_t>(kBlockSize);
    std::size_t lido = 0;
    auto* dest = static_cast<char*>(buffer);
    while (lido < kBlockSize) {
        const ssize_t n = ::pread(fd_, dest + lido, kBlockSize - lido, offset + static_cast<off_t>(lido));
        if (n < 0) throw erro("falha na leitura do bloco", path_);
        if (n == 0) {  // fim do arquivo: completa com zeros
            std::memset(dest + lido, 0, kBlockSize - lido);
            break;
        }
        lido += static_cast<std::size_t>(n);
    }
    ++stats_.reads;
}

void BlockFile::write_block(std::uint64_t index, const void* buffer) {
    if (fd_ < 0) throw std::runtime_error("write_block: arquivo nao esta aberto");
    const off_t offset = static_cast<off_t>(index) * static_cast<off_t>(kBlockSize);
    std::size_t escrito = 0;
    const auto* src = static_cast<const char*>(buffer);
    while (escrito < kBlockSize) {
        const ssize_t n = ::pwrite(fd_, src + escrito, kBlockSize - escrito, offset + static_cast<off_t>(escrito));
        if (n <= 0) throw erro("falha na escrita do bloco", path_);
        escrito += static_cast<std::size_t>(n);
    }
    ++stats_.writes;
}

std::uint64_t BlockFile::alloc_block() {
    const std::uint64_t index = block_count();
    std::vector<char> zeros(kBlockSize, 0);
    write_block(index, zeros.data());
    return index;
}

std::uint64_t BlockFile::block_count() const {
    if (fd_ < 0) return 0;
    struct stat st;
    if (::fstat(fd_, &st) < 0) throw erro("falha ao consultar o tamanho", path_);
    return (static_cast<std::uint64_t>(st.st_size) + kBlockSize - 1) / kBlockSize;
}

}  // namespace blockio
