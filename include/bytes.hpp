// bytes.hpp — leitura e escrita de inteiros em posições fixas de um buffer.
//
// Os blocos são buffers de bytes crus. Em vez de sobrepor structs C++ ao
// buffer (o que depende de alinhamento e preenchimento do compilador), cada
// campo é gravado/lido em um deslocamento explícito com memcpy. Assim o layout
// em disco é exatamente o documentado nos comentários de cada módulo.

#ifndef BYTES_HPP
#define BYTES_HPP

#include <cstdint>
#include <cstring>

namespace tp2 {

// ---- ordem nativa (little-endian no x86-64): usada nos campos de cabeçalho ----

inline void put_u16(std::uint8_t* p, std::uint16_t v) { std::memcpy(p, &v, sizeof v); }
inline void put_u32(std::uint8_t* p, std::uint32_t v) { std::memcpy(p, &v, sizeof v); }
inline void put_u64(std::uint8_t* p, std::uint64_t v) { std::memcpy(p, &v, sizeof v); }
inline void put_i32(std::uint8_t* p, std::int32_t v) { std::memcpy(p, &v, sizeof v); }

inline std::uint16_t get_u16(const std::uint8_t* p) { std::uint16_t v; std::memcpy(&v, p, sizeof v); return v; }
inline std::uint32_t get_u32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, sizeof v); return v; }
inline std::uint64_t get_u64(const std::uint8_t* p) { std::uint64_t v; std::memcpy(&v, p, sizeof v); return v; }
inline std::int32_t get_i32(const std::uint8_t* p) { std::int32_t v; std::memcpy(&v, p, sizeof v); return v; }

// ---- big-endian: usada nas CHAVES da B+Tree ----
// Com o byte mais significativo primeiro, comparar duas chaves com memcmp
// equivale a comparar os números. Isso permite que a B+Tree trate toda chave
// como uma sequência de bytes de tamanho fixo.

inline void put_be32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}
inline std::uint32_t get_be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}
inline void put_be16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v);
}
inline std::uint16_t get_be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

}  // namespace tp2

#endif  // BYTES_HPP
