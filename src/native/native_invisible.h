// native_invisible.h - draws que não podem mudar nenhum pixel (F1.3).
//
// Dois casos exatos, lidos dos registradores do próprio draw (referência: nfsmw-nx,
// nfsmw_nativo_dibujos.cpp, "Draws that paint nothing and are still shaded"):
//   1. blend "0 x origem + 1 x destino" (ADD) em todos os canais escritos: o resultado é o destino.
//   2. alpha test com função NEVER (0): o shader recompilado sempre faz clip(), todo fragmento morre.
// Em ambos, se o draw também não escreve Z nem stencil, ele não deixa rastro e pode ser pulado.
//
// Parte pura, sem SDK; usa os valores de xenos::BlendFactor/BlendOp (kZero=0, kOne=1, kAdd=0).

#pragma once

#include <cstdint>

namespace gh3::native::invisible {

struct Blend {
  uint32_t color_src;
  uint32_t color_dst;
  uint32_t color_op;
  uint32_t alpha_src;
  uint32_t alpha_dst;
  uint32_t alpha_op;
};

inline bool CopiesDestination(const Blend& b) {
  return b.color_src == 0 && b.color_dst == 1 && b.color_op == 0 &&
         b.alpha_src == 0 && b.alpha_dst == 1 && b.alpha_op == 0;
}

// color_mask: 4 bits por alvo de cor (16 no total). blends: um por alvo. Devolve true só quando o
// draw não altera cor, Z nem stencil — o caso conservador (não escreve Z/stencil) é deliberado.
inline bool DrawIsInvisible(uint32_t color_mask, const Blend blends[4], bool alpha_never,
                            bool writes_depth_or_stencil) {
  if (writes_depth_or_stencil) return false;
  if (alpha_never) return true;
  for (uint32_t i = 0; i < 4; ++i) {
    if (((color_mask >> (4 * i)) & 0xF) && !CopiesDestination(blends[i])) return false;
  }
  return true;
}

}  // namespace gh3::native::invisible
