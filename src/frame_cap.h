// frame_cap.h - trava de quadro por modo do Switch (portátil / dock), parte pura testável no host.
//
// Num jogo de ritmo, quadro irregular pesa mais que FPS. No portátil a GPU (460,8 MHz) não sustenta 60, então
// o padrão é 30 cravados (2 vblanks); no dock (768 MHz) sustenta, então 60 (1 vblank). O modo é consultado
// durante o jogo: encaixar ou tirar do dock troca a trava na hora.
#pragma once

#include <algorithm>
#include <cstdint>

namespace gh3::frame_cap {

// Mínimo de vblanks de 60 Hz entre quadros. `floor` é a trava global (gh3_min_frame_vblanks), que continua
// valendo como piso; 0 nos valores por modo vira 1.
inline uint32_t Vblanks(bool docked, uint32_t handheld, uint32_t docked_vblanks, uint32_t floor) {
  const uint32_t by_mode = std::max<uint32_t>(1, docked ? docked_vblanks : handheld);
  return std::max<uint32_t>(std::max<uint32_t>(1, floor), by_mode);
}

}  // namespace gh3::frame_cap
