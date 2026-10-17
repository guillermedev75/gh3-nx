// gh3 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>

#include "dump_gate.h"

#if GH3_NATIVE_RENDERER
#include <rex/system/gpu_plugin.h>

#include "native/gh3_native_system.h"

// O plugin xenos compilado dentro do executável (CMakeLists.txt, gh3_gpu_xenos).
extern "C" rex::system::IGraphicsSystem* rex_gpu_create(uint32_t abi_version,
                                                         const rex::system::GpuCreateInfo* info);
#endif

#include <cstdlib>
#include <fstream>

class Gh3App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Gh3App>(new Gh3App(ctx, "gh3",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
#if GH3_NATIVE_RENDERER
  // --gh3_renderer=native: o renderer nativo do project8-sw (src/native/) substitui a GPU emulada
  // (Xenos) inteira. O padrão continua "xenos", criado pela cópia do plugin linkada no executável e não
  // pelo .dylib (ver gh3_gpu_xenos no CMakeLists.txt).
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (gh3::native::Enabled()) {
      config.graphics = gh3::native::CreateGraphicsSystem();
    } else if (config.gpu_plugin == "xenos") {
      rex::system::GpuCreateInfo info{};
      info.struct_size = sizeof(info);
      info.backend = "any";
      config.graphics.reset(rex_gpu_create(rex::system::kGpuPluginAbiVersion, &info));
    }
  }
#endif
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
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
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
};
