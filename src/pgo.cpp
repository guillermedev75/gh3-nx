// pgo.cpp - dump do perfil do build instrumentado (GH3_PGO=gerar).
//
// Só entra no build de gravação (cmake/switch.cmake). Para cada objeto, o GCC embute no executável o
// caminho <GH3_PGO_DIR>/CMakeFiles#...#arquivo.gcda; esse caminho não existe no Switch, então o wrap de
// fopen redireciona para sdmc:/switch/gh3-nx/pgo/. O Switch não fecha o programa de forma limpa (o menu
// HOME mata), então uma thread grava e zera os contadores a cada 30 s; o libgcov soma ao que cada .gcda
// já tem, e vários dumps dão o total da sessão.
#if defined(GH3_PGO_GERAR) && defined(__SWITCH__)

#include <switch.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

extern "C" {
FILE* __real_fopen(const char* path, const char* mode);
void __gcov_dump(void);
void __gcov_reset(void);

FILE* __wrap_fopen(const char* path, const char* mode) {
  static constexpr char kPrefix[] = GH3_PGO_DIR;
  if (path && std::strncmp(path, kPrefix, sizeof(kPrefix) - 1) == 0) {
    const char* rest = path + sizeof(kPrefix) - 1;
    while (*rest == '\\' || *rest == '/') {
      ++rest;
    }
    std::string out = "sdmc:/switch/gh3-nx/pgo/";
    for (; *rest; ++rest) {
      out += (*rest == '\\') ? '/' : *rest;
    }
    return __real_fopen(out.c_str(), mode);
  }
  return __real_fopen(path, mode);
}
}  // extern "C"

namespace {

Thread g_pgo_thread;

void PgoThread(void*) {
  for (int dump = 1;; ++dump) {
    svcSleepThread(30LL * 1000 * 1000 * 1000);
    mkdir("sdmc:/switch/gh3-nx/pgo", 0777);
    const u64 start = armGetSystemTick();
    __gcov_dump();
    __gcov_reset();
    const double ms = double(armTicksToNs(armGetSystemTick() - start)) / 1e6;
    if (FILE* f = __real_fopen("sdmc:/switch/gh3-nx/pgo/dumps.txt", "a")) {
      std::fprintf(f, "dump %d: %.0f ms\n", dump, ms);
      std::fclose(f);
    }
  }
}

__attribute__((constructor)) void StartPgo() {
  mkdir("sdmc:/switch/gh3-nx/pgo", 0777);
  // Prioridade normal do jogo (0x3B): só acorda a cada 30 s.
  if (R_SUCCEEDED(threadCreate(&g_pgo_thread, PgoThread, nullptr, nullptr, 64 * 1024, 0x3B, -2))) {
    threadStart(&g_pgo_thread);
  }
}

}  // namespace

#endif  // GH3_PGO_GERAR && __SWITCH__
