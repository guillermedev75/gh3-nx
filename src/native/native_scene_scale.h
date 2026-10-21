// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Cena 3D em resolução menor que a HUD (T16, gh3_native_scene_scale). No Switch a GPU é o gargalo e o
// passe principal 720p com profundidade é o maior custo; a HUD vem depois, num passe sem profundidade
// no mesmo alvo de cor. O alvo 1280 usado COM profundidade passa a ser uma imagem escalada (ex. 960x540),
// e quando o mesmo alvo é usado sem profundidade (HUD) ou resolvido, um blit sobe a imagem escalada.
// Esta parte é só aritmética (tamanhos, scissor, região do blit), sem GPU.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gh3::native::scene {

// O alvo de cor da cena e da HUD do GH3 (pitch do host, em pixels).
constexpr uint32_t kScenePitch = 1280;

// 1.0 (ou inválido: <= 0, NaN) = desligado; abaixo de 0.25 a cena ficaria ilegível.
inline float ClampScale(float s) {
  if (!(s > 0.0f)) return 1.0f;
  if (s >= 0.999f) return 1.0f;
  return std::max(s, 0.25f);
}
inline bool Enabled(float s) { return s < 1.0f; }

// Pixels da imagem escalada: arredonda para o mais próximo (meio sobe); nunca zera um tamanho não nulo.
inline uint32_t ScaledSize(float s, uint32_t px) {
  if (!px) return 0;
  return std::max(1u, uint32_t(std::floor(double(px) * double(s) + 0.5)));
}
inline uint32_t UnscaledSize(float s, uint32_t px) {
  if (!px) return 0;
  return uint32_t(std::floor(double(px) / double(s) + 0.5));
}

// Um intervalo [offset, offset + extent) do jogo em pixels escalados: o início desce e o fim sobe, para
// nenhum pixel que o jogo desenharia ficar de fora.
struct ScaledSpan {
  uint32_t offset, extent;
};
inline ScaledSpan ScaleSpan(float s, uint32_t offset, uint32_t extent) {
  if (!extent) return {uint32_t(std::floor(double(offset) * double(s))), 0};
  uint32_t begin = uint32_t(std::floor(double(offset) * double(s)));
  uint32_t end = uint32_t(std::ceil(double(offset + extent) * double(s)));
  return {begin, end - begin};
}

// Região trocada entre a imagem escalada e o alvo cheio. A cheia é cortada pelo menor dos dois; a
// escalada acompanha.
struct SceneBlit {
  bool valid;
  uint32_t full_w, full_h;      // região do alvo cheio
  uint32_t scaled_w, scaled_h;  // a mesma região na imagem escalada
};
inline SceneBlit BlitExtent(float s, uint32_t scaled_width, uint32_t scaled_height, uint32_t full_width,
                            uint32_t full_height) {
  uint32_t fw = std::min(full_width, UnscaledSize(s, scaled_width));
  uint32_t fh = std::min(full_height, UnscaledSize(s, scaled_height));
  uint32_t sw = std::min(scaled_width, ScaledSize(s, fw));
  uint32_t sh = std::min(scaled_height, ScaledSize(s, fh));
  if (!fw || !fh || !sw || !sh) return {false, 0, 0, 0, 0};
  return {true, fw, fh, sw, sh};
}

// Um draw vai para a imagem escalada se escreve o alvo 1280 de 1 amostra e testa/escreve profundidade.
// alias_msaa != 0 é a imagem de alias da T15 (outro alvo); msaa != 0 é MSAA de host.
inline bool Qualifies(uint32_t host_pitch, bool depth_used, uint32_t msaa, uint32_t alias_msaa) {
  return host_pitch == kScenePitch && depth_used && !msaa && !alias_msaa;
}

// Com msaa_as_samples=false e sem MSAA de host (T15, padrão do Switch) o palco é desenhado na superfície
// MSAA do guest com 1 amostra (640x360 para 4x: já um quarto dos pixels) e espelhado no alvo 1x de pitch
// 1280. Escalar esse alvo (T16/T31) quebra o espelho: no Switch o palco saiu preto, só silhuetas e luzes
// (2026-10-21), e quase não economiza GPU. Nesse caminho a cena não escala.
inline bool ScaleAllowed(bool host_multisample, bool msaa_as_samples) {
  return host_multisample || msaa_as_samples;
}

}  // namespace gh3::native::scene
