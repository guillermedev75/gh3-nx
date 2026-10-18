// frame_stats.h - FPS do overlay de debug (F3 no PC, L+R+Cima no Switch).
//
// O SDK desenha a caixa do overlay, mas a linha "Guest: X FPS" só aparece se o app registrar um provider
// com SetGuestFrameStats; sem isso a caixa abre vazia (o nfsmw-nx faz o mesmo, MuestreaFotograma). Este
// arquivo é a parte pura: um contador monotônico de quadros + o relógio viram FPS suavizado, sem SDK, para
// o tests/test_frame_stats.py compilar com c++ puro (como o src/native/native_primitive_expand.h).
//
// Recomputa no máximo a cada 1 s, sobre os quadros daquela janela, e suaviza com a janela anterior para o
// número não tremer. É deliberadamente uma estimativa grosseira (média de janela), não o frame time real.

#pragma once

#include <cstdint>

namespace gh3::frame_stats {

struct Stats {
  uint64_t frame_count = 0;
  double fps = 0.0;
  double frame_time_ms = 0.0;
};

class Sampler {
 public:
  // `total` = quadros contados até agora (monotônico), `now_ms` = milissegundos desde uma origem fixa
  // qualquer. O overlay não desenha se frame_count for 0, então nunca devolvemos 0 ali.
  Stats Sample(uint64_t total, double now_ms) {
    stats_.frame_count = total > 0 ? total : 1;
    if (!started_) {
      started_ = true;
      last_ms_ = now_ms;
      frames_ = total;
      return stats_;
    }
    const double dt_ms = now_ms - last_ms_;
    if (dt_ms < 1000.0) return stats_;

    const uint64_t done = total - frames_;
    last_ms_ = now_ms;
    frames_ = total;
    if (done == 0) {
      // O jogo não apresentou nada na janela inteira.
      smooth_ms_ = 0.0;
      stats_.fps = 0.0;
      stats_.frame_time_ms = dt_ms;
      return stats_;
    }
    const double ms_per_frame = dt_ms / double(done);
    // Depois de muito tempo com o overlay fechado, a janela anterior não vale mais.
    smooth_ms_ = (smooth_ms_ <= 0.0 || dt_ms > 5000.0)
                     ? ms_per_frame
                     : (smooth_ms_ + ms_per_frame) * 0.5;
    stats_.frame_time_ms = smooth_ms_;
    stats_.fps = 1000.0 / smooth_ms_;
    return stats_;
  }

 private:
  bool started_ = false;
  uint64_t frames_ = 0;
  double last_ms_ = 0.0;
  double smooth_ms_ = 0.0;
  Stats stats_;
};

}  // namespace gh3::frame_stats
