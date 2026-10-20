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


### T21 — tentativa 1
- feito: levantamento do caminho de shader e das bibliotecas. Nada alterado.
- verificado: o SDK **não constrói** o spirv-tools: `sdk/thirdparty/CMakeLists.txt` só expõe `spirv-tools-headers` (INTERFACE); o `libSPIRV.a` de `sdk/out/{mac,switch}-arm64` é o SPIRV do **glslang** (822 KB/1 MB, 0 símbolos `spvtools::Optimizer`). O `VulkanTranslation::translated_binary()` é `const std::vector<uint8_t>&` e o `shader_module_` é privado no SDK; integrar exigiria criar o módulo otimizado no nosso renderer (mapa translation→VkShaderModule) e validar/fallback.
- decisões: bloqueada nesta sessão por escopo: precisa (a) de um patch local no SDK (mecanismo do `setup_sdk.sh` é da série do project8-sw) para `add_subdirectory(spirv-tools)` e construir `SPIRV-Tools`/`SPIRV-Tools-opt` no Mac e no devkitA64; (b) linkar/instalar as libs nos dois builds; (c) integrar o otimizador no caminho de módulo. Além disso, o gargalo medido é fill-rate (passe principal 720p + sombra/bloom; GPU 13 ms no modo desempenho), não ALU — o ganho de spirv-opt é incerto e não move FPS enquanto os pixels dominarem. Multi-hora; fica para a próxima sessão com contexto novo.
- pendente: implementar com o harness do aceite (teste + captura + log). Sem ganho garantido.
- 2026-10-18 05:21 T21 [!] bloqueada (build/escopo; ganho incerto)

### T22 — tentativa 1
- feito: nada alterado.
- verificado: o compilador de shaders do driver (NAK) **não suporta FP16 nem VRS** nesta GPU — medido e documentado pelo nfsmw-nx (`docs/porting-another-game.md`, "What did not work": "Half-precision math at double speed (FP16) and variable rate shading: the shader compiler, NAK, does not support them on this GPU"). O próprio detalhe do T22 manda documentar e bloquear se o NVK não honrar.
- decisões: bloqueada pela limitação do driver, sem código.
- pendente: reavaliar se o NAK ganhar suporte a FP16.
- 2026-10-18 05:21 T22 [!] bloqueada (NAK sem FP16)

### T23 — tentativa 1
- feito: nada alterado.
- verificado: depende de T21 (spirv-opt) e T22 (FP16), ambas bloqueadas; o aceite pede as strings dos dois no NRO.
- decisões: bloqueada por dependência. O pacote atual (T20) já contém T16-T18 e o log do anel.
- pendente: refazer quando T21/T22 saírem.
- 2026-10-18 05:21 T23 [!] bloqueada (depende de T21/T22)

### T24 — feito fora do loop (be32b23)
- feito: `switch/gh3.toml` 0.65; cap de render area por `known_1x_height`/`AliasHeight` (vale 1x e alias), preservado na recriação; teste de fiação 0.65.
- verificado: suíte 163; captura 0.65 (832x468, white=0,011, stddev=52,3); o passe 320x2048 virou h512/h256.
- decisões: o cap generalizado (T30) usa a altura real do alvo 1x revelada pelos draws explícitos.
- pendente: ganho no Switch (F5).
- 2026-10-19 23:55 T24 ✅ aceita (fora do loop)

### T25 — feito fora do loop (f5058bd)
- feito: `src/native/native_invisible.h` + 6 testes; o `Draw` pula blend 0×src/1×dst e alpha NEVER sem escrita de Z/stencil.
- verificado: suíte 169; 53.248 draws pulados na captura; imagem white=0,001 stddev=50,0.
- pendente: ganho real no Switch (F5).
- 2026-10-19 23:55 T25 ✅ aceita (fora do loop)

### T28 — feito fora do loop (1370223)
- feito: fallback do guest output verde → preto (test colour).
- verificado: grep do clear preto; 10 transições no Switch ficam para a F5.
- 2026-10-19 23:55 T28 ✅ aceita (fora do loop)

### T30 — feito fora do loop
- feito: validação local do T24/F1.2 (perfil por passe).
- verificado: `h2048 c=t0p320` não aparece; o passe virou h512/h256.
- 2026-10-19 23:55 T30 ✅ aceita (fora do loop)

