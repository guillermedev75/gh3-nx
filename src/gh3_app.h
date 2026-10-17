// gh3 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/runtime.h>

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
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
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
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
