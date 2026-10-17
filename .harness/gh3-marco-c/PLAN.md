# Plano — gh3-marco-c

Spec: docs/superpowers/specs/2026-10-17-gh3-nx-design.md
Plano detalhado: docs/superpowers/plans/2026-10-17-gh3-nx-marco-c-switch.md

<!--
Formato (o runner depende dele — não mude):
- [ ] T1: título curto e específico
  aceite: comando que sai 0 SÓ quando a tarefa está pronta (e falha antes dela)
  detalhe: ponteiro opcional (seção do plano detalhado, arquivo, issue)

Status: [ ] pendente · [x] feita · [!] bloqueada — só o runner muda status.
-->

## Tarefas

- [x] T1: SDK ReXGlue patcheado do código-fonte (série do project8-sw)
  aceite: .venv/bin/python -m pytest -q tests/test_setup_sdk.py && tools/setup_sdk.sh --check
  detalhe: plano detalhado, Task 1. Referência: ~/gh3-refs/project8-sw/docs/building.md §1
- [x] T2: GH3 no Mac compilado sobre o SDK patcheado (renderer emulado), 300 quadros
  aceite: GH3_BUILD_DIR=out/build/mac-arm64-p8sdk python3 tools/boot_test.py --frames 300 --timeout 180
  detalhe: plano detalhado, Task 2
- [x] T3: renderer nativo do project8-sw no Mac (--gh3_renderer=native), 300 quadros
  aceite: GH3_BUILD_DIR=out/build/mac-arm64-p8sdk python3 tools/boot_test.py --frames 300 --timeout 180 -- ./run-gh3.command --gh3_renderer=native --log_level debug
  detalhe: plano detalhado, Task 3. Referência: ~/gh3-refs/project8-sw/src/game/src/native/ e thps_p8_app.h
- [x] T4: toolchain Switch (Docker devkitA64) e driver NVK patcheado
  aceite: test -f out/driver-sdk/lib/libvulkan.a && nm out/driver-sdk/lib/libvulkan.a 2>/dev/null | grep -q vkCreateInstance
  detalhe: plano detalhado, Task 4
- [x] T5: NRO do GH3 validado
  aceite: .venv/bin/python -m pytest -q tests/test_check_nro.py && python3 tools/check_nro.py build/switch/gh3.nro
  detalhe: plano detalhado, Task 5
- [x] T6: trava do default.xex e pacote do SD sem conteúdo do jogo
  aceite: .venv/bin/python -m pytest -q tests/test_sd_package.py && tools/check_no_game_content.sh
  detalhe: plano detalhado, Task 6
- [x] T7: renderer nativo no Mac sem geometry shaders (HUD, partículas): rect/quad/point lists expandidos sem GS
  aceite: GH3_BUILD_DIR=out/build/mac-arm64-p8sdk python3 tools/boot_test.py --frames 600 --timeout 240 -- ./run-gh3.command --gh3_renderer=native --log_level debug && python3 tools/native_skips.py out/boot-test.log --reason 5 --max 0
  detalhe: MoltenVK não tem geometryShader; src/native/native_renderer.cpp faz `if (geometry != kNone && !properties.geometryShader) return fail(5)` e pula ~58 mil draws (HUD some). Usar o caminho sem GS que o plugin xenos do SDK já tem ("primitive fallback conversion/expansion paths"; procurar em sdk/src/graphics/vulkan, ex. expandir rectangle/quad lists em índices de triângulos e point sprites no vertex shader). No Switch (NVK com GS) manter o GS. Escrever tools/native_skips.py (soma o último "[native] skipped by reason: R:N ..." por motivo; sai 1 se o motivo passar de --max) com teste em tests/test_native_skips.py.
- [x] T8: roteiro de entrada até a gameplay + captura de quadro (renderer emulado como referência)
  aceite: .venv/bin/python -m pytest -q tests/test_gameplay_capture.py && python3 tools/gameplay_capture.py --renderer emulated --out out/parity/emulated.png && python3 tools/image_stats.py out/parity/emulated.png --max-white 0.15 --min-stddev 20
  detalhe: portar ~/gh3-refs/project8-sw/src/game/src/rexglue_script_input.h e parity_capture.h (GPL, NOTICE) para src/, ligados em src/gh3_app.h como no thps_p8_app.h. Roteiro do Guillerme (teclado ↔ pad): Start → baixo ×2 → A ×3 leva do menu a uma música no Quickplay; descobrir os delays (intro, telas) olhando capturas. Gravar em config/song_script.json. tools/gameplay_capture.py abre o jogo (GH3_BUILD_DIR=out/build/mac-arm64-p8sdk ./run-gh3.command --input_json_file=config/song_script.json --parity_capture_frames=N ...) e copia a captura para --out; escolher N no meio da música. tools/image_stats.py (PNG; fração de pixels quase brancos ≥245 nos 3 canais e desvio padrão) com testes. Sem dependências novas se der (PNG via zlib da stdlib).
- [ ] T9: renderer nativo desenha o palco 3D na gameplay (hoje sai branco atrás da HUD)
  aceite: python3 tools/gameplay_capture.py --renderer native --out out/parity/native.png && python3 tools/image_stats.py out/parity/native.png --max-white 0.15 --min-stddev 20 && python3 tools/image_stats.py out/parity/native.png --compare out/parity/emulated.png --max-mean-diff 40
  detalhe: com --gh3_renderer=native o palco 3D da gameplay sai branco (HUD e braço da guitarra certos; o palco 3D no menu aparece). Hipótese: o palco vai para um render target/resolve que é amostrado por um passe de composição (blur/bloom/DOF) e a textura do resolve não é achada ou sai com formato errado. Usar os contadores do [native] no log (resolves, "lookups answered empty", skipped by reason), comparar com o emulado (out/parity/emulated.png do T8). Corrigir no src/native/ sem quebrar o Switch (GS) e registrar a causa no docs/porting-playbook.md.
