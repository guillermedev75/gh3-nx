#!/bin/bash
# Abre o GH3 recompilado (macOS) com a GPU emulada. Dois cliques no Finder.
# Janela 1280x720 + vsync (FIFO): com o modo "immediate" em tela cheia, o MoltenVK trava em
# CAMetalLayer nextDrawable e a janela fica preta com o jogo rodando (medido em 2026-10-17).
# Teclado como guitarra: 1-5 = trastes (verde A, vermelho B, amarelo Y, azul X, laranja LB),
# setas cima/baixo = palhetada (D-pad), Enter = Start, Tab = Back (star power). Rebind no F4.
cd "$(dirname "$0")"
exec out/build/mac-arm64-release/gh3 --game_data_root="$PWD/extracted" --gpu_plugin xenos --log_file "$PWD/out/last-run.log" \
  --fullscreen=false --window_width=1280 --window_height=720 \
  --vulkan_allow_present_mode_immediate=false --vulkan_allow_present_mode_mailbox=false \
  --mnk_mode=true --mnk_mouse=false \
  --keybind_a=1 --keybind_b=2 --keybind_y=3 --keybind_x=4 --keybind_left_shoulder=5 \
  --keybind_dpad_up=Up --keybind_dpad_down=Down --keybind_dpad_left=Left --keybind_dpad_right=Right \
  --keybind_start=Return --keybind_back=Tab "$@"
