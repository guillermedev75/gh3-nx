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
  // A configuração vale por modo: registra-se a do portátil (Normal) mesmo com o jogo aberto no dock, e o
  // sistema a aplica ao sair do dock. Pedir para o modo atual deixava o portátil a 307,2 MHz quando o jogo
  // abria no dock (2026-10-23).
  ApmPerformanceMode current = ApmPerformanceMode_Invalid;
  if (R_FAILED(apmGetPerformanceMode(&current))) current = ApmPerformanceMode_Invalid;
  const uint32_t want = ChooseConfiguration(Mode::Handheld, REXCVAR_GET(gh3_switch_gpu_boost));
  if (!want) {
    REXLOG_INFO("apm performance configuration: sem alteração (boost desligado)");
    apmExit();
    return;
  }
  if (R_FAILED(apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &g_previous))) g_previous = 0;
  if (R_FAILED(apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, want))) {
    REXLOG_WARN("apm performance configuration 0x{:08X}: recusada pelo sistema", want);
    apmExit();
    return;
  }
  g_mode = ApmPerformanceMode_Normal;
  g_active = true;
  REXLOG_INFO("apm performance configuration 0x{:08X} para o portátil (anterior 0x{:08X}, modo atual {})", want,
              g_previous, static_cast<int>(current));
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
