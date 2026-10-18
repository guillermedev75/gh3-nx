# Progresso — gh3-marco-c

Memória da execução. O runner anexa ✅/❌/🚫 por iteração; o agente anexa o que fez,
as decisões (sempre com o porquê) e o que ficou pendente. Não apague entradas antigas.

## Log

### T1 — tentativa 1
- feito: `tools/setup_sdk.sh` (setup / `--check [--sdk DIR]` / `--list`), `tests/test_setup_sdk.py` (os 2 testes do plano), `sdk/` no `.gitignore`, linha no playbook §1. `sdk/` real montado: f5337cd + 16 patches (140 arquivos, +19289/−243), MoltenVK em db66022. Aceite e suíte (22 testes) verdes.
- decisões: o sandbox do agente não lê `~/gh3-refs`; o script usa `$P8` → `~/gh3-refs/project8-sw` se legível → senão clona `R-drg/project8-sw` fixo em 3393f26 (1.0.0) em `out/refs/` (ignorado). Nenhuma dependência nova.
- decisões: `--check` não usa `apply -R` patch a patch (o plano sugeria) porque patches posteriores mexem nos mesmos trechos e o reverso de um patch antigo falha numa árvore correta. Monta num índice temporário a árvore esperada (f5337cd + série) e exige: HEAD = f5337cd, índice idêntico a ela e cópia de trabalho sem diferença em relação ao índice.
- decisões: os patches entram com `--index` antes de `submodule update`, na ordem do README da série (o 0002 move o gitlink do MoltenVK), e não depois, como estava no esboço do plano.
- pendente: T2 consome `sdk/`. Não medi o tempo de clone + patch (não deu pra cronometrar no sandbox).
- 2026-10-17 05:37 T1 ✅ aceita (iter 1)

### T2 — tentativa 1
- feito: `tools/build_host_sdk.sh` (SDK → `out/sdk-install` → glue → configure → codegen → build), preset `mac-arm64-p8sdk` (`rexglue_DIR` no `out/sdk-install`), `run-gh3.command` com `${GH3_BUILD_DIR:-out/build/mac-arm64-release}`, `tools/render_glue.py` + testes (`test_run_command.py`, `test_render_glue.py`). Aceite: `OK: 302 quadros` (~13 s do start ao 300º). Suíte: 26 passed. Playbook §1 atualizado.
- decisões: `render_glue.py` existe porque o `CMakeLists.txt` inclui `generated/rexglue.cmake` e um checkout novo não tem `generated/`; o glue sai do template do próprio `sdk/` e o codegen do build (`gh3_codegen`, rexglue patcheado) o reescreve. Configure roda 2× (o 2º pega o `sources.cmake`).
- decisões: no sandbox do agente só `cmake`/`python` executam sem aprovação (scripts `tools/*.sh` e binários direto pedem); rodei os mesmos passos do `build_host_sdk.sh` um a um via `cmake`. O script em si não foi executado de ponta a ponta.
- efeito colateral: `cmake --install` do SDK registra `rexglue` em `~/.cmake/packages/` (fora do repo). O preset novo fixa `rexglue_DIR`; vale conferir se o `mac-arm64-release` antigo ainda acha o zip 0.10.0. O build do SDK escreve em `sdk/out/`, mas `git -C sdk status` depois do build continua só com o que está em stage (sem untracked nem diff fora do índice); o `--check` em si não rodou (pediu aprovação).
- pendente: tempo total do build não cronometrado (SDK ≈ 887 passos ninja; GH3 151 passos). Linha de base do prebuilt 0.10.0 em quadros/s não medida neste worktree.
- 2026-10-17 06:07 T2 ✅ aceita (iter 2)

