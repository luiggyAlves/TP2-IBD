// check.hpp — mini-framework de testes (sem dependências externas).
//
// CHECK(cond) registra a falha com arquivo e linha, mas não aborta o teste:
// assim um único teste mostra todas as verificações que falharam.
// Ao final, tests_done() imprime o resumo e devolve o código de saída.

#ifndef TESTS_CHECK_HPP
#define TESTS_CHECK_HPP

#include <cstdio>
#include <cstdlib>
#include <string>

namespace testing {

inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }

inline void check(bool ok, const char* expr, const char* file, int line) {
    ++checks();
    if (!ok) {
        ++failures();
        std::fprintf(stderr, "FALHOU: %s (%s:%d)\n", expr, file, line);
    }
}

// Cria um diretório temporário exclusivo para os arquivos do teste.
inline std::string temp_dir() {
    char tmpl[] = "/tmp/tp2_test_XXXXXX";
    const char* d = mkdtemp(tmpl);
    if (d == nullptr) {
        std::perror("mkdtemp");
        std::exit(1);
    }
    return d;
}

inline int tests_done(const char* nome) {
    if (failures() == 0) {
        std::printf("%s: ok (%d verificacoes)\n", nome, checks());
        return 0;
    }
    std::printf("%s: %d de %d verificacoes FALHARAM\n", nome, failures(), checks());
    return 1;
}

}  // namespace testing

#define CHECK(cond) ::testing::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

#endif  // TESTS_CHECK_HPP
