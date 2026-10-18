# AGENTS.md — gh3-nx

Port não oficial de Guitar Hero III (Xbox 360) para Nintendo Switch por **recompilação estática** (ReXGlue:
PowerPC → C++ → ARM64) com renderer Vulkan nativo (project8-sw / NVK). Código, comentários, docs e commits
em **português**.


> **Continuando a fila de otimizações?** Leia `.harness/gh3-marco-c/HANDOFF.md` primeiro (estado, regras do
> harness sem o runner, comandos e armadilhas).

## Dados do jogo — nunca commitar

- `extracted/` (disco), `generated/` (C++ derivado do `default.xex`) e `sdk/` são **gitignored** e exigem o
  disco do próprio usuário. Antes de push: `tools/check_no_game_content.sh`.
- A trava em `src/dump_gate.h` confere o SHA-256 do `default.xex` contra `src/supported_dumps.h` e **recusa
  outros dumps** (exit 3, sem flag para desligar): um `generated/` só vale para um executável.

## Arquitetura

- `src/main.cpp` (`REX_DEFINE_APP(gh3, Gh3App::Create)`) + `src/gh3_app.h` = app ReXGlue.
- `generated/default/` = C++ recompilado. `src/native/` = renderer Vulkan nativo (GPL, adaptado do
  project8-sw), só compila com `GH3_NATIVE_RENDERER=ON` e é escolhido em runtime por `--gh3_renderer=native`
  (padrão = GPU Xenos emulada, ~5 FPS).
- `config/gh3_functions.toml` = `includes` do codegen: funções extras registradas **e** flags de codegen.
  `gh3_manifest.toml` = entrypoint.

## Build + execução no Mac

```bash
python3 -m venv .venv && .venv/bin/pip install pytest
tools/setup_sdk.sh          # sdk/ = ReXGlue f5337cd + 16 patches do project8-sw
tools/build_host_sdk.sh     # SDK + codegen + build → out/build/mac-arm64-p8sdk/gh3
.venv/bin/python -m pytest -q tests        # oráculo do projeto
GH3_BUILD_DIR=out/build/mac-arm64-p8sdk ./run-gh3.command [--gh3_renderer=native]
```

- Preset é **`mac-arm64-p8sdk`**. O `mac-arm64-release` (zip 0.10.0) foi **aposentado** e dá `#error` (falta o
  SDK patcheado). Não use.
- Rodar **em janela**: tela cheia + modo present `immediate`/`mailbox` trava no `CAMetalLayer nextDrawable` do
  MoltenVK → janela preta. O `run-gh3.command` já passa as flags certas e o teclado-como-guitarra.
- Codegen: `tools/codegen.sh` (rexglue → `split_register.py` → `llamadas_diretas.py`). O `split_register` é
  `POST_BUILD` de `gh3_codegen`; sem ele o rebuild leva 6–11 min.
- Opções de codegen (ex.: `cr_as_local`, `xer_as_local`) vão no **topo de `config/gh3_functions.toml`** — no
  manifesto são ignoradas.
- Não editar `generated/` fora do build: o passo de codegen do build regenera/sobrescreve. Ajustes via
  `POST_BUILD`. Não mexer em flags por arquivo: quebra o PCH (`__OPTIMIZE__`) — usar `SKIP_PRECOMPILE_HEADERS`.

## Verificações (aceite binário; abrem o jogo, custam minutos)

- Boot: `GH3_BUILD_DIR=out/build/mac-arm64-p8sdk python3 tools/boot_test.py --frames 300 --timeout 180 [-- CMD]`
  (conta linhas `XELOG_GPU PRESENT`).
- Captura/paridade: `python3 tools/gameplay_capture.py --renderer native|emulated --out out/parity/X.png`, depois
  `python3 tools/image_stats.py`.
- FPS: `python3 tools/gameplay_fps.py --renderer native --seconds 30 --out out/perf/X.json [--min-vs out/perf/baseline.json]`.
  Ruído de corrida ~±10%: um `--min-vs` vermelho isolado **não** prova regressão; repetir com a máquina fria.

## Build Switch (Docker amd64; detalhes no playbook §1)

```bash
docker build --platform linux/amd64 -t gh3-switch-clang docker/switch
tools/build_mesa.sh                    # NVK → out/driver-sdk
docker run --rm --platform linux/amd64 -v "$PWD":/project -w /project gh3-switch-clang sh -c \
  'cmake -S . -B build/switch -G Ninja -DCMAKE_BUILD_TYPE=Release -DGH3_SWITCH=ON \
     -DCMAKE_TOOLCHAIN_FILE=/project/cmake/switch-rexglue-gcc.cmake -DREXSDK_DIR=/project/sdk \
     -DREXGLUE_SWITCH_NVK_SDK=/project/out/driver-sdk && cmake --build build/switch -j 6'
python3 tools/check_nro.py build/switch/gh3.nro
```

O build Switch (`cmake/switch.cmake`) só compila o `generated/` já gerado pelo host: `CMakeLists.txt` faz
`return()` antes do glue do codegen.

## Armadilhas

- Queda `Call to invalid or unregistered function at guest address X`: funções minúsculas chamadas por
  ponteiro que a análise estática não vê. Fluxo barato→caro em `docs/porting-playbook.md` §3
  (`find_missing_funcs.py`, `discover_functions.sh`, `fix_last_crash.sh`). **Não** registrar todos os buracos
  (`--all`): passou no build mas derrubou o boot de ~3700 para 19 quadros.
- No Switch, `nm … | grep -q` morre de SIGPIPE sob `set -o pipefail`: use `grep … >/dev/null`.
- Arquivos enormes, não leia inteiros: `src/native/native_renderer.cpp` (~210 KB), `generated/default/*`.

## Documentação e fluxo

- `docs/porting-playbook.md` é a base de conhecimento (sintoma → causa → ação): **atualize ao resolver algo
  novo**. `docs/sessions/` = notas; `docs/superpowers/{specs,plans}/` = spec/planos.
- `.harness/gh3-marco-c/PLAN.md` é o plano do harness (formato do runner — não editar). No loop harness o
  runner commita e roda o `ORACLE_CMD` de `config.sh`; o agente não faz `git commit`/`push`.
- Mudança de comportamento entra com teste; meça antes/depois e registre no `registro-de-impacto`.
