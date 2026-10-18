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
- [!] T9: renderer nativo desenha o palco 3D na gameplay (hoje sai branco atrás da HUD)
  aceite: python3 tools/gameplay_capture.py --renderer native --out out/parity/native.png && python3 tools/image_stats.py out/parity/native.png --max-white 0.15 --min-stddev 20 && python3 tools/image_stats.py out/parity/native.png --compare out/parity/emulated.png --max-mean-diff 40 && python3 tools/image_stats.py out/parity/native.png --compare out/parity/emulated.png --region 380,420,900,720 --max-mean-diff 30 && python3 tools/image_stats.py out/parity/native.png --compare out/parity/emulated.png --region 1000,480,1280,720 --max-mean-diff 30
  detalhe: ADIADA pelo Guillerme (2026-10-17): brilho fica para depois; o palco já aparece (commit wip). Com --gh3_renderer=native o palco 3D da gameplay sai branco (HUD e braço da guitarra certos; o palco 3D no menu aparece). Hipótese: o palco vai para um render target/resolve que é amostrado por um passe de composição (blur/bloom/DOF) e a textura do resolve não é achada ou sai com formato errado. Usar os contadores do [native] no log (resolves, "lookups answered empty", skipped by reason), comparar com o emulado (out/parity/emulated.png do T8). Corrigir no src/native/ sem quebrar o Switch (GS) e registrar a causa no docs/porting-playbook.md. ESTADO (2026-10-17): o palco já aparece (commit wip(T9): MSAA como amostras da EDRAM); falta o brilho — o nativo sai bem mais claro (média 83 vs 32 do emulado). O Guillerme viu uma tentativa que corrigiu o brilho mas sumiu com a HUD: por isso o aceite agora compara também a região do braço da guitarra (380,420,900,720) e do medidor de rock (1000,480,1280,720) com o emulado. tools/image_stats.py precisa ganhar --region x0,y0,x1,y1 (com teste). A referência out/parity/emulated.png deve ser capturada pela marca 'gameplay' (gameplay_capture.py --renderer emulated), não pelo quadro 2700.

## Marco D — desempenho (fila do nfsmw-nx; gargalo medido no Switch: CPU do código do jogo, ~12 ms/quadro com o renderer esperando o jogo; ~18 quadros de jogo/s)

- [x] T10: medidor de FPS da gameplay no Mac (linha de base para o marco D)
  aceite: .venv/bin/python -m pytest -q tests/test_gameplay_fps.py && python3 tools/gameplay_fps.py --renderer native --seconds 30 --out out/perf/baseline.json && python3 -c "import json;d=json.load(open('out/perf/baseline.json'));assert d['game_fps']>0 and d['swaps_per_s']>0"
  detalhe: tools/gameplay_fps.py roda o roteiro (config/song_script.json) com --gh3_renderer=native até a marca de gameplay e lê as linhas "[native] last 10 s: ... draws ... swaps" e "per frame: ... waits for the game, N ms waiting" depois da marca; grava JSON com swaps_per_s, draws_per_s, wait_ms, e game_fps estimado (draws_per_s / draws de um quadro de gameplay, medido no próprio run como mediana de draws por swap quando o renderer não espera o jogo). Documentar o método no playbook §5. Testes com logs sintéticos.
- [ ] T11: registradores PowerPC como variáveis locais no codegen (cr/xer/ctr/non_volatile)
  aceite: python3 -c "import re,glob;n=sum(len(re.findall(r'ctx\.(xer|ctr)\b',open(f).read())) for f in glob.glob('generated/default/gh3_recomp.*.cpp'));print(n);assert n==0" && python3 tools/gameplay_capture.py --renderer native --out out/parity/native-t11.png && python3 tools/image_stats.py out/parity/native-t11.png --max-white 0.15 --min-stddev 20 && python3 tools/gameplay_fps.py --renderer native --seconds 30 --out out/perf/t11.json --min-vs out/perf/baseline.json
  detalhe: ligar no gh3_manifest.toml as opções de codegen cr_as_local, xer_as_local, ctr_as_local e non_volatile_as_local (hoje todas false em generated/default/gh3_init.cpp). No nfsmw-nx (~/gh3-refs/nfsmw-nx/docs/toolchain.md §Registers as C++ locals) isso tirou ~14% do thread principal (__savegprlr/__restgprlr). Cuidado: funções partidas leem registradores antes de escrevê-los — nfsmw usa tools/lee_antes.py e a opção share_registers do codegen (sdk/src/codegen/builders); portar o necessário. --min-vs falha se game_fps cair mais de 5% vs a linha de base. Medir e registrar no playbook o que mudou (contagem de ctx.*, FPS Mac).
