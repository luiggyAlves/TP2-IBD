// Teste de sanidade da camada blockio: escreve, relê e confere os contadores.
// Compile e rode com: make test

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "blockio.hpp"

int main() {
    const std::string path = "/tmp/tp2_smoke.dat";
    std::vector<char> buf(blockio::kBlockSize, 0);

    blockio::BlockFile f;
    f.open(path, blockio::Mode::kTruncate);

    const std::uint64_t b0 = f.alloc_block();
    const std::uint64_t b1 = f.alloc_block();
    assert(b0 == 0 && b1 == 1);
    assert(f.block_count() == 2);

    std::memset(buf.data(), 0, buf.size());
    std::snprintf(buf.data(), buf.size(), "bloco um");
    f.write_block(b1, buf.data());

    f.reset_stats();
    std::memset(buf.data(), 0, buf.size());
    f.read_block(b1, buf.data());
    assert(std::string(buf.data()) == "bloco um");
    assert(f.stats().reads == 1);
    assert(f.stats().writes == 0);

    f.read_block(b0, buf.data());
    assert(f.stats().reads == 2);

    f.close();
    std::remove(path.c_str());
    std::printf("blockio ok (bloco de %zu bytes)\n", blockio::kBlockSize);
    return 0;
}