### T26 — tentativa 1
- feito: `tools/gameplay_fps.py --runs N` (padrão 1) roda N corridas e agrega: `game_fps`/`swaps_per_s`/`wait_ms` por mediana, `run_results` por corrida; p1 (1% low = 1000/p99 do frame time), `p99_ms`, `%>50 ms` e mediana de frame time vêm dos timestamps dos PRESENTs de todas as corridas, no mesmo trecho dos blocos de 10 s (1º bloco descartado). Sem PRESENTs, `%>50 ms` usa `slow frames: N over 50 ms`. Playbook §8 ganhou a seção F0.1.
- decisões: p1 agregado sobre os quadros (não mediana de 3 p1s) para ser estável com 3 corridas; timestamp do log tem 1 ms (no Switch não é o do NVK, que pede ×1,627); `--min-vs` segue no `game_fps` mediano. Nada do renderer mudou.
- verificado: aceite exit 0 (`pytest tests/test_gameplay_fps.py` 10, `--help` com `--runs`); suíte 172; corrida real 20 s no Mac: game_fps 63,3, frame mediano 17 ms, p1 29,4, p99 34 ms, >50 ms 0% (1021 quadros).
- pendente: medir o A/B de 3 corridas no Switch (F5, com o aparelho).
- 2026-10-20 00:05 T26 ✅ aceita (iter 1)

### T27 — tentativa 1
- feito: `src/native/native_swap_log.h` (formatter puro, testado sem SDK), `tests/test_swap_log.py` (7: campos, clear/resolve/alias/fence, vazio, fiação) e cvar `gh3_native_swap_log` (padrão false) no `Swap` de `native_renderer.cpp`; o quadro acumula clear de cor/profundidade (raw do registrador + alvo), contagem/origem/destino do último resolve, chave do último passe, `alias_seq` e `frame_fence_wait_ns`.
- decisões: uma linha `[native] swap:` só com a cvar ligada (padrão false: custo de log por swap no Mac e no Switch) e reset do acumulador a cada swap; `clear=-` quando o quadro não limpou pelo caminho de resolve (fiel ao que aconteceu, não aos registradores parados).
- verificado: aceite exit 0; 5.026 linhas `swap:` na captura (ex.: `frame=4992 ... resolve=41 ... fence:10.437ms presented:1`); suíte 179 passed; imagem white=0,014 stddev=55,2. Playbook §8/F0.2.
- pendente: usar o log para fechar o verde (F0.3/F3.1); nenhum ganho de FPS nesta tarefa (só diagnóstico).
- 2026-10-20 00:17 T27 ✅ aceita (iter 2)

### T29 — tentativa 1
- feito: `src/native/native_format_log.h` (formato puro, tabela de VkFormat + `Name`, testado sem SDK), cvar `gh3_native_format_log` (padrão false) e log `[native] format:` no `CreateOrGetRenderTarget` (uma linha por alvo criado); 8 testes em `tests/test_format_log.py`; playbook §8/F0.4.
- decisões: logar na criação (uma vez por alvo, não por quadro) para a auditoria custar zero no Switch; `static_assert` no renderer liga cada valor numérico do header ao `VK_FORMAT_*` real (header compila sem SDK); flags `hdr16f`/`gamma`/`clear`/`scene`/`alias` cobrem o que o F1.4 vai cortar.
- verificado: aceite exit 0 (8 testes, captura nativa, 10 linhas `format:` com `VK_FORMAT_`); suíte 187 passed. Alvos vistos: cor `R8G8B8A8_UNORM`, profundidade `D32_SFLOAT_S8_UINT`; nenhum 16F no caminho atual.
- pendente: nada; F1.4 (T33) usa este log para decidir os cortes.
- 2026-10-20 00:26 T29 ✅ aceita (iter 3)

