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
