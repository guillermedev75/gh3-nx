// switch_apm_config.h - escolha pura da configuração de desempenho (sem libnx, testável no host).
#pragma once

#include <cstdint>

namespace gh3::apm {

enum class Mode { Handheld, Docked };

// 0x92220008 = GPU 460,8 MHz + EMC 1331,2 MHz: o maior modo de GPU que o sistema oferece no portátil
// (o mesmo pedido do nfsmw-nx). No dock o sistema já dá mais (768 MHz): não mexer.
inline constexpr uint32_t kHandheldBoost = 0x92220008;

// Retorna a configuração a pedir, ou 0 quando não se deve mexer.
constexpr uint32_t ChooseConfiguration(Mode mode, bool boost) {
  return (boost && mode == Mode::Handheld) ? kHandheldBoost : 0;
}

}  // namespace gh3::apm