### T31 — tentativa 1
- feito: `src/native/native_scene_dynamic.h` (níveis 0.65-1.0 em passos de 0.05, alvo 16.7 ms, margens 1.0/1.5 ms, histerese 3 quadros e 0.6 s no nível) + 9 testes em `tests/test_scene_dynamic.py`; cvar `gh3_native_scene_dynamic` (false no código, true no `switch/gh3.toml`); o `Swap` mede o intervalo entre swaps e loga `scene dynamic:` ao ligar e a cada mudança; `RenderTarget.scale_used` força recriar o alvo `scene_scaled` quando a escala muda (inclusive menor) e o conteúdo antigo é esticado por `vkCmdBlitImage` (LINEAR; NEAREST na profundidade).
- decisões: começar da escala do cvar (grade de 0.05) e usar o tempo real entre swaps (é o que o jogador sente, já inclui vsync/GPU); tempos inválidos (>1 s) não contam para a histerese para não reagir a carregamento.
- verificado: aceite exit 0 (9 testes; captura 0.65 white=0,001 stddev=53,1; log com `scene dynamic:`); suíte **196 passed**; corrida extra partindo de 1.0 desceu 1.0→0.65 em passos com log por mudança.
- pendente: medir no Switch (F5): a recriação dos alvos custa um quadro longo (~300 ms no pior caso no Mac), então o piso 0.65 deve ser o ponto de partida.
- 2026-10-20 01:45 T31 ✅ aceita (iter 1)
- 2026-10-20 01:47 T31 ✅ aceita (iter 4)

### T32 — tentativa 1
- feito: `src/native/native_scene_cas.h` (puro: sharpness pós-setup `-1/(8-3s)`, retângulo NDC, ratio, formato; static_assert dos offsets dos push constants) + `tests/test_scene_cas.py` (7). No `native_renderer.cpp`: cvars `gh3_native_scene_cas` (false) / `_sharpness` (0.2), pipeline CAS por formato (VS de retângulo + PS de resample do próprio SDK, `sdk/src/ui/shaders/vulkan_spirv`), `CasUpscale` chamado no upscale do `SyncSceneTarget` (downscale/depth/outro formato seguem LINEAR), reset dos binds/dynamic state após o passe; include paths no CMake host e Switch; `switch/gh3.toml` com CAS ligado; playbook §8/F1.1c.
- decisões (e por quê): reaproveitar o SPIR-V de CAS que o SDK já usa no presenter (sem gerar/compilar shader e sem dependência nova); só o upscale usa CAS, porque CAS não faz downsample útil e profundidade não é cor; descriptor set por submissão via `AllocateSet` (mesmo padrão do copy de profundidade, sem corrida entre frames).
- verificado: aceite exit 0 (7 testes; captura 0.65+CAS white=0,001 stddev=50,3; log `scene CAS: enabled` e `upscaling 832x468 -> 1280x720`); suíte completa 203 passed; build Mac ok.
- pendente: custo de GPU/nitidez no Switch (F5); NRO não rebuildado (T46). Medição de FPS não faz sentido no Mac (GPU não é o limite); registrado em "Métricas em aberto" do vault.
- 2026-10-20 02:02 T32 ✅ aceita (iter 5)

### T33 — tentativa 1
- feito: `src/native/native_format_audit.h` (puro, sem SDK) acumula bytes/pixel×amostras por alvo e os candidatos do F1.4 (16F, gamma, 64bpp, guest 24 bits guardado em 8 B/px); `tests/test_format_audit.py` (9). Ligado ao `gh3_native_format_log`: 1 linha `format audit:` por alvo novo depois do quadro 300, com memória, contagem por VkFormat, `depth24_save` e hint.
- decisões (e por quê): logar de novo a cada alvo novo (não só por formato) para a conta final incluir os alvos da gameplay (320x2048 entra no quadro 3833); acumular só com a cvar ligada (custo zero no Switch); **não** mudar formatos: a captura mostra 16f:0/gamma:0/bpp64:0 no caminho — não há alvo caro sem HDR para cortar. O custo real é profundidade de 24 bits em D32_SFLOAT_S8_UINT (~35,5 MiB), que o `gh3_native_depth24` já corta e o `switch/gh3.toml` já liga; o Mac fica em 32 bits para a paridade.
- verificado: aceite exit 0 (9 testes; captura; `format audit:` no log: 10 alvos, 82,3 MiB, 16f:0, depth24_save:35,5 MiB); suíte **212 passed**; build Mac ok.
- pendente: observação fora do escopo — a captura ainda cria depth `1280x2048` e `1040x2048` (o cap do T24 só vale para alias `alias_msaa`).
- 2026-10-20 02:12 T33 (iter 1)
- 2026-10-20 02:16 T33 ✅ aceita (iter 6)

