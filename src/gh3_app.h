// gh3 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#if !__has_include(<rex/crypto/sha256.h>)
// O SDK 0.10.0 pronto (tools/rexglue, preset mac-arm64-release) não tem rex/crypto/sha256.h, que a trava do
// default.xex usa. Erro claro em vez de trava desligada em silêncio.
#error "gh3 precisa do SDK patcheado: tools/setup_sdk.sh && tools/build_host_sdk.sh (preset mac-arm64-p8sdk)"
#endif

#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>

#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

#include "dump_gate.h"
#include "frame_stats.h"
#include "guest_waits.h"
#include "switch_apm.h"
#include "mark_screenshot.h"
#include "parity_capture.h"
#include "present_counter.h"
#include "rexglue_script_input.h"

#if GH3_NATIVE_RENDERER
#include <rex/system/gpu_plugin.h>

#include "native/gh3_native_system.h"

// O plugin xenos compilado dentro do executável (CMakeLists.txt, gh3_gpu_xenos).
extern "C" rex::system::IGraphicsSystem* rex_gpu_create(uint32_t abi_version,
                                                         const rex::system::GpuCreateInfo* info);
#endif

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <rex/graphics/pipeline/texture/util.h>
#include <vector>
#include <fstream>
#include <memory>
#include <string_view>

REXCVAR_DEFINE_STRING(mark_screenshot, "", "Parity",
                      "Comma-separated input-script marks to screenshot (\"*\" = every mark), "
                      "written to --mark_screenshot_dir");

// Conta os quadros apresentados para o índice da captura de paridade (src/present_counter.h). Sem mutex:
// Observe() só mexe num atômico.
class Gh3PresentCountSink : public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    gh3::present_counter::Observe(std::string_view(msg.payload.data(), msg.payload.size()));
  }
  void flush_() override {}
};

