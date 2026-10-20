// SPDX-License-Identifier: GPL-3.0-or-later
//
// F3.1 do plano 60 FPS: quanto o presenter espera fences (anel de paint e refresh do guest
// output) por janela de 10 s. O tempo é acumulado no runtime do SDK pelo patch local
// patches/rexglue-local/0002-present-fence-wait.patch; aqui só se formata a janela que o
// relatório de 10 s lê e zera. Puro: compila sem SDK nem GPU (testes compilam com c++).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace gh3 {
namespace native {
namespace present_wait {

struct Window {
  // Tempo parado em vkWaitForFences nos trackers do presenter na janela.
  uint64_t wait_ns = 0;
  // Quantas esperas de fence aconteceram na janela.
  uint64_t count = 0;
  // Quadros apresentados na janela (contexto para o leitor do log).
  uint64_t frames = 0;
};

inline std::string Format(const Window& window) {
  const double ms = double(window.wait_ns) / 1e6;
  char buffer[192];
  if (window.count) {
    const double each = ms / double(window.count);
    std::snprintf(buffer, sizeof(buffer),
                  "present fence wait %.1f ms/10 s (%llu waits, %.1f ms each, %llu frames)", ms,
                  static_cast<unsigned long long>(window.count), each,
                  static_cast<unsigned long long>(window.frames));
    return buffer;
  }
  std::snprintf(buffer, sizeof(buffer), "present fence wait %.1f ms/10 s (0 waits, %llu frames)",
                ms, static_cast<unsigned long long>(window.frames));
  return buffer;
}

}  // namespace present_wait
}  // namespace native
}  // namespace gh3