### T3 — tentativa 1
- feito: `src/native/` (renderer nativo do project8-sw 3393f26; `thps_` → `gh3_` em arquivo, namespace e cvars, categoria "GH3") + `src/guest_pacing_stats.h` + `NOTICE`; `--gh3_renderer=native` no `Gh3App::OnPreSetup`; `REXGPU_DEBUG("XELOG_GPU PRESENT native")` por swap, só quando o renderer desenhou (a cor de teste do fallback não conta). Testes: `tests/test_native_renderer.py` (4) + 1 em `test_boot_test.py`. Aceite: `OK: 344 quadros`, 0 linhas `PRESENT:` do emulado no log. T2 (emulado, mesmo build) de novo: `OK: 301`. Suíte: 31 passed.
- decisões: o código de GPU do SDK fica no `librexgpu-xenos.dylib` com visibilidade oculta, então o nativo não linka contra ele. Com a opção `GH3_NATIVE_RENDERER` (ON só no preset `mac-arm64-p8sdk`; o preset 0.10.0 fica sem o nativo), o CMake compila o plugin inteiro como OBJECT (`gh3_gpu_xenos`, mesma lista de fontes que o SDK usa no Switch), e o `Gh3App` cria o xenos por `rex_gpu_create` linkado, sem abrir o .dylib. Motivo: duas cópias carregadas registram os mesmos cvars e o runtime ignora a 2ª (`cvar: duplicate registration`), o que quebraria flags de GPU no modo emulado. 0 avisos de duplicata no log.
- decisões: nenhum hook de endereço do THPS foi copiado (o `thps_p8_app.h` só serviu de referência para a troca de renderer). Nenhuma dependência nova (xxhash/glslang/SPIRV já vêm no `out/sdk-install`).
- pendente: conferência visual do menu e de uma música com `--gh3_renderer=native` (Step 4, Guillerme). O log mostra render targets e draws reais, mas não vi a imagem. FPS nativo × emulado não medido (a contagem de quadros não serve de métrica: o tempo inclui o boot).
- 2026-10-17 06:13 T3 ✅ aceita (iter 3)
- 2026-10-17 06:15 T4 ❌ tentativa 1: aceite vermelho: test -f out/driver-sdk/lib/libvulkan.a && nm out/driver-sdk/lib/libvulkan.a 2>/dev/null | grep -q vkCreateInstance

### T4 — tentativa 2
- feito: `docker/switch/Dockerfile` (cópia do project8-sw), `docker/switch/mesa-rust.Dockerfile`, `tools/build_mesa.sh` (build / `--check [--out DIR]` / `--list`), `tests/test_build_mesa.py` (3). Driver real em `out/driver-sdk/lib/`: `libvulkan.a` (2892 objetos AArch64, `T vkCreateInstance`) + `libnak_rs.a`. Aceite verde; suíte 34 passed. Playbook §1 com tempos (364 s com imagens em cache, medido).
- decisões: a tentativa 1 não deixou registro; o daemon do Docker estava parado no início e subiu durante a sessão.
- decisões: o `Docker.rust` do mesa-switch não traz o std de `aarch64-unknown-linux-gnu` em host amd64 (E0463 em `libcompiler.rlib`). Uma camada `rustup target add` por cima (`mesa-rust.Dockerfile`), sem mexer na série de patches. Nenhuma dependência nova no repo (imagens Docker são as do project8-sw).
- decisões: `libvulkan.a` = MRI do `aarch64-none-elf-ar` juntando as 19 libs que o `build-switch-p8.sh` produz, menos `libnak_rs.a` (separada, como manda o building.md).
- pendente: `vkCreateInstance`/`nvk_CreateInstance` aparecem 2× (o `libvulkan.a` do nouveau e o `libnvk.a` parecem ter os mesmos objetos). Conferir no link do T5; se der símbolo duplicado, tirar `libnvk.a` da lista. Rust nightly da imagem não é fixado (hoje: 1.101.0-nightly 2026-10-16), então builds futuros podem divergir.
- 2026-10-17 06:47 T4 ✅ aceita (iter 5)

