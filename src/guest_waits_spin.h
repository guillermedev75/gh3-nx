// guest_waits_spin.h - a parte pura do guest_waits.h, sem SDK.
//
// Fica separado para o tests/test_guest_waits.py compilar com c++ puro (como o
// src/native/native_primitive_expand.h): o guest_waits.h de verdade inclui cvars, hooks e o runtime.

#pragma once

#include <cstdint>

namespace gh3::guest_waits {

// Decide, por thread, se a leitura atual de um contador é repetição suficiente para ceder o núcleo.
// `last` começa em ~0 para a primeira leitura nunca contar como repetição de um valor real.
struct SpinDetector {
  uint64_t last = ~uint64_t(0);
  uint32_t repeats = 0;

  bool OnValue(uint64_t value, uint32_t after) {
    if (value == last) {
      if (++repeats >= after) {
        repeats = 0;
        return true;
      }
    } else {
      last = value;
      repeats = 0;
    }
    return false;
  }
};

}  // namespace gh3::guest_waits
