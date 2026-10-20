// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Controlador dinâmico da escala da cena (T31, gh3_native_scene_dynamic). Recebe o tempo entre swaps e
// escolhe a escala do T16 entre 0.65 e 1.0 em passos de 0.05, com histerese de alguns quadros e um
// tempo mínimo no mesmo nível: assim um pico isolado não muda a resolução e a imagem não oscila.
// Esta parte é só aritmética (níveis, histerese, tempo), sem GPU nem relógio.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gh3::native::scene {

struct DynamicConfig {
  double target_ms = 16.7;       // orçamento de um quadro a 60 FPS
  double over_margin_ms = 1.0;   // acima disso o quadro está estourado (60 -> 56,8 FPS)
  double under_margin_ms = 1.5;  // abaixo disso sobra orçamento para subir a resolução
  double hold_s = 0.6;           // tempo mínimo no mesmo nível (o plano pede 0,5-1 s)
  int frames_to_change = 3;      // histerese: 3 quadros seguidos no mesmo lado
  int step_levels = 1;           // passo de 0.05 (1/20)
  int min_level = 13;            // 0.65
  int max_level = 20;            // 1.0
};

class DynamicScale {
 public:
  DynamicScale() = default;
  explicit DynamicScale(DynamicConfig config, double start_scale) : config_(config) {
    // A escala inicial também cai na grade de 0.05 (0.73 -> 0.75), para os passos serem exatos.
    int level = int(std::lround(start_scale * 20.0));
    level_ = std::clamp(level, config_.min_level, config_.max_level);
  }

  double scale() const { return double(level_) / 20.0; }
  int level() const { return level_; }
  const DynamicConfig& config() const { return config_; }

  // Uma amostra por quadro: o intervalo entre swaps em ms. Devolve a escala a usar neste quadro.
  // Tempos inválidos (primeiro swap, hitch de carregamento) não contam para a histerese.
  double Update(double frame_ms) {
    if (!(frame_ms > 0.0) || frame_ms > 1000.0) return scale();
    hold_s_ += frame_ms / 1000.0;
    if (frame_ms > config_.target_ms + config_.over_margin_ms) {
      ++over_frames_;
      under_frames_ = 0;
    } else if (frame_ms < config_.target_ms - config_.under_margin_ms) {
      ++under_frames_;
      over_frames_ = 0;
    } else {
      over_frames_ = under_frames_ = 0;
    }
    if (hold_s_ < config_.hold_s) return scale();
    if (over_frames_ >= config_.frames_to_change && level_ > config_.min_level) {
      level_ = std::max(config_.min_level, level_ - config_.step_levels);
      ResetWindow();
    } else if (under_frames_ >= config_.frames_to_change && level_ < config_.max_level) {
      level_ = std::min(config_.max_level, level_ + config_.step_levels);
      ResetWindow();
    }
    return scale();
  }

  // Quadros seguidos no mesmo lado (para o log/diagnóstico).
  int over_frames() const { return over_frames_; }
  int under_frames() const { return under_frames_; }

 private:
  void ResetWindow() {
    hold_s_ = 0.0;
    over_frames_ = under_frames_ = 0;
  }

  DynamicConfig config_;
  int level_ = 20;  // 1.0
  double hold_s_ = 0.0;
  int over_frames_ = 0;
  int under_frames_ = 0;
};

}  // namespace gh3::native::scene