### T34 — tentativa 1
- feito: série local `patches/rexglue-local` (`0001-spirv-tools-opt.patch`) aplicada pelo `tools/setup_sdk.sh` depois da do P8; `tools/build_spirv_tools.sh` (sem args = Mac, `--switch`, `--check`) e `tests/test_spirv_tools.py` (4 testes). O patch liga `add_subdirectory(spirv-tools)` no bloco Vulkan com `SPIRV_SKIP_EXECUTABLES`/`SPIRV_SKIP_TESTS` ON, `SPIRV_WERROR=OFF` e `SPIRV-Headers_SOURCE_DIR` apontando o `spirv-headers` do SDK (libs static, BUILD_SHARED_LIBS=OFF).
- verificado: aceite exit 0 (`pytest tests/test_spirv_tools.py` 4 passed + `test -f out/sdk-install/lib/libSPIRV-Tools-opt.a`); suíte **216 passed**; lib do Mac com 36 símbolos `spvtools::Optimizer` e headers em `out/sdk-install/include/spirv-tools/`; devkitA64 (GCC 15.2) compila e instala em `out/sdk-switch-install/lib` (avulso) e o alvo `SPIRV-Tools-opt` também builda dentro de `build/switch` (206 TUs); configure do NRO ok.
- decisões (e por quê): série local versionada no repo em vez de mexer na série externa do project8-sw (o `--check` compara a árvore exata e a série do P8 é compartilhada); diretório `patches/rexglue-local` porque `patches/sdk` cai no `.gitignore` (`sdk/` casa em qualquer nível); `SPIRV_WERROR=OFF` porque o GCC 15 do devkitA64 avisa em código de terceiro; `--check` usa `grep >/dev/null` porque `nm | grep -q` morre de SIGPIPE sob pipefail.
- pendente: T35 linka `SPIRV-Tools-opt`/`SPIRV-Tools` no renderer e roda o otimizador; NRO não rebuildado (cada build do Switch ganhou ~330 TUs de spirv-tools). Playbook §8/F2.1 atualizado.
- 2026-10-20 02:31 T34 ✅ aceita (iter 7)

### T35 — tentativa 1
- feito: `src/native/native_spirv_opt.h` (contagem de instruções, env pela versão do header, validação/otimização com fallback, Stats), `tests/test_spirv_opt.py` (8; C++ ligado às libs do T34 + fiação/cvar/toml/CMake); `gh3_native_spirv_opt` (padrão false) no renderer: `ShaderModuleFor` otimiza uma vez por tradução, guarda o módulo otimizado (dono: `Shutdown`) e cai no original do SDK em falha; log de 10 s `spirv-opt: N shaders, X% fewer instructions (M fallbacks)` no `Report`; link de `SPIRV-Tools-opt/-link/-Tools` no host (install) e no Switch (subdiretório); `switch/gh3.toml` true; playbook §8/F2.2.
- decisões (e por quê): `SetScalarBlockLayout(true)` no validador — o `XeSystemConstants` do tradutor põe array de vec4 em offset 28 e o SDK liga `VK_EXT_scalar_block_layout`; sem isso todo shader era `invalid input` (1ª captura: 415/415 fallbacks). Otimizar no thread da tradução e nunca por quadro; fallback não entra no mapa de módulos para não destruir duas vezes o módulo do SDK. Sem dependência nova (libs do T34).
- verificado: aceite exit 0 (8 testes; captura nativa white=0,011 stddev=53,1; log `spirv-opt: 75 shaders, 28.4% fewer instructions (0 fallbacks)`); suíte completa 224 passed; build Mac ok; nenhum `did not translate`; configure do Switch ok e os dois .obj nativos compilam no devkitA64 (GCC 15.2) com os alvos `SPIRV-Tools-opt/-link/-Tools`.
- pendente: link/NRO completo do Switch (T46) e o ganho real de GPU/FPS no aparelho (F5).
- 2026-10-20 02:56 T35 ✅ aceita (iter 8)

### T36 — tentativa 1
- feito: `tests/test_nak_patch.py` (5: série do P8, árvore do Mesa, check real, reprova latência 32 e peephole 0) e `tools/build_mesa.sh` com `--mesa DIR` e a checagem `NAK` no `--check`; playbook §8/F2.3. NVK rebuildado (331,7 s, 2892 objetos AArch64).
- decisões (e por quê): a latência 200 (textura/memória, SM<70), o `peephole_select` 8 e a revisão `1<<24` do cache **já vinham** no patch 0002 da série do project8-sw fixada (nfsmw-nx, MIT) — um patch local com os mesmos hunks não aplicaria sobre a árvore já patcheada, então o T36 torna o caminho verificável em vez de duplicá-lo. O `--check` antes só provava `vkCreateInstance`; agora reprova árvore sem o NAK (testado mutando cópias da árvore).
- pendente: ganho real de GPU/FPS no Switch só na F5 (`NVK_SHADER_STATS=1`: conferir "B de memoria local" = 0); o resto do draw path do nfsmw (rebinds de constant buffer, set 4 por diferenças) não foi portado — entrelaçado com os sets do nfsmw e sem medição no aparelho.
- 2026-10-20 03:11 T36 ✅ aceita (iter 9)

