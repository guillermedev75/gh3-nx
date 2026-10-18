// switch_apm.h - pede o modo de GPU de 460,8 MHz no portátil e devolve o padrão ao sair.
//
// Portado de nfsmw-nx (sdk/src/ui/switch_apm.cpp). Fora do Switch tudo aqui é no-op. A configuração
// anterior é lida antes de trocar (apmGetPerformanceConfiguration) e restaurada em Restore(), em vez de
// fixar um valor "padrão" que pode variar entre firmwares.
#pragma once

#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "switch_apm_config.h"

#if defined(__SWITCH__)
#include <switch.h>
#endif

REXCVAR_DEFINE_BOOL(gh3_switch_gpu_boost, true, "GH3",
                    "No Switch portátil, pede o modo de GPU oficial de 460,8 MHz (apm 0x92220008)");

namespace gh3::apm {

#if defined(__SWITCH__)
inline bool g_active = false;
inline ApmPerformanceMode g_mode = ApmPerformanceMode_Normal;
inline uint32_t g_previous = 0;

inline void Apply() {
  if (R_FAILED(apmInitialize())) {
    REXLOG_WARN("apm performance configuration: apmInitialize falhou");
    return;
  }
  ApmPerformanceMode mode = ApmPerformanceMode_Invalid;
  if (R_FAILED(apmGetPerformanceMode(&mode))) mode = ApmPerformanceMode_Invalid;
  const uint32_t want = ChooseConfiguration(
      mode == ApmPerformanceMode_Boost ? Mode::Docked : Mode::Handheld,
      REXCVAR_GET(gh3_switch_gpu_boost));
  if (mode == ApmPerformanceMode_Invalid || !want) {
    REXLOG_INFO("apm performance configuration: sem alteração (modo {})", static_cast<int>(mode));
    apmExit();
    return;
  }
  if (R_FAILED(apmGetPerformanceConfiguration(mode, &g_previous))) g_previous = 0;
  if (R_FAILED(apmSetPerformanceConfiguration(mode, want))) {
    REXLOG_WARN("apm performance configuration 0x{:08X}: recusada pelo sistema", want);
    apmExit();
    return;
  }
  g_mode = mode;
  g_active = true;
  REXLOG_INFO("apm performance configuration 0x{:08X} (anterior 0x{:08X})", want, g_previous);
}

inline void Restore() {
  if (!g_active) return;
  if (g_previous) apmSetPerformanceConfiguration(g_mode, g_previous);
  REXLOG_INFO("apm performance configuration 0x{:08X} restaurada", g_previous);
  apmExit();
  g_active = false;
}
#else
inline void Apply() {}
inline void Restore() {}
#endif

}  // namespace gh3::apm
