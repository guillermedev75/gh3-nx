#!/bin/bash
# Abre o GH3 recompilado (macOS) com a GPU emulada. Dois cliques no Finder.
# Janela 1280x720 + vsync (FIFO): com o modo "immediate" em tela cheia, o MoltenVK trava em
# CAMetalLayer nextDrawable e a janela fica preta com o jogo rodando (medido em 2026-10-17).
# GH3_BUILD_DIR escolhe o build (padrão out/build/mac-arm64-p8sdk, o SDK do project8-sw; o preset 0.10.0 pronto foi aposentado).
# Teclado como guitarra. O GH3 trata o teclado como controle comum, não guitarra; no controle comum os trastes
# são verde = LT, vermelho = LB, amarelo = RB, azul = RT, laranja = A (confirmado jogando, 2026-10-17).
# 1-5 = trastes, setas = palhetada (D-pad), Espaço também = A (confirmar menu), Enter = Start, Tab = Back. Rebind no F4.
cd "$(dirname "$0")"
LOG_FILE="${GH3_LOG_FILE:-$PWD/out/last-run.log}"
exec "${GH3_BUILD_DIR:-out/build/mac-arm64-p8sdk}/gh3" --game_data_root="$PWD/extracted" --gpu_plugin xenos --log_file "$LOG_FILE" \
  --fullscreen=false --window_width=1280 --window_height=720 \
  --vulkan_allow_present_mode_immediate=false --vulkan_allow_present_mode_mailbox=false \
  --mnk_mode=true --mnk_mouse=false \
  --keybind_left_trigger=1 --keybind_left_shoulder=2 --keybind_right_shoulder=3 --keybind_right_trigger=4 --keybind_a=5,Space \
  --keybind_b=Backspace --keybind_x=X --keybind_y=Y \
  --keybind_dpad_up=Up --keybind_dpad_down=Down --keybind_dpad_left=Left --keybind_dpad_right=Right \
  --keybind_start=Return --keybind_back=Tab "$@"
