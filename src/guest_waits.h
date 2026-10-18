// guest_waits.h - faz as esperas ativas do jogo cederem o núcleo em vez de girar.
//
// O QUE FOI ACHADO (código gerado, 2026-10-18)
//
// O GH3 é o mesmo motor Neversoft do project8-sw, e o desenho da espera é o mesmo. O laço quente do
// thread de render:
//
//   loc_822CA360:  bl sub_82172718        ; lê o contador de swap (u64 em 0x82AE2528)
//                  lwz r11,32260(r30)    ; deadline
//                  cmpld cr6,r3,r11
//                  blt cr6,loc_822CA360  ; gira enquanto contador < deadline
//
// e o contador é um accessor folha:
//
//   lis r11,-32082 ; ld r3,9512(r11) ; blr
//
// Ao lado dele há o segundo contador, de vblank/present (u64 em 0x82AE2520), no mesmo par a 8 bytes do
// THPS, lido por sub_82172758 e usado por outro laço (sub_822BF0F8, loc_822BF184).
//
// No Switch o processo tem 3 núcleos (máscara 0x7) e o log de perfil medido mostra 282% de 300%: a
// máquina está saturada, e o thread XThreadDC1A5080 sozinho queima 90% de um núcleo girando nesse laço.
// É CPU tirada justamente do thread que precisa avançar o contador para o giro acabar.
//
// O QUE ISTO FAZ
//
// Substitui os dois accessors por versões que chamam o original e, quando o mesmo valor volta várias
// vezes seguidas no mesmo thread, cedem a timeslice (sleep curto por padrão). O contrato é preservado:
// o convidado sempre recebe o valor real do contador; nada é falseado, pulado ou reordenado. Se a
// máquina estiver ociosa, o sleep devolve na hora e isto custa um branch.
//
// A troca é por símbolo (o `sub_X` gerado é fraco; o gancho abaixo é forte), então as chamadas diretas
// `__imp__sub_X` do T12 precisam voltar a `sub_X` nesses dois endereços: ver tools/llamadas_diretas.py.
//
// Padrão: ligado no Switch (onde o giro custa 1/3 da máquina), desligado fora dele (regra da casa para
// patch de comportamento do convidado). As cvars valem em runtime, sem reinstalar.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

#include "guest_waits_spin.h"

#if defined(__SWITCH__)
REXCVAR_DEFINE_BOOL(gh3_guest_waits, true, "GH3",
                    "Cede o núcleo quando um thread convidado fica girando no contador de quadro");
#else
REXCVAR_DEFINE_BOOL(gh3_guest_waits, false, "GH3",
                    "Cede o núcleo quando um thread convidado fica girando no contador de quadro");
#endif
REXCVAR_DEFINE_UINT32(gh3_guest_waits_after, 4, "GH3",
                      "Quantas leituras idênticas seguidas contam como giro");
REXCVAR_DEFINE_UINT32(gh3_guest_waits_us, 100, "GH3",
                      "Microssegundos de sleep ao estabelecer o giro; 0 usa yield");
REXCVAR_DEFINE_BOOL(gh3_vblank_late_no_wait, false, "GH3",
                    "Deixa um frame atrasado seguir na hora em vez de esperar o alvo+2 (dois ticks "
                    "de 16,7 ms). Sem ganho medido no GH3: os frames lentos sao do proprio jogo, "
                    "nao da quantizacao de vblank");
REXCVAR_DEFINE_BOOL(gh3_guest_waits_stats, false, "GH3",
                    "Loga a contagem de cedências a cada 4096");