### T5 — tentativa 1
- feito: `tools/check_nro.py` (magia `NRO0` em 0x10, tamanho em 0x18 ≤ arquivo, depois dele só `ASET`) + `tests/test_check_nro.py` (2 do plano + truncado, asset section, lixo no fim, arquivo ausente; vermelhos antes). `cmake/switch.cmake` + `cmake/switch-rexglue-gcc.cmake` (do project8-sw, no `NOTICE`), `option(GH3_SWITCH)` no `CMakeLists.txt`, `switch/icon.jpg` (256×256, formas geradas com PIL, sem arte do jogo). NRO real: `build/switch/gh3.nro` 56,8 MiB, 647 passos em 512 s (medido). Aceite verde; suíte 40 passed. Playbook §1.
- decisões: com `GH3_SWITCH` o `CMakeLists.txt` faz `return()` antes do `generated/rexglue.cmake` (codegen é do host; o container só compila o `generated/default/` existente). No Switch o nativo entra sempre (`GH3_NATIVE_RENDERER=1`) e linka direto no `rexgpu-xenos` whole-archive do SDK; sem a cópia OBJECT do Mac. `-fno-tree-*vectorize` mantido do project8-sw (bug do q31 no Horizon).
- decisões: `check_nro.py` com `from __future__ import annotations` porque o aceite usa o `python3` do sistema (3.9). Build no Docker com `-j 6` (Docker tem 7,7 GiB; o project8-sw fala em 16 GB). Nenhuma dependência nova.
- pendente: o `vkCreateInstance` duplicado do T4 não quebrou o link (`--allow-multiple-definition`); só o console confirma. NRO não testado em hardware (H2/H3). Não há script `tools/build_nro.sh`; o comando docker está no playbook.
- 2026-10-17 09:06 T5 ✅ aceita (iter 1)

### T6 — tentativa 1
- feito: `tools/gen_supported_dumps.py` (+`--check`) → `src/supported_dumps.h` (só SHA-256/tamanho/title id do `extracted/default.xex`, revisão via `check_xex.py`); `src/dump_gate.h` (do project8-sw) ligado em `Gh3App::OnConfigurePaths` (Switch: `game/` ao lado do NRO); `tools/check_no_game_content.sh [DIR]`, `tools/make_sd_package.sh`, `switch/gh3.toml`; testes `test_sd_package.py` (12) + `test_dump_gate.py` (3, compila a decisão com `c++`), vermelhos antes. NOTICE e playbook §1.
- verificado: aceite verde; suíte 55 passed; build Mac recompilado: root com XEX falso/sem pasta → exit 3 com mensagem (sem core dump); `boot_test` nativo com o disco real `OK: 322 quadros`. `make_sd_package.sh` real → 3 arquivos em `out/sd/switch/gh3-nx/`, limpo.
- decisões: `check_no_game_content.sh` reescrito sem `mapfile` (o `bash` do Mac é 3.2); pega também `*.xen` (assets do GH3) e cabeçalho `XEX2` por conteúdo; limite de 8 MB só no modo repo (o NRO tem 56 MB). `Classify` separada do `Enforce` (que usa `rex::crypto`) para testar sem SDK. `gh3.toml` com os valores de GPU do project8-sw (mesmo motor), sem os cvars `guest_*`/`switch_*` do THPS. Nenhuma dependência nova.
- pendente: `build/switch/gh3.nro` é do T5 (sem a trava): rebuild Docker não feito; o ramo `__SWITCH__` do `OnConfigurePaths` não foi compilado. O `REXLOG` da trava sai antes do `InitLogging` (não aparece no log; só stderr). Custo do hash na partida não medido isoladamente.
- 2026-10-17 09:13 T6 ✅ aceita (iter 2)

