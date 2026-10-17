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

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string_view>

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
  // Captura da saída do guest (src/parity_capture.h): parada até --parity_capture_frames ou F10.
  void OnPostSetup() override { parity_capture_.Start(runtime()); }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override { parity_capture_.RegisterBinds(); }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  void OnShutdown() override { parity_capture_.Stop(); }
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

 private:
  parity_capture::ParityCapture parity_capture_;
};
