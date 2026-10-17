// mark_screenshot.h - quais marcas do roteiro de entrada (rexglue_script_input.h) viram screenshot.
//
// --mark_screenshot=gameplay (ou "title,gameplay", ou "*" para todas): o gh3_app.h pede ao parity_capture.h
// uma captura nomeada quando o roteiro passa pela marca. A marca é relógio de parede, o mesmo das entradas,
// então cai no mesmo ponto da música nos dois renderers; o índice de quadro não (o nativo apresenta ~60
// quadros/s no menu, o emulado ~34). tools/gameplay_capture.py usa a marca "gameplay" por padrão.
//
// Sem SDK: tests/test_gameplay_parity.py compila com c++ puro.

#pragma once

#include <string_view>

namespace gh3::mark_screenshot {

inline bool Wanted(std::string_view list, std::string_view name) {
  while (!list.empty()) {
    size_t comma = list.find(',');
    std::string_view item = list.substr(0, comma);
    if (item == "*" || item == name) return true;
    if (comma == std::string_view::npos) break;
    list.remove_prefix(comma + 1);
  }
  return false;
}

}  // namespace gh3::mark_screenshot
