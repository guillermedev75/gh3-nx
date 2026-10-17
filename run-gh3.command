#!/bin/bash
# Abre o GH3 recompilado (macOS) com a GPU emulada. Dois cliques no Finder.
# Janela 1280x720 + vsync (FIFO): com o modo "immediate" em tela cheia, o MoltenVK trava em
# CAMetalLayer nextDrawable e a janela fica preta com o jogo rodando (medido em 2026-10-17).
cd "$(dirname "$0")"
exec out/build/mac-arm64-release/gh3 --game_data_root="$PWD/extracted" --gpu_plugin xenos --log_file "$PWD/out/last-run.log" \
  --fullscreen=false --window_width=1280 --window_height=720 \
  --vulkan_allow_present_mode_immediate=false --vulkan_allow_present_mode_mailbox=false "$@"
