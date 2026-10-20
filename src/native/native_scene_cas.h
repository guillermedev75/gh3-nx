// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// CAS (Contrast Adaptive Sharpening, FidelityFX) no blit que sobe a cena escalada do T16 para o alvo
// cheio 1280x720 (T32/F1.1c). O blit atual é LINEAR, que borra ainda mais a imagem que já vem de 0.65;
// o CAS realça as bordas na mesma passada de upscale. O shader é o SPIR-V que o SDK já traz para o
// presenter (`guest_output_ffx_cas_resample_ps`), com os mesmos push constants; esta parte é só
// aritmética (constantes, retângulo NDC, proporção e formato), sem GPU.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gh3::native::scene {

// 0 desliga (nem o sharpen 0 é útil); 1 é o máximo do FidelityFX.
constexpr float kDefaultCasSharpness = 0.2f;

inline float ClampCasSharpness(float sharpness) {
  if (!(sharpness >= 0.0f)) return 0.0f;  // NaN e negativo desligam
  return std::min(sharpness, 1.0f);
}
inline bool CasEnabled(bool enabled, float sharpness) {
  return enabled && ClampCasSharpness(sharpness) > 0.0f;
}

// CasSetup const1.x do FidelityFX, como em sdk/include/rex/ui/presenter.h
// (CalculateCasPostSetupSharpness): o shader espera o valor já "pós-setup".
inline float CasSetupSharpness(float sharpness) {
  return -1.0f / (8.0f - 3.0f * ClampCasSharpness(sharpness));
}

// O shader de resample lê texels inteiros e escreve floats normalizados; o alvo da cena é
// R8G8B8A8_UNORM. Outro formato (ou profundidade) cai no blit LINEAR.
constexpr uint32_t kCasR8G8B8A8Unorm = 37;  // VK_FORMAT_R8G8B8A8_UNORM
inline bool CasCompatible(bool enabled, bool depth, uint32_t source_format, uint32_t target_format) {
  return enabled && !depth && source_format == kCasR8G8B8A8Unorm && target_format == kCasR8G8B8A8Unorm;
}

// Push constants do par VS (`guest_output_triangle_strip_rect_vs`) + PS
// (`guest_output_ffx_cas_resample_ps`). O VS espera o retângulo em NDC nos bytes 0-15; o PS lê
// output_offset/ratio/sharpness a partir do byte 16 (o layout que o presenter do SDK usa).
struct CasRect {
  float offset[2];  // NDC do canto superior esquerdo
  float size[2];    // NDC do tamanho
};
struct CasEffect {
  int32_t output_offset[2];  // origem da região no framebuffer (0,0)
  float input_output_ratio[2];  // pixels da origem / pixels do destino
  float sharpness_post_setup;   // ver CasSetupSharpness
};
struct CasPushConstants {
  CasRect rect;
  CasEffect effect;
};
static_assert(sizeof(CasRect) == 16, "VS push constants: 2 + 2 floats");
static_assert(sizeof(CasEffect) == 20, "PS push constants: ivec2 + vec2 + float");
static_assert(offsetof(CasPushConstants, effect) == 16, "o PS lê a partir do byte 16");
static_assert(offsetof(CasPushConstants, effect) + offsetof(CasEffect, input_output_ratio) == 24,
              "ratio no byte 24, como no SPIR-V");
static_assert(offsetof(CasPushConstants, effect) + offsetof(CasEffect, sharpness_post_setup) == 32,
              "sharpness no byte 32, como no SPIR-V");

struct CasParams {
  bool valid;
  uint32_t width, height;  // região coberta no alvo cheio
  CasPushConstants push;
};

// Região (0,0)-(dst_w,dst_h) do alvo target_w x target_h, alimentada por src_w x src_h pixels da
// imagem escalada. O retângulo do VS é montado em NDC a partir do alvo inteiro.
inline CasParams CasFor(uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
                        uint32_t target_w, uint32_t target_h, float sharpness) {
  CasParams p{};
  if (!src_w || !src_h || !dst_w || !dst_h || !target_w || !target_h) return p;
  p.valid = true;
  p.width = dst_w;
  p.height = dst_h;
  p.push.rect.offset[0] = -1.0f;
  p.push.rect.offset[1] = -1.0f;
  p.push.rect.size[0] = float(dst_w) * 2.0f / float(target_w);
  p.push.rect.size[1] = float(dst_h) * 2.0f / float(target_h);
  p.push.effect.output_offset[0] = 0;
  p.push.effect.output_offset[1] = 0;
  p.push.effect.input_output_ratio[0] = float(src_w) / float(dst_w);
  p.push.effect.input_output_ratio[1] = float(src_h) / float(dst_h);
  p.push.effect.sharpness_post_setup = CasSetupSharpness(sharpness);
  return p;
}

}  // namespace gh3::native::scene