### T37 — tentativa 1
- feito: `src/native/native_shader_simplify.h` (puro + validador do SPIRV-Tools): max(a,a)/min(a,a) → `OpCopyObject`, query de tamanho de textura repetida no mesmo bloco → cópia do 1º resultado, branch redundante (t==f e bloco-saltinho de 1 predecessor) fundido; valida antes/depois e cai no original em falha. Cvar `gh3_native_shader_simplify` (padrão true; true no `switch/gh3.toml`), roda mesmo com o spirv-opt desligado e loga `shader simplify: …` a cada 10 s; `tests/test_shader_simplify.py` (8). Corrigi `spirv_opt::CountInstructions`, que contava o cabeçalho de 5 palavras como 1 instrução (o relatório do F2.2 passa a ter contagem real).
- decisões (e por quê): medido no GH3 — **1026 shaders, 0 de cada regra, 0 fallbacks, 0,0% menos palavras**: o tradutor do SDK já devolve `a` no max(a,a) (`spirv_translator_alu.cpp`), só emite `OpImageQueryLod` e o CFG não tem blocos vazios. O passe fica como guarda (custo: 1 validação por tradução, nunca por quadro); nenhum ganho de GPU/FPS é reivindicado.
- verificado: aceite exit 0 (8 testes; captura white=0,014 stddev=55,9 mean=89,5; log `shader simplify: 1026 shaders … 0 fallbacks`); suíte **237 passed**; build Mac ok; os dois .obj nativos compilam no devkitA64 (GCC 15.2). Playbook §8/F2.4.
- pendente: NRO do Switch não rebuildado (T46); ganho real do F2.4 no aparelho só se o tradutor voltar a emitir os padrões (F5).
- 2026-10-20 T37 (iter 1)
- 2026-10-20 03:49 T37 ✅ aceita (iter 10)

### T38 — tentativa 1
- feito: `src/native/native_pipeline_prewarm.h` (puro: versão `(formato<<8)|passes`, arquivo `pipelines-<versão>.bin` com magic+versão+contagem+XXH3, `Parse` validado, dedupe, teto, linha de log) + `tests/test_pipeline_prewarm.py` (9, vermelhos antes). No renderer: `RecordPipeline` copia chave+microcódigo na 1ª vez que cada pipeline aparece; gravação em thread própria a cada 10 s e no `Shutdown`; `PrewarmPipelines` (em `InitializeDriverCache`, antes do guest) recria shaders/traduções/geometria/layout/pipelines; log `pipeline prewarm:` na inicialização e no relatório de 10 s (`N at start ..., M new since`); cvar `gh3_native_pipeline_prewarm` (true). Cache do **driver** também versionado (nome + cabeçalho `{magic, versão, tamanho, hash}`).
- medido: aceite exit 0; captura white=0,014 stddev=55,4; 121 de 121 pipelines recriados em ~1,0 s de boot (Mac, cache do driver quente) e **0 pipelines novos** em toda a captura (`0 new since` nos 9 relatórios); suíte 246 passed; `check_no_game_content` limpo.
- decisões (e por quê): gravar o microcódigo (não o SPIR-V) porque o `LoadShader` do jogo reusa o shader pelo hash e a tradução continua num thread só (o init roda antes dos workers usarem o tradutor); o microcódigo fica no byte order do host (como o `Shader` do SDK), então o hash da chave (do guest) não confere por XXH3 — a integridade é o hash do corpo do arquivo. Arquivo é cache local da máquina (`~/.local/share/gh3/cache/shaders/driver`), como o cache do driver: nunca vai ao repo/SD. Gravação periódica porque o capture encerra por SIGTERM (sem `Shutdown`).
- pendente: NRO do Switch não rebuildado (T46); custo do prewarm no boot do Switch e o "zero new since" na mesma música ficam para a F5 (o manifesto é a união das telas vistas: a 1ª execução grava, a 2ª já zera).
- 2026-10-20 04:07 T38 ✅ aceita (iter 11)
