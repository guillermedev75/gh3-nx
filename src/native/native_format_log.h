// Auditoria de formato (F0.4 do plano de 60 FPS): uma linha por alvo criado, com o VkFormat que o
// renderer escolheu, o formato do guest, se é 16F (o alvo caro que só se justifica com HDR real),
// gamma, profundidade e o clear inicial. A formatação é pura (sem SDK) para ser testada sozinha,
// como os outros headers de src/native; o renderer confere os valores de VkFormat com
// static_assert.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace gh3::native::format_log {

// Valores de VkFormat que o renderer usa (vulkan_core.h). O renderer tem static_asserts ligando
// cada um ao enum do Vulkan; aqui ficam como números para este header compilar sem o SDK.
enum VkFormatValue : uint32_t {
  kUndefined = 0,
  kR8G8B8A8Unorm = 37,
  kA2B10G10R10UnormPack32 = 64,
  kR16G16Snorm = 78,
  kR16G16Sfloat = 83,
  kR16G16B16A16Snorm = 92,
  kR16G16B16A16Sfloat = 97,
  kR32Sfloat = 100,
  kR32G32Sfloat = 103,
  kD32Sfloat = 126,
  kD24UnormS8Uint = 129,
  kD32SfloatS8Uint = 130,
};

inline std::string Name(uint32_t format) {
  switch (format) {
    case kR8G8B8A8Unorm:
      return "VK_FORMAT_R8G8B8A8_UNORM";
    case kA2B10G10R10UnormPack32:
      return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
    case kR16G16Snorm:
      return "VK_FORMAT_R16G16_SNORM";
    case kR16G16Sfloat:
      return "VK_FORMAT_R16G16_SFLOAT";
    case kR16G16B16A16Snorm:
      return "VK_FORMAT_R16G16B16A16_SNORM";
    case kR16G16B16A16Sfloat:
      return "VK_FORMAT_R16G16B16A16_SFLOAT";
    case kR32Sfloat:
      return "VK_FORMAT_R32_SFLOAT";
    case kR32G32Sfloat:
      return "VK_FORMAT_R32G32_SFLOAT";
    case kD32Sfloat:
      return "VK_FORMAT_D32_SFLOAT";
    case kD24UnormS8Uint:
      return "VK_FORMAT_D24_UNORM_S8_UINT";
    case kD32SfloatS8Uint:
      return "VK_FORMAT_D32_SFLOAT_S8_UINT";
    default: {
      char unknown[32];
      std::snprintf(unknown, sizeof(unknown), "VK_FORMAT_UNKNOWN_%u", format);
      return unknown;
    }
  }
}

struct Target {
  uint64_t frame = 0;
  // Endereço do guest (base_tiles << 12) e pitch em pixels do jogo.
  uint32_t base = 0;
  uint32_t pitch = 0;
  // Tamanho real da imagem Vulkan (depois do scene scale, se for o caso).
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t samples = 1;
  bool depth = false;
  // VkFormat escolhido e formato do guest (enum xenos) de origem.
  uint32_t vk_format = 0;
  uint32_t guest_format = 0;
  // Alvo 16F (R16G16B16A16_SFLOAT): candidato a sair se não houver HDR de verdade.
  bool half_float = false;
  // k_8_8_8_8_GAMMA: a conversão de gamma está no alvo.
  bool gamma = false;
  // Clear que o CreateOrGetRenderTarget faz ao criar: cor 0 ou profundidade 1.0.
  bool clear_color = false;
  bool clear_depth = false;
  // T16: imagem escalada da cena; T15: alias de amostras (log2 da contagem do guest).
  bool scene_scaled = false;
  uint32_t alias_msaa = 0;
};

inline std::string Format(const Target& t) {
  const uint32_t samples = t.samples ? t.samples : 1;
  const char* clear = t.clear_depth ? "depth=1.0" : (t.clear_color ? "color=0" : "none");
  char line[512];
  std::snprintf(line, sizeof(line),
                "format: frame=%llu %s base:%08X pitch:%u %ux%u samples:%u guest:%u vk:%s "
                "hdr16f:%d gamma:%d clear:%s scene:%d alias:%u",
                static_cast<unsigned long long>(t.frame), t.depth ? "depth" : "color", t.base,
                t.pitch, t.width, t.height, samples, t.guest_format, Name(t.vk_format).c_str(),
                t.half_float ? 1 : 0, t.gamma ? 1 : 0, clear, t.scene_scaled ? 1 : 0,
                t.alias_msaa);
  return line;
}

}  // namespace gh3::native::format_log