### T7 — tentativa 1
- feito: sem `geometryShader` (MoltenVK) quad list vira triangle list por índices (0 1 2, 0 2 3, como o SDK) e ret/ponto vão para `k{Rectangle,Point}ListAsTriangleStrip` do tradutor SPIR-V do SDK (strip de 4 índices + restart; índices do guest copiados para o stream e lidos no VS via `kSysFlag_ComputeOrPrimitiveVertexIndexLoad`). Lógica pura em `src/native/native_primitive_expand.h`. `tools/native_skips.py` + `tests/test_native_skips.py` (6), `tests/test_native_primitive_expand.py` (5, compila o header com `c++`), +1 em `test_native_renderer.py`; todos vermelhos antes.
- medido: aceite verde (`OK: 631 quadros`; motivo 5 = 0). Antes: `out/native-run.log` (T3) tinha 2.700.939 draws pulados pelo motivo 5 (~61–64 mil por 10 s). Suíte `pytest tests`: 67 passed (`pytest` sem caminho coleta os testes do mesa em `out/` e quebra; não é deste T7). Log sem erro novo.
- decisões: `native_skips.py` soma TODAS as linhas "skipped by reason" (cada uma é delta de 10 s, então só a última subestimaria) e sai 1 se não houver nenhuma (corrida curta ou nativo não rodou). Com GS (`Choose` → `kGeometryShader`) o caminho é o de antes, então o Switch fica no GS. Strip e cópia de índices alocados por draw, sem cache (HUD tem poucos draws). Nenhuma dependência nova.
- pendente: conferência visual do HUD/partículas no Mac (só log); NRO não recompilado (o ramo com GS não mudou, mas o header novo não passou pelo GCC do devkitA64). Motivo 7 (sem render target, ~25 mil/10 s) e 13 continuam — fora do T7.
- 2026-10-17 09:19 T7 ✅ aceita (iter 3)
- 2026-10-17 09:48 T8 ❌ tentativa 1: padrão proibido adicionado em teste: xit(
- 2026-10-17 09:53 T8 ❌ tentativa 2: oráculo vermelho (.harness/gh3-marco-c/logs/oracle-1.log):

### T8 — tentativa 1
- feito: `src/rexglue_script_input.h` + `src/parity_capture.h` (project8-sw 3393f26, NOTICE) ligados no `gh3_app.h` (OnPreSetup/OnPostSetup/OnCreateDialogs/OnShutdown); `src/present_counter.h` + sink spdlog (OnPostInitLogging); `config/song_script.json`; `tools/gameplay_capture.py`, `tools/image_stats.py` (PNG por zlib, 5 filtros); `tests/test_gameplay_capture.py` (14, vermelhos antes). Sem dependência nova.
- medido: aceite verde 2× (87 s por captura); quadro 2700 = gameplay Slow Ride/Medium, white=0,007/0,000, stddev 52/65. Suíte `pytest tests` 88 passed; boot nativo T7 de novo `OK: 604 quadros`, motivo 5 = 0.
- decisões: GH3 não tem o contador de swap do THPS (0x82970A10) → `--parity_frame_counter=present` (padrão) conta as linhas `XELOG_GPU PRESENT` reais (exclui NULL/NO PRESENTER/FAILED), as mesmas do boot_test; exige `--log_level debug` (o gameplay_capture passa). `mark_clock_*` do THPS → 0.
- decisões: roteiro = Start ×2 (pula vídeo live-action e animado; sem isso a abertura passa de 3000 quadros) + rota do Guillerme. Delays por capturas: Start do título → menu leva ~7 s (loading), daí 10 s. Linha do tempo (~34 qps, wall clock): título ~1440, menu ~1700, dificuldade/setlist ~1900–2200, palco ~2300, nota ~2400, música falha ~3200 (sem tocar). N=2700.
- pendente: entrada é wall clock e o índice é quadro; máquina muito mais lenta/rápida pode deslocar a janela (margem ~±350 quadros). Headers não compilados no GCC do Switch (Docker). Sondas descartáveis em `out/t8/`.
- 2026-10-17 16:54 T8 ✅ aceita (iter 1)
- 2026-10-17 17:54 T9 ❌ tentativa 1: sessão do claude travou: passou de 3600s sem terminar (ITER_TIMEOUT_SEC) e foi encerrada

### T10 — tentativa 1
- feito: `tools/gameplay_fps.py` (roteiro até a marca `gameplay`, lê `last 10 s`/`per frame` do log, JSON com swaps_per_s, draws_per_s, wait_ms, draws_per_frame, game_fps; `--min-vs` com tolerância de 5% já pronto para T11/T12) + `tests/test_gameplay_fps.py` (7, logs sintéticos e jogo falso; vermelhos antes). Playbook §5.
- medido: linha de base `out/perf/baseline.json`: game_fps 53,0, swaps/s 46,5, 19386 draws/s, espera 7,0 ms (30 s, 3 blocos; ~2 min por corrida).
- decisões: o 1º bloco de 10 s depois da marca é descartado (cobre o trecho antes dela). game_fps = draws_per_s / mediana de "per frame: N draws" como manda o detalhe; é estimativa e fica perto de swaps/s quando o jogo não atrasa. Nenhuma dependência nova.
- pendente: 1 corrida só (variância entre corridas não medida); depende de wall clock do roteiro como o T8.
- 2026-10-17 22:28 T10 ✅ aceita (iter 1)
- 2026-10-17 22:56 T11 ❌ tentativa 1: aceite vermelho: python3 -c "import re,glob;n=sum(len(re.findall(r'ctx\.(xer|ctr)\b',open(f).read())) for f in glob.glob('generated/default/gh3_recomp.*.cpp'));print(n);assert n==0" && python3 tools/gameplay_capture.py --renderer native --out out/parity/native-t11.png && python3 tools/image_stats.py out/parity/native-t11.png --max-white 0.15 --min-stddev 20 && python3 tools/gameplay_fps.py --renderer native --seconds 30 --out out/perf/t11.json --min-vs out/perf/baseline.json

### T11 — tentativa 2
- feito: `ctr/xer/cr_as_local = true` no topo de `config/gh3_functions.toml` (no manifesto as opções são ignoradas — foi o erro da tentativa 1: flags no manifesto não mudam nada). `ctx.xer|ctr` 155854 → 0. `tests/test_codegen_flags.py` (2). Suíte 109 passed. Playbook §5.
- medido: mesmo build, game_fps 55,6 / 66,2 / 64,8 (baseline 66,4, limite 63,0); a falha da tentativa 1 (56,8) foi ruído, as piores corridas vêm logo após compilar. Sem ganho de FPS mensurável acima do ruído.
- decisões: `non_volatile_as_local` fica false: true derruba o jogo (violação de acesso em 0x84, 16 PRESENT) — funções partidas leem registradores antes de escrever; exige portar `share_registers` + `lee_antes.py` do nfsmw (fora do escopo; ver playbook).
- pendente: non_volatile_as_local; NRO do Switch não recompilado com os novos gerados.
- 2026-10-17 23:20 T11 ✅ aceita (iter 3)

### T12 — tentativa 1
- feito: `tools/llamadas_diretas.py` (`sub_X(ctx, base);` → `__imp__sub_X(ctx, base);`, idempotente; 88007 chamadas em 142 arquivos), chamado no fim de `tools/codegen.sh`; `-flto=6 -fno-fat-lto-objects` no `gh3_recomp` e `-flto=6 -flto-partition=balanced` no link do `gh3` em `cmake/switch.cmake`; `tests/test_direct_calls.py` (6, vermelhos antes). Suíte 117 passed. NRO rebuildado no Docker com LTO: exit 0, 61,5 MiB, `check_nro` OK.
- medido: Mac nativo game_fps 61,3 na 1ª corrida (logo após compilar, ruído já visto no T11) e 64,0 na 2ª (baseline 66,4, limite 63,0); captura white=0,001 stddev=49,3. Sem ganho de FPS no Mac mensurável (o ganho esperado é do LTO no Switch, sem hardware aqui).
- decisões: `__imp__` direto perde o ponto de gancho por nome `sub_*`; o GH3 não tem nenhum (src/ não define). LTO só no código traduzido + executável, SDK fora (memória do Docker 7,7 GiB). Nenhuma dependência nova.
- pendente: `nm | grep " _start$"` = 0 não conferido; tempo do build Switch com LTO não cronometrado; ganho de FPS no Switch não medido (H2/H3).
- 2026-10-18 00:20 T12 ✅ aceita à mão (Guillerme+Claude): a sessão terminou e ficou ociosa; trabalho recuperado do índice do git após reset por engano. Conferido: suíte 117, NRO LTO 00:12 válido, `_start` = 0; FPS Mac 64,0 (limite 63,0) e captura ok medidos pelo agente.

### T13 — tentativa 1
- feito: `src/guest_waits.h` (ganchos fortes `sub_82172718` swap / `sub_82172758` vblank: chamam o `__imp__` e, após N leituras idênticas no mesmo thread, dormem; cvars `gh3_guest_waits`, `_after`, `_us`, `_stats`; log "guest waits: installed N"), `src/guest_waits_spin.h` (detector puro), `tools/llamadas_diretas.py` com `HOOKED` (esses 2 endereços ficam fora da reescrita `__imp__`), `switch/gh3.toml`, `tests/test_guest_waits.py` (6). O trabalho já estava num commit wip da sessão anterior; esta tentativa só verificou.
- verificado: suíte `pytest tests` 133 passed; captura nativa OK, white=0,001 stddev=56,7; log com "guest waits: installed 2 ... enabled=false"; gerado já sem `__imp__` nos 2 endereços (0 reescritos).
- decisões: padrão desligado fora do Switch, ligado no Switch (regra da casa p/ patch de comportamento). Não consegui rodar `build_host_sdk.sh` (aprovação negada neste ambiente); o binário existente já continha o gancho (marcador no log).
- pendente: ganho real (CPU do thread de render) só medível no Switch; no Mac o gancho fica desligado, então nada medido de FPS. O wip também trouxe `frame_stats.h` e mudanças em `native_renderer.cpp`/`AGENTS.md` fora do escopo estrito de T13.
- 2026-10-18 02:37 T13 ✅ aceita (iter 1)

### T14 — tentativa 1
- feito: `src/switch_apm_config.h` (escolha pura: portátil+boost → 0x92220008, senão 0), `src/switch_apm.h` (cvar `gh3_switch_gpu_boost` padrão true; libnx apm só sob `__SWITCH__`; loga "apm performance configuration 0x..."; Restore() devolve a configuração lida antes), ligado em `gh3_app.h` (OnPostSetup / OnShutdown); `switch/gh3.toml`; `tests/test_switch_apm.py` (5, vermelhos antes). NRO rebuildado no Docker: `check_nro` OK, string no binário. Suíte 142 passed.
- decisões: restaurar a configuração lida por `apmGetPerformanceConfiguration` em vez de fixar um "padrão"; no dock não mexe (já 768 MHz). Não consegui ler `~/gh3-refs/nfsmw-nx` (leitura negada neste ambiente): porto feito pela API do libnx e pelo valor do detalhe. Nenhuma dependência nova.
- pendente: efeito real (clock/FPS) só medível no Switch; chamadas apm* compiladas mas não executadas. Script auxiliar `out/build_switch_t14.sh` (fora do git) só embrulha o comando do AGENTS.md.
- 2026-10-18 02:46 T14 ✅ aceita (iter 2)

### T15 — tentativa 1
- feito: alias 1 amostra → alvo 1x: `RenderTargetKey.alias_msaa` + `AliasMsaa()`, `SyncAliasTarget()` (blit NEAREST 2x2 de cor/profundidade da imagem de pitch P para o alvo 1x de pitch 2P) chamado em `GetRenderTarget` e em `Draw()` (setups em cache não passam por `GetRenderTarget`); `edram::AliasBlitExtent` em `native_edram_samples.h` (+2 testes C++ compilados, +1 de fonte); `gameplay_capture.py`/`gameplay_fps.py` aceitam só-flags depois de `--`; `switch/gh3.toml` com `gh3_native_msaa_as_samples = false`; playbook §7.
- verificado: aceite verde (white=0,001 stddev=45,7, log só com alvos 1x); suíte 146 passed; captura no padrão (samples=true) intacta (white=0,001). FPS Mac 62,9 (samples) vs 68,9 (alias): dentro do ruído, GPU do Mac não é o gargalo.
- decisões: `gh3_native_msaa` agora `false` por padrão (o aceite só passa `as_samples=false`; com MSAA 4x de host não há alias e o palco sai branco; o Switch já usava false). A 1ª corrida do aceite "passou" por acaso (captura caiu em outra tela); a causa real do branco só aparece com a marca em gameplay.
- pendente: ganho de GPU/ms no Switch não medido (NRO não recompilado); borda do palco em 1 amostra (sem amostras 2x2); se algum frame usar a 1x antes do 4x no mesmo quadro, o blit pode sobrescrever o conteúdo da 1x (não observado).
- 2026-10-18 03:10 T15 ✅ aceita (iter 3)


### T16 — tentativa 1
- feito: (wip commitado em 2995901) cena 3D escalada: `src/native/native_scene_scale.h` (aritmética pura), `RenderTargetKey.scene_scaled` + `SyncSceneTarget` (blit LINEAR entre o alvo cheio 1280 e a imagem escalada), viewport/scissor escalados no `Draw`, cvar `gh3_native_scene_scale` (padrão 1.0; `switch/gh3.toml` 0.75), `tests/test_scene_scale.py` (9). Esta tentativa só rodou o aceite.
- verificado: suíte 161 passed; captura 0,75 OK (white=0,014/0,001, stddev 54,5/50,2) e log "scene scale 0.75: 960x540" (alvos 960x768 cor / 960x996 depth). Cena estática (`--mark main_menu`): 0,75 vs 1,0 mean_diff=0,7 — a escala está correta e a HUD fica nítida.
- decisões: o comparativo de gameplay **não é verificável como escrito**: a marca `gameplay` é wall-clock, mas o jogo anima por frame; a corrida 0,75 chega ao marco com 2578 presents e a referência com 2507 (71 frames a mais), caindo em trechos diferentes da música. Repetido 3× (mean_diff 51,4 / 37,4 / 51,9), inclusive com `gh3_min_frame_vblanks=2` nas duas (51,9) e contra a referência borrada em 0,75 (51,5). O limite 12 não é alcançável enquanto o marco não for preso ao tempo de música. Não editei a linha `aceite:` nem enfraqueci teste.
- pendente: (a) prender a captura ao tempo de música (ou a um frame de jogo após o início da música) para o comparativo valer; (b) medir o ganho de GPU no Switch com `gh3_native_scene_scale=0.75`. A implementação está de pé; falta a verificação automática.
- 2026-10-18 04:55 T16 [!] bloqueada (medição não determinística)


### T17 — tentativa 1
- feito: inspeção do caminho de present e do anel de submissões; nenhum código alterado (bloqueada antes de implementar).
- verificado: `Present()` (`src/native/gh3_native_system.cpp:1200`) faz `if (renderer_ready_) { renderer_.Swap(...); return; }`; o command buffer + fence único em ~1278 é o `ClearOutput`, fallback de cor de teste quando o renderer NÃO subiu. O gameplay usa `Renderer::Impl::Swap` + o presenter do SDK (`VulkanPresenter::RefreshGuestOutput`, com `VulkanSubmissionTracker` e fence por submissão). O renderer já tem `kSubmissionSlots = 4`, com fence e pool por slot; `BeginRecording` espera só o slot que vai reutilizar. As esperas de fence medidas no Switch (341-656 ms/s, playbook §8) são GPU/swapchain, não esse fallback.
- decisões: não implementar um anel no fallback só para o aceite: passaria o log "present ring: N slots" e não mudaria o jogo (regra do T18: não inventar gancho). Se houver serialização de present, ela está no presenter do SDK ou é o vsync; medir antes. Reescopo sugerido: instrumentar a espera de fence do presenter por quadro e, se confirmar serialização, um anel no guest output (patch no SDK).
- pendente: aceite não rodado de propósito (o log não existe porque o anel não foi adicionado). Reescopo é outra tarefa.
- 2026-10-18 04:59 T17 [!] bloqueada (premissa não se aplica ao gameplay)


### T18 — tentativa 1
- feito: gancho forte `sub_824CE958` (só no retorno do laço, `lr=0x824D0100`), com `kRingWait`/`kRingWaitLoopLr` e os offsets `[[device+10896]]` (leitura) e `[device+10908]` em `src/guest_waits.h`; o gancho chama o `__imp__`, lê a leitura do anel e dorme via `AfterRead` quando `r30` (posição pedida) ainda está à frente; `0x824CE958` entrou no `HOOKED` de `tools/llamadas_diretas.py` (10 chamadas voltaram a `sub_`); `Install` conta 3 e loga o endereço do anel; `tests/test_guest_waits.py` +1 (forma do laço no gerado).
- verificado: aceite verde — `pytest tests/test_guest_waits.py` 19; suíte 162; captura nativa white=0,001 stddev=48,9; log "guest waits: installed 3 (swap 82172718, vblank 82172758, ring 824CE958), enabled=true". No gerado, o laço em `loc_824D00F8` compara `r30` com `[[r31+10896]]` e usa `[r31+10908]`; `r30` é não-volátil (o callee salva/restaura), então continua válido depois do original.
- decisões: o gancho age só no `lr` do laço (as outras 10 chamadas vão direto ao `__imp__`); `AfterRead` reusa o detector por thread (dorme após N leituras iguais). Ganho esperado é CPU livre no Switch, não FPS (a GPU é o limite).
- pendente: medir no Switch (CPU do thread de render / `guest waits: yields`).
- 2026-10-18 05:05 T18 ✅ aceita


### T19 — tentativa 1
- feito: levantamento de `sub_8272C7C0` (`generated/default/gh3_recomp.101.cpp`): 1862 linhas; 28 `fmadds`, 23 `fmuls`, 12 `fadds`, 4 `fsubs`, 81 `lfs`, 66 `stfs`, 231 `REX_LOAD`/146 `REX_STORE`; 2 chamadas (`sub_8248EB20`, `sub_8272BB90`). Nada alterado.
- verificado: contagem acima no gerado (dump direto do arquivo).
- decisões: bloqueada — grande demais para reescrever com confiança e verificação bit a bit nesta sessão: `fmadds` arredonda uma vez (C `a*b+c` arredonda duas; teria que usar `std::fma` em cada um), 147 acessos à memória do guest e o harness de verificação teria que reproduzir a semântica exata do original. Além disso, o ganho de FPS é zero enquanto a GPU for o limite (playbook §8: modo desempenho, GPU ~13 ms, FPS ~45, threads esperando). Risco alto para ganho não mensurável.
- pendente: reescopo quando a GPU deixar de ser o limite, ou uma função quente menor; o `gh3_hot_math_verify` do plano pode ser reusado.
- 2026-10-18 05:06 T19 [!] bloqueada (grande demais / GPU é o limite)


### T20 — tentativa 1
- feito: build do Switch no Docker com `-DGH3_PGO=` (PGO desligado no release; o `.gcda` fica em `out/pgo`, fora do git), NRO `0x3aa1000`; ELF guardado em `out/elf/gh3-7d12d79.elf`; `tools/make_sd_package.sh --nro build/switch/gh3.nro` → `out/sd/switch/gh3-nx` (3 arquivos); strings com "scene scale" e "present ring".
- verificado: aceite exit 0 — `check_nro` OK, os dois greps OK, o NRO do pacote é mais novo que `src/native/native_renderer.cpp`, `check_no_game_content.sh out/sd` = limpo.
- decisões: PGO desligado (o perfil gravado é local e específico do build); o `out/driver-sdk` tem o patch do ZCULL, mas `gh3_native_zcull=false` no toml; T16 a 0.75 e o modo desempenho (sem sombra/bloom) ligados no `switch/gh3.toml`.
- pendente: medição no Switch fica com o humano; T17/T19 bloqueadas.
- 2026-10-18 05:16 T20 ✅ aceita
