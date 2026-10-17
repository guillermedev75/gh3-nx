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
