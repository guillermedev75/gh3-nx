// Rastreamento de texturas (depuracao): uma linha por textura ligada a um draw, com o endereco do
// guest, o formato, o tamanho, o pitch, tiling, mips, o VkImage/ImageView que o renderer escolheu e
// se a textura veio de um destino de resolve (e de uma copia de tamanho exato, o caminho `Sized`).
// A formatação é pura (sem SDK) para ser testada sozinha, como os outros headers de src/native.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace gh3::native::texture_log {

struct Entry {
  uint64_t frame = 0;
  // Indice do draw dentro do frame (0 = primeiro submetido).
  uint64_t draw = 0;
  // 0 = vertex, 1 = pixel.
  uint32_t stage = 0;
  // Constante de fetch da textura.
  uint32_t slot = 0;
  // Endereco do guest (base_page << 12) e formato do guest (enum xenos).
  uint32_t address = 0;
  uint32_t guest_format = 0;
  uint32_t dimension = 0;
  // Como o shader le a fetch constant (FetchOpDimension) e se pede os componentes com sinal: uma
  // mesma fetch constant vira mais de uma binding quando e' lida como 2D e como 3D/stacked.
  uint32_t bind_dimension = 1;
  bool signed_components = false;
  // Tamanho pedido pela fetch constant, em texels.
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t layers = 1;
  uint32_t pitch = 0;
  uint32_t mip_min = 0;
  uint32_t mip_max = 0;
  bool tiled = false;
  bool packed_mips = false;
  // VkImageView/ViewId que o renderer ligou (0 quando nao ha view).
  uint64_t view_id = 0;
  uint64_t image = 0;
  // Resolve: o endereco e um destino de resolve, com o tamanho da imagem dele. `sized` marca a
  // amostragem num tamanho diferente do resolve (copia de tamanho exato, ResolveDestination::Sized).
  bool resolve = false;
  bool sized = false;
  uint32_t resolve_width = 0;
  uint32_t resolve_height = 0;
  // Scissor do draw e a passada de renderizacao em que ele caiu.
  int32_t scissor_x = 0;
  int32_t scissor_y = 0;
  uint32_t scissor_width = 0;
  uint32_t scissor_height = 0;
  std::string pass;
};

inline const char* DimensionName(uint32_t dimension) {
  switch (dimension) {
    case 0:
      return "1D";
    case 1:
      return "2D";
    case 2:
      return "3D/stacked";
    case 3:
      return "cube";
    default:
      return "?";
  }
}

inline std::string Format(const Entry& e) {
  char line[720];
  if (e.resolve) {
    std::snprintf(
        line, sizeof(line),
        "tex f=%llu d=%llu %s slot=%u addr=%08X fmt=%u dim=%u bind=%s signed=%d %ux%ux%u "
        "pitch=%u tiled=%d packed=%d mips=%u..%u view=%016llX image=%016llX src=resolve(%ux%u%s) "
        "scissor=%d,%d %ux%u pass=%s",
        static_cast<unsigned long long>(e.frame), static_cast<unsigned long long>(e.draw),
        e.stage ? "ps" : "vs", e.slot, e.address, e.guest_format, e.dimension,
        DimensionName(e.bind_dimension), e.signed_components ? 1 : 0, e.width, e.height, e.layers,
        e.pitch, e.tiled ? 1 : 0, e.packed_mips ? 1 : 0, e.mip_min, e.mip_max,
        static_cast<unsigned long long>(e.view_id), static_cast<unsigned long long>(e.image),
        e.resolve_width, e.resolve_height, e.sized ? " sized" : "", e.scissor_x, e.scissor_y,
        e.scissor_width, e.scissor_height, e.pass.c_str());
  } else {
    std::snprintf(
        line, sizeof(line),
        "tex f=%llu d=%llu %s slot=%u addr=%08X fmt=%u dim=%u bind=%s signed=%d %ux%ux%u "
        "pitch=%u tiled=%d packed=%d mips=%u..%u view=%016llX image=%016llX src=texture "
        "scissor=%d,%d %ux%u pass=%s",
        static_cast<unsigned long long>(e.frame), static_cast<unsigned long long>(e.draw),
        e.stage ? "ps" : "vs", e.slot, e.address, e.guest_format, e.dimension,
        DimensionName(e.bind_dimension), e.signed_components ? 1 : 0, e.width, e.height, e.layers,
        e.pitch, e.tiled ? 1 : 0, e.packed_mips ? 1 : 0, e.mip_min, e.mip_max,
        static_cast<unsigned long long>(e.view_id), static_cast<unsigned long long>(e.image),
        e.scissor_x, e.scissor_y, e.scissor_width, e.scissor_height, e.pass.c_str());
  }
  return line;
}

}  // namespace gh3::native::texture_log