class Gh3App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Gh3App>(new Gh3App(ctx, "gh3",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  void OnPostInitLogging() override { rex::AddSink(std::make_shared<Gh3PresentCountSink>()); }
  // --input_json / --input_json_file (src/rexglue_script_input.h): pad roteirizado no slot 0, somado ao
  // teclado e aos controles reais. config/song_script.json leva do menu a uma música (tools/gameplay_capture.py).
  // --gh3_renderer=native: o renderer nativo do project8-sw (src/native/) substitui a GPU emulada
  // (Xenos) inteira. O padrão continua "xenos", criado pela cópia do plugin linkada no executável e não
  // pelo .dylib (ver gh3_gpu_xenos no CMakeLists.txt).
  void OnPreSetup(rex::RuntimeConfig& config) override {
    rexglue_script_input::Install(config);
#if GH3_NATIVE_RENDERER
    if (gh3::native::Enabled()) {
      config.graphics = gh3::native::CreateGraphicsSystem();
    } else if (config.gpu_plugin == "xenos") {
      rex::system::GpuCreateInfo info{};
      info.struct_size = sizeof(info);
      info.backend = "any";
      config.graphics.reset(rex_gpu_create(rex::system::kGpuPluginAbiVersion, &info));
    }
#endif
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  // GH3_DUMP_IMAGE=<arquivo>: grava a imagem já carregada (descriptografada) da memória do guest, para
  // tools/find_missing_funcs.py achar funções que a análise estática não registrou.
  void OnPostLoadXexImage() override {
    const char* out = std::getenv("GH3_DUMP_IMAGE");
    if (!out) return;
    const uint8_t* image = runtime()->virtual_membase() + PPCImageConfig.image_base;
    std::ofstream(out, std::ios::binary)
        .write(reinterpret_cast<const char*>(image), PPCImageConfig.image_size);
  }
  // Captura da saída do guest (src/parity_capture.h): parada até --parity_capture_frames, F10 ou uma marca
  // do roteiro pedida em --mark_screenshot (src/mark_screenshot.h), que vira mark_<nome>_<modo>.png.
  void OnPostSetup() override {
    parity_capture_.Start(runtime());
    // FPS no overlay de debug (F3 no PC, L+R+Cima no Switch). Sem provider a caixa abre vazia: o SDK não
    // conta quadros sozinho (src/frame_stats.h).
    SetGuestFrameStats([this] { return MuestreaFotograma(); });
    // Esperas ativas (swap/vblank) cedem o núcleo em vez de girar (src/guest_waits.h).
    gh3::guest_waits::Install(runtime());
    // GPU a 460,8 MHz no portátil (src/switch_apm.h); no-op fora do Switch.
    gh3::apm::Apply();
    std::string marks = REXCVAR_GET(mark_screenshot);
    if (!marks.empty()) {
      rexglue_script_input::SetMarkObserver(
          [this, marks](size_t, const std::string& name, int64_t t_ms) {
            if (gh3::mark_screenshot::Wanted(marks, name)) {
              parity_capture_.RequestNamedCapture(name, t_ms);
            }
            if (name == "gameplay") T9DumpProbe();  // T9 PROBE (temporário)
          });
    }
  }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override { parity_capture_.RegisterBinds(); }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  void OnShutdown() override {
    rexglue_script_input::SetMarkObserver({});
    parity_capture_.Stop();
    gh3::apm::Restore();
  }
  // Trava do default.xex (src/dump_gate.h): o último gancho antes do Runtime existir, então recusar
  // aqui não deixa nada pela metade. Sem flag para desligar.
  void OnConfigurePaths(rex::PathConfig& paths) override {
#ifdef __SWITCH__
    // O hbloader não passa argv; o disco extraído fica em game/ ao lado do NRO.
    if (paths.game_data_root.empty()) {
      paths.game_data_root = rex::filesystem::GetExecutableFolder() / "game";
    }
#endif
    dump_gate::Enforce(paths.game_data_root);
  }

  // T9 PROBE (temporário): GH3_T9_DUMP="ADDR:W:H,..." despeja regiões 32 bpp tiladas da memória do guest.
  void T9DumpProbe() {
    const char* e = std::getenv("GH3_T9_DUMP");
    if (!e) return;
    std::string s = e;
    size_t p = 0;
    while (p < s.size()) {
      size_t q = s.find(',', p);
      if (q == std::string::npos) q = s.size();
      std::string item = s.substr(p, q - p);
      p = q + 1;
      uint32_t addr = 0, w = 0, h = 0;
      if (std::sscanf(item.c_str(), "%x:%u:%u", &addr, &w, &h) != 3) continue;
      const uint8_t* base = runtime()->memory()->TranslatePhysical<const uint8_t*>(addr);
      std::vector<uint8_t> out(size_t(w) * h * 4);
      uint32_t pitch = (w + 31) & ~31u;
      for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
          int32_t off = rex::graphics::texture_util::GetTiledOffset2D(int32_t(x), int32_t(y), pitch, 2);
          const uint8_t* src = base + off;
          uint8_t* dst = &out[(size_t(y) * w + x) * 4];
          dst[0] = src[3]; dst[1] = src[2]; dst[2] = src[1]; dst[3] = src[0];
        }
      std::ofstream(fmt::format("out/t9/dump_{:08X}_{}x{}.raw", addr, w, h), std::ios::binary)
          .write(reinterpret_cast<const char*>(out.data()), out.size());
    }
  }

  // Quadros contados desde o início: o renderer nativo conta os swaps que apresentou
  // (gh3::native::SwapsPresented, sem depender do log); o emulado cai no present_counter, que lê as linhas
  // REXGPU_DEBUG (só existem com --log_level debug). Ver src/frame_stats.h.
  rex::ui::FrameStats MuestreaFotograma() {
    uint64_t total = 0;
#if GH3_NATIVE_RENDERER
    if (gh3::native::Enabled()) total = gh3::native::SwapsPresented();
#endif
    if (total == 0) total = gh3::present_counter::Count();
    const double now_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    const gh3::frame_stats::Stats s = frame_sampler_.Sample(total, now_ms);
    return rex::ui::FrameStats{s.frame_time_ms, s.fps, s.frame_count};
  }

 private:
  parity_capture::ParityCapture parity_capture_;
  gh3::frame_stats::Sampler frame_sampler_;
};
