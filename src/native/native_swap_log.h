// Log por swap (F0.2 do plano de 60 FPS): uma linha por swap com o que decide o que aparece na
// tela — frontbuffer, formato, clear do quadro, resolve que o alimentou, alvo da cena, alias e a
// espera de fence. A formatação é pura (sem SDK) para ser testada sozinha, como os outros headers
// de src/native.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace gh3::native::swap_log {

struct Entry {
  uint64_t frame = 0;
  // Endereço do frontbuffer no espaço do guest e tamanho apresentado.
  uint32_t frontbuffer = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  // VkFormat da imagem resolvida que o present lê (0 = não resolvida).
  uint32_t format = 0;
  // Tamanho da imagem de onde o present copia.
  uint32_t source_width = 0;
  uint32_t source_height = 0;
  // Último clear de cor/profundidade registrado no quadro (raw do RB_COLOR_CLEAR/RB_DEPTH_CLEAR e
  // o endereço/alvo que ele limpou).
  bool has_clear_color = false;
  uint64_t clear_color = 0;
  uint32_t clear_color_target = 0;
  bool has_clear_depth = false;
  uint32_t clear_depth = 0;
  uint32_t clear_depth_target = 0;
  // Último resolve do quadro: alvo de origem (base_tiles), formato e destino (RB_COPY_DEST_BASE).
  uint32_t resolve_count = 0;
  bool has_resolve = false;
  uint32_t resolve_src = 0;
  uint32_t resolve_src_format = 0;
  uint32_t resolve_dst = 0;
  // Alvo da cena no último passe do quadro (o que o jogo considerava "a tela").
  bool has_rt = false;
  uint32_t rt_base = 0;
  uint32_t rt_pitch = 0;
  uint32_t rt_format = 0;
  uint32_t rt_samples = 1;
  bool rt_depth = false;
  // Sequência do alias 4x->1x do T15 aplicada ao alvo nesse swap.
  uint64_t alias_seq = 0;
  // Espera de fence do quadro (submissões que o CPU esperou antes de gravar).
  uint64_t fence_wait_ns = 0;
  bool presented = false;
};

namespace detail {

inline std::string Clear(bool has, uint64_t value, uint32_t target) {
  char buffer[64];
  if (!has) {
    std::snprintf(buffer, sizeof(buffer), "-@%08X", target);
  } else if (value >> 32) {
    std::snprintf(buffer, sizeof(buffer), "%08X%08X@%08X", uint32_t(value >> 32),
                  uint32_t(value), target);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%08X@%08X", uint32_t(value), target);
  }
  return buffer;
}

}  // namespace detail

inline std::string Format(const Entry& e) {
  char line[768];
  const uint32_t samples = e.rt_samples ? e.rt_samples : 1;
  const std::string clear_color = detail::Clear(e.has_clear_color, e.clear_color,
                                                e.clear_color_target);
  const std::string clear_depth = detail::Clear(e.has_clear_depth, e.clear_depth,
                                                e.clear_depth_target);
  char resolve[128];
  if (e.has_resolve) {
    std::snprintf(resolve, sizeof(resolve), "%u %08X fmt:%u -> %08X", e.resolve_count,
                  e.resolve_src, e.resolve_src_format, e.resolve_dst);
  } else {
    std::snprintf(resolve, sizeof(resolve), "%u -", e.resolve_count);
  }
  char rt[128];
  if (e.has_rt) {
    std::snprintf(rt, sizeof(rt), "base:%08X pitch:%u fmt:%u samples:%u %s", e.rt_base,
                  e.rt_pitch, e.rt_format, samples, e.rt_depth ? "depth" : "color");
  } else {
    std::snprintf(rt, sizeof(rt), "-");
  }
  std::snprintf(line, sizeof(line),
                "swap: frame=%llu fb=%08X %ux%u fmt:%u src=%ux%u clear=%s depth=%s resolve=%s "
                "rt=%s alias:%llu fence:%.3fms presented:%d",
                static_cast<unsigned long long>(e.frame), e.frontbuffer, e.width, e.height,
                e.format, e.source_width, e.source_height, clear_color.c_str(),
                clear_depth.c_str(), resolve, rt, static_cast<unsigned long long>(e.alias_seq),
                double(e.fence_wait_ns) / 1e6, e.presented ? 1 : 0);
  return line;
}

}  // namespace gh3::native::swap_log