- [ ] T12: chamadas diretas entre funções traduzidas + LTO no build do Switch
  aceite: .venv/bin/python -m pytest -q tests/test_direct_calls.py && python3 tools/gameplay_capture.py --renderer native --out out/parity/native-t12.png && python3 tools/image_stats.py out/parity/native-t12.png --max-white 0.15 --min-stddev 20 && python3 tools/gameplay_fps.py --renderer native --seconds 30 --out out/perf/t12.json --min-vs out/perf/baseline.json && test -f build/switch/gh3.nro && python3 tools/check_nro.py build/switch/gh3.nro
  detalhe: portar ~/gh3-refs/nfsmw-nx/tools/llamadas_directas.py (rodar após o codegen, como o split_register) e ligar LTO no cmake/switch.cmake (nfsmw toolchain.md §Direct calls/LTO). Rebuild do NRO no Docker (comando no playbook §1, com -j 6). Conferir `nm build/switch/gh3 | grep " _start$"` = 0 se mexer em ordenação.
- [ ] T13: esperas ativas do jogo dormem em vez de girar (swap/vblank/jobs)
  aceite: .venv/bin/python -m pytest -q tests/test_guest_waits.py && python3 tools/gameplay_capture.py --renderer native --out out/parity/native-t13.png && python3 tools/image_stats.py out/parity/native-t13.png --max-white 0.15 --min-stddev 20 && grep -q "guest waits: installed" out/parity/native-t13.capture/game.log
  detalhe: o project8-sw (mesmo motor Neversoft) troca as esperas em src/game/src/guest_swap_wait.h, guest_job_wait.h, guest_render_wait.h, guest_ring_wait.h por versões que dormem/cedem, nos endereços do THPS. Achar as funções equivalentes no GH3 (assinatura das instruções do laço; o THPS e o GH3 compartilham o motor), instalar os mesmos ganchos e logar "guest waits: installed N" ao iniciar. Cada gancho devolve exatamente o que o laço original devolveria. Cvars para ligar/desligar cada um (padrão ligado no Switch).
- [ ] T14: modo de GPU oficial 460,8 MHz no portátil (apm 0x92220008)
  aceite: .venv/bin/python -m pytest -q tests/test_switch_apm.py && test -f build/switch/gh3.nro && strings build/switch/gh3 | grep -q "apm performance configuration"
  detalhe: portar ~/gh3-refs/nfsmw-nx/sdk/src/ui/switch_apm.cpp (e o uso em app/src/nfsmw_ajustes_graficos.cpp): pedir 0x92220008 (GPU 460,8 + EMC 1331,2) no portátil, voltar ao padrão ao sair, logar "apm performance configuration 0x...". Cvar gh3_switch_gpu_boost (padrão true). Teste da escolha de configuração (portátil/dock) sem hardware. Rebuild do NRO.
- [ ] T15: palco com 1 amostra por pixel no Switch sem voltar o branco (custo de GPU do MSAA-como-amostras)
  aceite: python3 tools/gameplay_capture.py --renderer native --out out/parity/native-t15.png -- --gh3_native_msaa_as_samples=false && python3 tools/image_stats.py out/parity/native-t15.png --max-white 0.15 --min-stddev 20 && grep -q 'Render target .* 1x MSAA' out/parity/native-t15.capture/game.log
  detalhe: medido no Switch (clocks de fábrica, 1 jogador, 18:31): GPU ~31-36 ms reais/quadro e CPU do renderer 22-28 ms. O conserto do T9 desenha o palco MSAA 4x como amostras no alvo 1x de pitch 2P (4x os pixels). Fazer o palco funcionar com gh3_native_msaa_as_samples=false: desenhar com 1 amostra e fazer o alvo que o jogo limpa/reaproveita (o 1x de pitch 2P) apontar para a mesma imagem (ex.: resolver/aliasar só a amostra 0, ou mapear as leituras do resolve para o alvo 1x). gameplay_capture.py deve aceitar cvars extras depois de --. Brilho fora do escopo (T9 adiada): não comparar com o emulado no aceite; o log precisa mostrar os alvos com 1 amostra. Depois trocar o padrão no switch/gh3.toml. Registrar no playbook.