namespace gh3::guest_waits {

// Contador de swap (fim de quadro) e de vblank/present. Ficam aqui, e não num comentário, para o teste
// conferir que são os mesmos endereços que tools/llamadas_diretas.py mantém fora da reescrita.
inline constexpr uint32_t kSwapCounter = 0x82172718;
inline constexpr uint32_t kVblankCounter = 0x82172758;
// sub_822BF0F8 (espera de vblank da Main): alvo do intervalo, e as duas leituras do contador.
// A 1a (lr) detecta o atraso; a do laço (loop_lr) espera o alvo. Ver VblankLateSkip.
inline constexpr uint32_t kVblankTarget = 0x82BF78A8;
inline constexpr uint32_t kVblankLateReadLr = 0x822BF158;
inline constexpr uint32_t kVblankLoopReadLr = 0x822BF188;

// Parte pura em src/guest_waits_spin.h (SpinDetector).
inline std::atomic<uint64_t> g_yields{0};

inline void AfterRead(uint64_t value) {
  if (!REXCVAR_GET(gh3_guest_waits)) return;

  // Por thread: dois threads lendo contadores diferentes não podem ser confundidos com um girando.
  thread_local SpinDetector detector;
  if (!detector.OnValue(value, REXCVAR_GET(gh3_guest_waits_after))) return;

  g_yields.fetch_add(1, std::memory_order_relaxed);
  if (REXCVAR_GET(gh3_guest_waits_stats)) {
    const uint64_t n = g_yields.load(std::memory_order_relaxed);
    if ((n & 0xFFF) == 0) REXLOG_INFO("guest waits: yields={}", n);
  }

  // Sleep de verdade, não só sched_yield: sob CFS um thread que cede e continua elegível é escolhido de
  // novo e não libera o núcleo (medido no project8-sw). A espera aqui é de um quadro inteiro (~16-20 ms),
  // então ~100 us não pesa na latência e tira quase todo o giro.
  const uint32_t micros = REXCVAR_GET(gh3_guest_waits_us);
  if (micros == 0) {
    std::this_thread::yield();
  } else {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
  }
}

inline uint64_t yields() { return g_yields.load(std::memory_order_relaxed); }

// Chamar no OnPostSetup, depois do registro das funções. Os ganchos valem por símbolo; isto só confere
// que os endereços existem no dump carregado e registra quantos entraram.
inline int Install(rex::Runtime* runtime) {
  auto* dispatcher = runtime ? runtime->function_dispatcher() : nullptr;
  int installed = 0;
  if (dispatcher && dispatcher->GetFunction(kSwapCounter)) ++installed;
  if (dispatcher && dispatcher->GetFunction(kVblankCounter)) ++installed;
  REXLOG_INFO("guest waits: installed {} (swap {:08X}, vblank {:08X}), enabled={}",
              installed, kSwapCounter, kVblankCounter, REXCVAR_GET(gh3_guest_waits));
  return installed;
}

}  // namespace gh3::guest_waits

// Os corpos originais (o `__imp__` do gerado). O gancho forte `sub_X` substitui o alias fraco e continua
// podendo chamar o original.
extern "C" void __imp__sub_82172718(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82172758(PPCContext& ctx, uint8_t* base);

REX_HOOK_RAW(sub_82172718) {
  __imp__sub_82172718(ctx, base);
  gh3::guest_waits::AfterRead(ctx.r3.u64);
}

REX_HOOK_RAW(sub_82172758) {
  static thread_local gh3::guest_waits::VblankLateSkip late_skip;
  const bool late_read =
      REXCVAR_GET(gh3_vblank_late_no_wait) && ctx.lr == gh3::guest_waits::kVblankLateReadLr;
  const bool loop_read =
      REXCVAR_GET(gh3_vblank_late_no_wait) && ctx.lr == gh3::guest_waits::kVblankLoopReadLr;
  __imp__sub_82172758(ctx, base);
  const uint64_t counter = ctx.r3.u64;
  if (late_read) {
    late_skip.OnLateRead(counter, REX_LOAD_U64(gh3::guest_waits::kVblankTarget));
  } else if (loop_read) {
    const uint64_t target = REX_LOAD_U64(gh3::guest_waits::kVblankTarget);
    if (late_skip.OnLoopRead(counter, target)) ctx.r3.u64 = target;
  }
  gh3::guest_waits::AfterRead(counter);
}
