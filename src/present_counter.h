// present_counter.h - índice de quadro do GH3 para a captura de paridade (parity_capture.h).
//
// O parity_capture.h do project8-sw lê o contador de swap do THPS na memória do guest (0x82970A10). O GH3
// não tem um endereço equivalente conhecido, e o SDK não expõe contagem de quadros ao app. O que existe é
// uma linha de log por quadro apresentado, a mesma que o tools/boot_test.py conta:
//   emulado: "XELOG_GPU PRESENT: swap_texture_view=0x... packet_size=..." (command_processor do Vulkan)
//   nativo:  "XELOG_GPU PRESENT native" (src/native/gh3_native_system.cpp, só quando desenhou)
// Os PRESENT de erro (NO PRESENTER, BeginSubmission FAILED, swap_texture_view=NULL) não apresentaram nada e
// não contam. As linhas são REXGPU_DEBUG: só existem com --log_level debug.
//
// Este arquivo não depende do SDK (tests/test_gameplay_capture.py compila com c++ puro); o sink de spdlog
// que chama Observe() fica no gh3_app.h.

#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

namespace gh3::present_counter {

inline bool IsPresent(std::string_view line) {
  constexpr std::string_view kTag = "XELOG_GPU PRESENT";
  size_t at = line.find(kTag);
  if (at == std::string_view::npos) return false;
  std::string_view rest = line.substr(at + kTag.size());
  if (rest.substr(0, 7) == " native") return true;
  constexpr std::string_view kView = ": swap_texture_view=";
  if (rest.substr(0, kView.size()) != kView) return false;
  return rest.substr(kView.size(), 4) != "NULL";
}

inline std::atomic<uint64_t>& Counter() {
  static std::atomic<uint64_t> presents{0};
  return presents;
}

inline void Observe(std::string_view line) {
  if (IsPresent(line)) Counter().fetch_add(1, std::memory_order_relaxed);
}

inline uint64_t Count() { return Counter().load(std::memory_order_relaxed); }

}  // namespace gh3::present_counter
