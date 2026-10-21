// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Upscaler espacial no blit que sobe a cena escalada (T16) para o alvo cheio 1280x720 (T47/F1.5). O
// CAS (T32, `native_scene_cas.h`) troca o LINEAR por um resample com sharpen; o FSR1 (etapa EASU)
// troca o filtro por um upscale adaptativo de borda. O shader é o SPIR-V que o SDK já traz para o
// presenter (`guest_output_ffx_fsr_easu_ps`), com os push constants do FidelityFX FSR1; o RCAS
// (sharpen pós-upscale, `guest_output_ffx_fsr_rcas_ps`) é 1:1 e exige um intermediário — as
// constantes ficam aqui testadas, mas o renderer liga o EASU primeiro (ver PROGRESS do T47).
// Esta parte é só aritmética (modo, constantes, retângulo NDC, proporção e formato), sem GPU.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "native_scene_cas.h"  // CasRect, CasCompatible

namespace gh3::native::scene {

enum class UpscaleMode {
  kLinear,
  kCas,
  kFsr1,
};

inline std::string_view UpscaleModeName(UpscaleMode mode) {
  switch (mode) {
    case UpscaleMode::kCas:
      return "cas";
    case UpscaleMode::kFsr1:
      return "fsr1";
    default:
      return "linear";
  }
}

// "linear" | "cas" | "fsr1"; qualquer outro valor cai no LINEAR (nunca deixa a cena sem subida).
inline UpscaleMode ParseUpscaleMode(std::string_view name) {
  if (name == "cas") return UpscaleMode::kCas;
  if (name == "fsr1") return UpscaleMode::kFsr1;
  return UpscaleMode::kLinear;
}

inline bool UpscaleUsesFfx(UpscaleMode mode) {
  return mode == UpscaleMode::kCas || mode == UpscaleMode::kFsr1;
}

// FsrEasuCon: proporção origem/destino e o inverso do tamanho da origem. O EASU não tem output_offset
// (no presenter ele sempre escreve num intermediário); aqui escreve no próprio alvo cheio.
struct EasuEffect {
  float input_output_size_ratio[2];
  float input_size_inv[2];
};
static_assert(offsetof(EasuEffect, input_size_inv) == 8, "input_size_inv logo após o ratio");

struct EasuPushConstants {
  CasRect rect;
  EasuEffect effect;
};
static_assert(sizeof(EasuEffect) == 16, "EASU: dois vec2");
static_assert(offsetof(EasuPushConstants, effect) == 16, "o PS lê a partir do byte 16, como no SPIR-V");
static_assert(offsetof(EasuPushConstants, effect) + offsetof(EasuEffect, input_output_size_ratio) == 16,
              "ratio no byte 16, como no SPIR-V");
static_assert(offsetof(EasuPushConstants, effect) + offsetof(EasuEffect, input_size_inv) == 24,
              "input_size_inv no byte 24, como no SPIR-V");

// FsrRcasCon: sharpen pós-upscale (2^-stops). Só vale 1:1 (mesmo tamanho de entrada e saída).
struct RcasEffect {
  int32_t output_offset[2];
  float sharpness_post_setup;
};
static_assert(sizeof(RcasEffect) == 12, "RCAS: ivec2 + float");

struct RcasPushConstants {
  CasRect rect;
  RcasEffect effect;
};
static_assert(offsetof(RcasPushConstants, effect) + offsetof(RcasEffect, sharpness_post_setup) == 24,
              "sharpness no byte 24, como no SPIR-V");

struct EasuParams {
  bool valid;
  uint32_t width, height;  // região coberta no alvo cheio
  EasuPushConstants push;
};

// Região (0,0)-(dst_w,dst_h) do alvo target_w x target_h, alimentada pelos src_w x src_h pixels do
// canto da imagem escalada, que tem tex_w x tex_h (pode ser maior que a região: ex. 832x666 com a cena
// em 832x468). O EASU lê com UV normalizada, então input_size_inv é 1/tamanho da TEXTURA; a proporção
// é da região. O retângulo do VS é montado em NDC a partir do alvo inteiro (igual ao CAS).
inline EasuParams EasuFor(uint32_t src_w, uint32_t src_h, uint32_t tex_w, uint32_t tex_h,
                          uint32_t dst_w, uint32_t dst_h, uint32_t target_w, uint32_t target_h) {
  EasuParams p{};
  if (!src_w || !src_h || !dst_w || !dst_h || !target_w || !target_h) return p;
  if (src_w > tex_w || src_h > tex_h) return p;
  p.valid = true;
  p.width = dst_w;
  p.height = dst_h;
  p.push.rect.offset[0] = -1.0f;
  p.push.rect.offset[1] = -1.0f;
  p.push.rect.size[0] = float(dst_w) * 2.0f / float(target_w);
  p.push.rect.size[1] = float(dst_h) * 2.0f / float(target_h);
  p.push.effect.input_output_size_ratio[0] = float(src_w) / float(dst_w);
  p.push.effect.input_output_size_ratio[1] = float(src_h) / float(dst_h);
  p.push.effect.input_size_inv[0] = 1.0f / float(tex_w);
  p.push.effect.input_size_inv[1] = 1.0f / float(tex_h);
  return p;
}

struct RcasParams {
  bool valid;
  RcasPushConstants push;
};

// RCAS pós-setup (FsrRcasCon const0.x): 2^-stops. Fica em 1.0 com 0 stops (sem redução).
inline float RcasSetupSharpness(float sharpness_reduction_stops) {
  if (!(sharpness_reduction_stops >= 0.0f)) return 1.0f;  // NaN e negativo: sem redução
  return std::exp2(-sharpness_reduction_stops);
}

// RCAS é 1:1, então a região é o alvo inteiro e não há razão de escala a passar.
inline RcasParams RcasFor(uint32_t width, uint32_t height, float sharpness_reduction_stops) {
  RcasParams p{};
  if (!width || !height) return p;
  p.valid = true;
  p.push.rect = CasRect{{-1.0f, -1.0f}, {2.0f, 2.0f}};
  p.push.effect.output_offset[0] = 0;
  p.push.effect.output_offset[1] = 0;
  p.push.effect.sharpness_post_setup = RcasSetupSharpness(sharpness_reduction_stops);
  return p;
}

// O FSR1 usa o mesmo caminho de formato do CAS: alvo de cor opaco R8G8B8A8_UNORM.
inline bool Fsr1Compatible(bool enabled, bool depth, uint32_t source_format, uint32_t target_format) {
  return enabled && CasCompatible(true, depth, source_format, target_format);
}

}  // namespace gh3::native::scene
