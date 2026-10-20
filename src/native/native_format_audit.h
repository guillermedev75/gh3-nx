// Auditoria de formatos (F1.4 do plano de 60 FPS): fecha o log por alvo do F0.4 numa linha só, com a
// conta do que os alvos custam, e aponta os candidatos a corte — 16F (só se justifica com HDR de
// verdade), gamma, guest de 64bpp e profundidade de 24 bits guardada em 8 bytes/pixel (o
// gh3_native_depth24 usa 4). Puro (sem SDK), como os outros headers de src/native; o renderer
// alimenta um Target por alvo criado e loga o Report uma vez por conjunto de formatos.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "native_format_log.h"

namespace gh3::native::format_audit {

// Bytes por pixel de cada VkFormat que o renderer cria (vulkan_core.h). Desconhecido conta 4.
inline uint32_t BytesPerPixel(uint32_t format) {
  switch (format) {
    case format_log::kR16G16B16A16Snorm:
    case format_log::kR16G16B16A16Sfloat:
    case format_log::kR32G32Sfloat:
    case format_log::kD32SfloatS8Uint:
      return 8;
    default:
      return 4;
  }
}

// Formatos do guest: enums de ColorRenderTargetFormat/DepthRenderTargetFormat (xenos.h). O header é
// puro, então os valores ficam aqui como números; o renderer os passa direto do key.format.
enum GuestFormatValue : uint32_t {
  kColor8_8_8_8Gamma = 1,
  kColor16_16_16_16 = 5,
  kColor16_16_16_16Float = 7,
  kColor32_32Float = 15,
  kDepth24S8 = 0,
  kDepth24FS8 = 1,
};

inline bool Is64bppGuest(uint32_t guest_format) {
  return guest_format == kColor16_16_16_16 || guest_format == kColor16_16_16_16Float ||
         guest_format == kColor32_32Float;
}

struct Target {
  uint32_t vk_format = 0;
  uint32_t guest_format = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t samples = 1;
  bool depth = false;
};

struct Totals {
  uint64_t targets = 0;
  uint64_t color = 0;
  uint64_t depth = 0;
  uint64_t half_float = 0;
  uint64_t gamma = 0;
  uint64_t bpp64 = 0;
  uint64_t depth_guest24 = 0;
  // Profundidade do guest de 24 bits guardada em 8 bytes/pixel (D32_SFLOAT_S8_UINT): o alvo caro
  // que o gh3_native_depth24 corta para 4.
  uint64_t depth_stored_wide = 0;
  // Bytes desses alvos: metade deles é o que o gh3_native_depth24 economiza (8 -> 4 B/px).
  uint64_t depth_wide_bytes = 0;
  uint64_t bytes = 0;
};

class Audit {
 public:
  void Add(const Target& t) {
    const uint32_t samples = t.samples ? t.samples : 1;
    const uint64_t pixels = uint64_t(t.width) * uint64_t(t.height) * samples;
    totals_.targets++;
    totals_.bytes += pixels * BytesPerPixel(t.vk_format);
    if (t.depth) {
      totals_.depth++;
      if (t.guest_format == kDepth24S8 || t.guest_format == kDepth24FS8) {
        totals_.depth_guest24++;
        if (BytesPerPixel(t.vk_format) > 4) {
          totals_.depth_stored_wide++;
          totals_.depth_wide_bytes += pixels * BytesPerPixel(t.vk_format);
        }
      }
    } else {
      totals_.color++;
      if (t.vk_format == format_log::kR16G16B16A16Sfloat) totals_.half_float++;
      if (t.guest_format == kColor8_8_8_8Gamma) totals_.gamma++;
      if (Is64bppGuest(t.guest_format)) totals_.bpp64++;
    }
    bool known = false;
    for (const auto& seen : seen_) {
      if (seen.vk_format == t.vk_format && seen.guest_format == t.guest_format &&
          seen.depth == t.depth) {
        known = true;
        break;
      }
    }
    if (!known) {
      seen_.push_back({t.vk_format, t.guest_format, t.depth});
      signature_++;
    }
    bool counted = false;
    for (auto& by_format : formats_) {
      if (by_format.first == t.vk_format) {
        by_format.second++;
        counted = true;
        break;
      }
    }
    if (!counted) formats_.push_back({t.vk_format, 1});
  }

  const Totals& totals() const { return totals_; }
  bool Empty() const { return totals_.targets == 0; }
  // Muda quando aparece um (VkFormat, formato do guest) novo: o renderer loga uma vez por conjunto.
  uint64_t Signature() const { return signature_; }

  std::string Report(uint64_t frame) const {
    if (Empty()) return std::string();
    std::string formats;
    for (const auto& by_format : formats_) {
      if (!formats.empty()) formats += ',';
      formats += format_log::Name(by_format.first);
      formats += '=';
      formats += std::to_string(by_format.second);
    }
    std::string expensive;
    if (totals_.half_float) expensive += "16f";
    if (totals_.gamma) {
      if (!expensive.empty()) expensive += ',';
      expensive += "gamma";
    }
    if (totals_.bpp64) {
      if (!expensive.empty()) expensive += ',';
      expensive += "bpp64";
    }
    if (totals_.depth_stored_wide) {
      if (!expensive.empty()) expensive += ',';
      expensive += "depth24-storage";
    }
    if (expensive.empty()) expensive = "none";
    std::string hint;
    if (totals_.depth_stored_wide) {
      hint = " hint:gh3_native_depth24=true guarda a profundidade de 24 bits em 4B/px";
    }
    if (totals_.half_float) {
      hint += hint.empty() ? " hint:" : " ";
      hint += "16f:8B/px, so vale com HDR de verdade";
    }
    char line[1024];
    std::snprintf(line, sizeof(line),
                  "format audit: frame=%llu targets:%llu color:%llu depth:%llu "
                  "bytes:%llu memory:%.1fMiB formats:%s 16f:%llu gamma:%llu bpp64:%llu "
                  "depth_guest24:%llu depth_stored_wide:%llu depth24_save:%.1fMiB "
                  "expensive:%s%s",
                  static_cast<unsigned long long>(frame),
                  static_cast<unsigned long long>(totals_.targets),
                  static_cast<unsigned long long>(totals_.color),
                  static_cast<unsigned long long>(totals_.depth),
                  static_cast<unsigned long long>(totals_.bytes),
                  double(totals_.bytes) / (1024.0 * 1024.0), formats.c_str(),
                  static_cast<unsigned long long>(totals_.half_float),
                  static_cast<unsigned long long>(totals_.gamma),
                  static_cast<unsigned long long>(totals_.bpp64),
                  static_cast<unsigned long long>(totals_.depth_guest24),
                  static_cast<unsigned long long>(totals_.depth_stored_wide),
                  double(totals_.depth_wide_bytes) / (2.0 * 1024.0 * 1024.0), expensive.c_str(),
                  hint.c_str());
    return std::string(line);
  }

 private:
  struct Seen {
    uint32_t vk_format;
    uint32_t guest_format;
    bool depth;
  };
  Totals totals_;
  std::vector<std::pair<uint32_t, uint64_t>> formats_;
  std::vector<Seen> seen_;
  uint64_t signature_ = 0;
};

}  // namespace gh3::native::format_audit
