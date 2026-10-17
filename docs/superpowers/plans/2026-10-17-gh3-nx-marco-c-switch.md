# gh3-nx Marco C — Switch + renderer nativo — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Execução padrão: harness (`.harness/gh3-marco-c/PLAN.md`, seção no fim).

**Goal:** o GH3 recompilado abre no Switch (Atmosphère, title takeover) com o renderer nativo do project8-sw, com cada passo verificável no Mac antes de ir ao console.

**Architecture:** troca o SDK ReXGlue 0.10.0 pronto pelo SDK compilado do código-fonte com a série de patches do project8-sw (camada Horizon, pipeline cache, resolves). O renderer nativo do project8-sw (motor Neversoft, mesmo do GH3) é multiplataforma: roda no Mac via MoltenVK, então é validado aqui com `tools/boot_test.py` antes do console. O NRO é compilado num container Docker com devkitA64 + libnx + NVK (mesa-switch com os patches do project8-sw).

**Tech Stack:** ReXGlue SDK v0.10.0 (commit `f5337cd`) + `project8-sw/patches/rexglue-sdk` (16 patches); devkitPro devkitA64/libnx em Docker (`devkitpro/devkita64` + clang-19 + glslang); mesa-switch `d4a00ea0ab3` + `project8-sw/patches/mesa-switch`; CMake/Ninja; Python 3 + pytest.

**Spec:** `docs/superpowers/specs/2026-10-17-gh3-nx-design.md`

## Global Constraints

- Licença GPL-3.0; código vindo do project8-sw/nfsmw-nx mantém os cabeçalhos e entra no `NOTICE`.
- Nenhum arquivo do jogo nem código gerado a partir dele no repositório (`extracted/`, `generated/`, `out/`, `build/`).
- Ninguém baixa ROM/ISO; o jogo vem do disco do Guillerme.
- Medir sem overclock (clocks de fábrica); nada de frame generation (jogo de ritmo).
- Referências locais (somente leitura): `~/gh3-refs/project8-sw`, `~/gh3-refs/nfsmw-nx`.
- Toda mudança de comportamento entra com teste; `ORACLE_CMD='.venv/bin/python -m pytest -q tests'` verde antes e depois.
- Documentar cada melhoria/problema em `docs/porting-playbook.md` no mesmo commit.

## Estrutura de arquivos

| Arquivo | Responsabilidade |
|---|---|
| `tools/setup_sdk.sh` | clona `rexglue-sdk` em `sdk/` no commit `f5337cd`, aplica a série do project8-sw; `--check` confere |
| `tools/build_host_sdk.sh` | compila o SDK patcheado para macOS arm64 (rexglue + runtime + plugin xenos) |
| `CMakePresets.json` | novo preset `mac-arm64-p8sdk` apontando para o SDK compilado do código-fonte |
| `src/native/` | renderer nativo (cópia adaptada de `project8-sw/src/game/src/native/`) |
| `src/gh3_app.h` | cvar `gh3_renderer` (`"emulated"` padrão, `"native"`) que troca o sistema gráfico |
| `docker/switch/Dockerfile` | imagem da toolchain (cópia do project8-sw) |
| `tools/build_mesa.sh` | constrói o NVK patcheado e monta o "driver SDK" (`lib/libvulkan.a`) |
| `cmake/switch.cmake` | alvo NRO (cópia adaptada do project8-sw) |
| `tools/check_nro.py` | valida o NRO (magia `NRO0`, tamanho, asset section) |
| `tools/gen_supported_dumps.py` + `src/supported_dumps.h` | trava de SHA-256 do `default.xex` na partida |
| `tools/make_sd_package.sh` + `tools/check_no_game_content.sh` | monta `out/sd/switch/gh3-nx/` sem conteúdo do jogo |

---

### Task 1: SDK patcheado do código-fonte

**Files:** Create `tools/setup_sdk.sh`, `tests/test_setup_sdk.py`; Modify `.gitignore` (`sdk/`).

**Interfaces:** Produces `sdk/` (árvore do SDK patcheada); `tools/setup_sdk.sh --check` sai 0 só se todos os patches da série estão aplicados.

- [ ] **Step 1: teste que falha** — `tests/test_setup_sdk.py`:

```python
import subprocess

def test_check_reports_unpatched_tree(tmp_path):
    # árvore git vazia: --check precisa falhar
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    r = subprocess.run(["tools/setup_sdk.sh", "--check", "--sdk", str(tmp_path)], capture_output=True)
    assert r.returncode != 0

def test_series_is_read_in_order():
    out = subprocess.run(["tools/setup_sdk.sh", "--list"], capture_output=True, text=True, check=True).stdout.split()
    assert out[0] == "0001-thp8-runtime-stack.patch" and out[-1] == "0016-switch-runtime-tuning.patch" and len(out) == 16
```

- [ ] **Step 2:** `.venv/bin/python -m pytest -q tests/test_setup_sdk.py` → FAIL (script não existe).
- [ ] **Step 3: implementar** `tools/setup_sdk.sh`:

```bash
#!/usr/bin/env bash
# SDK ReXGlue v0.10.0 (f5337cd) + série de patches do project8-sw (camada Horizon, renderer nativo).
set -euo pipefail
cd "$(dirname "$0")/.."
P8=${P8:-$HOME/gh3-refs/project8-sw}
SERIES="$P8/patches/rexglue-sdk/series"
SDK=sdk
[ "${1:-}" = "--list" ] && { cat "$SERIES"; exit 0; }
[ "${2:-}" = "--sdk" ] || [ "${1:-}" = "--sdk" ] && SDK="${3:-${2:-sdk}}"
if [ "${1:-}" = "--check" ]; then
  [ "${2:-}" = "--sdk" ] && SDK="$3"
  while read -r p; do git -C "$SDK" apply --check -R "$P8/patches/rexglue-sdk/$p" 2>/dev/null || { echo "não aplicado: $p"; exit 1; }; done < "$SERIES"
  echo "SDK patcheado: OK"; exit 0
fi
[ -d "$SDK/.git" ] || git clone -q https://github.com/rexglue/rexglue-sdk "$SDK"
git -C "$SDK" checkout -q f5337cd
git -C "$SDK" submodule update --init --recursive -q
while read -r p; do git -C "$SDK" apply --index "$P8/patches/rexglue-sdk/$p"; done < "$SERIES"
"$0" --check
```

- [ ] **Step 4:** testes passam; rodar `tools/setup_sdk.sh` de verdade e `tools/setup_sdk.sh --check` → `SDK patcheado: OK`.
- [ ] **Step 5:** commit `feat: SDK ReXGlue patcheado (série do project8-sw)` + linha no playbook.

### Task 2: GH3 no Mac com o SDK patcheado (renderer emulado)

**Files:** Create `tools/build_host_sdk.sh`; Modify `CMakePresets.json` (preset `mac-arm64-p8sdk`, `binaryDir` `out/build/mac-arm64-p8sdk`), `run-gh3.command` (aceita `GH3_BUILD_DIR`, padrão `out/build/mac-arm64-release`), `tools/boot_test.py` (repassa o ambiente — já repassa).

**Interfaces:** Consumes `sdk/` (Task 1). Produces `out/build/mac-arm64-p8sdk/gh3`; `GH3_BUILD_DIR=out/build/mac-arm64-p8sdk ./run-gh3.command` abre esse binário.

- [ ] **Step 1:** `tools/build_host_sdk.sh`: `cmake -S sdk -B out/sdk-host -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build out/sdk-host && cmake --install out/sdk-host --prefix out/sdk-install`.
- [ ] **Step 2:** preset `mac-arm64-p8sdk` com `rexglue_DIR=${sourceDir}/out/sdk-install/lib/cmake/rexglue`; `run-gh3.command` troca `out/build/mac-arm64-release` por `${GH3_BUILD_DIR:-out/build/mac-arm64-release}`.
- [ ] **Step 3:** codegen com o `rexglue` patcheado (`out/sdk-install/bin/rexglue codegen gh3_manifest.toml`) e build do preset; anotar no playbook diferenças de codegen (contagem de funções, avisos).
- [ ] **Step 4:** `GH3_BUILD_DIR=out/build/mac-arm64-p8sdk python3 tools/boot_test.py --frames 300 --timeout 180` → `OK`.
- [ ] **Step 5:** commit `build: GH3 sobre o SDK patcheado do project8-sw`.

### Task 3: renderer nativo no Mac

**Files:** Create `src/native/*` (cópia de `project8-sw/src/game/src/native/`, renomeando `thps_` → `gh3_` só nos nomes públicos), `NOTICE`; Modify `CMakeLists.txt` (fontes nativas), `src/gh3_app.h` (cvar `gh3_renderer`, mesma troca do `thps_p8_app.h`: em `OnPreSetup`, se `"native"`, instala o sistema gráfico nativo).

**Interfaces:** Consumes build da Task 2. Produces `--gh3_renderer=native` na linha de comando.

- [ ] **Step 1:** ler `project8-sw/src/game/src/thps_p8_app.h` e copiar só a parte que escolhe o renderer (procurar `thps_renderer`); remover hooks com endereços do THPS (70 endereços `0x82xxxxxx` — são do outro jogo; ficam para o marco D).
- [ ] **Step 2:** build do preset `mac-arm64-p8sdk`.
- [ ] **Step 3:** `GH3_BUILD_DIR=out/build/mac-arm64-p8sdk python3 tools/boot_test.py --frames 300 --timeout 180 -- ./run-gh3.command --gh3_renderer=native --log_level debug` → precisa de contagem de quadro própria do nativo: se o nativo não escreve `XELOG_GPU PRESENT`, adicionar uma linha de log por swap em `gh3_native_system.cpp` (`REXGPU_DEBUG("XELOG_GPU PRESENT native")`) e um teste em `tests/test_boot_test.py` que aceite essa forma.
- [ ] **Step 4:** conferência visual com o Guillerme (fora do loop): menu e uma música com `--gh3_renderer=native`.
- [ ] **Step 5:** commit `feat: renderer nativo (project8-sw) no Mac` + playbook.

### Task 4: toolchain Switch e driver NVK

**Files:** Create `docker/switch/Dockerfile` (cópia do project8-sw), `tools/build_mesa.sh`.

**Interfaces:** Produces `out/driver-sdk/lib/libvulkan.a` e `out/driver-sdk/lib/libnak_rs.a`.

- [ ] **Step 1:** `docker build --platform linux/amd64 -t gh3-switch-clang docker/switch` (a imagem devkitA64 é amd64; no M1 roda emulada — medir o tempo e anotar no playbook).
- [ ] **Step 2:** `tools/build_mesa.sh`: clona `danfromtico/mesa-switch` em `out/mesa`, `checkout d4a00ea0ab3`, aplica `project8-sw/patches/mesa-switch/0*.patch`, roda `build-switch-p8.sh`, junta as libs com `project8-sw/tools/switch/stage-mesa-archive.py` em `out/driver-sdk/lib/`.
- [ ] **Step 3:** aceite: `test -f out/driver-sdk/lib/libvulkan.a && nm out/driver-sdk/lib/libvulkan.a 2>/dev/null | grep -q vkCreateInstance`.
- [ ] **Step 4:** commit `build: toolchain Switch e NVK patcheado`.

### Task 5: NRO

**Files:** Create `cmake/switch.cmake` (adaptado de `project8-sw/src/game/cmake/switch.cmake`), `tools/check_nro.py`, `tests/test_check_nro.py`, `switch/icon.jpg` (arte própria, sem logo do jogo).

**Interfaces:** Consumes `sdk/`, `out/driver-sdk/`. Produces `build/switch/gh3.nro`; `check_nro.py ARQ` sai 0 se válido.

- [ ] **Step 1: teste que falha**:

```python
import struct, subprocess, sys

def nro(tmp_path, magic=b"NRO0", size=0x1000):
    p = tmp_path / "x.nro"
    head = bytearray(0x80); head[0x10:0x14] = magic; struct.pack_into("<I", head, 0x18, size)
    p.write_bytes(bytes(head) + b"\0" * (size - 0x80)); return p

def test_valid_nro(tmp_path):
    assert subprocess.run([sys.executable, "tools/check_nro.py", str(nro(tmp_path))]).returncode == 0

def test_bad_magic(tmp_path):
    assert subprocess.run([sys.executable, "tools/check_nro.py", str(nro(tmp_path, b"XXXX"))]).returncode == 1
```

- [ ] **Step 2:** FAIL; implementar `check_nro.py` (lê magia em 0x10 e tamanho em 0x18, confere com o arquivo; aceita asset section depois do tamanho).
- [ ] **Step 3:** build no container (comando do `project8-sw/docs/building.md` §4, com `-S .` e `-DGH3_SWITCH=ON`).
- [ ] **Step 4:** aceite: `python3 tools/check_nro.py build/switch/gh3.nro`.
- [ ] **Step 5:** commit `build: NRO do GH3`.

### Task 6: trava do executável e pacote do SD

**Files:** Create `tools/gen_supported_dumps.py`, `src/supported_dumps.h` (gerado só com hashes, sem conteúdo do jogo), `tools/make_sd_package.sh`, `tools/check_no_game_content.sh` (adaptado do project8-sw), `tests/test_sd_package.py`.

**Interfaces:** Produces `out/sd/switch/gh3-nx/{gh3-nx.nro,gh3.toml,LICENSES.txt}`; o jogo recusa `default.xex` de SHA-256 desconhecido.

- [ ] **Step 1: teste que falha** — `test_sd_package.py`: roda `make_sd_package.sh` com um NRO falso e confere a árvore exata e que nenhum `.xex`/`.iso`/`DATA` entrou.
- [ ] **Step 2:** implementar; SHA-256 do `extracted/default.xex` calculado na hora pelo `gen_supported_dumps.py` (hash não é conteúdo do jogo).
- [ ] **Step 3:** aceite: `.venv/bin/python -m pytest -q tests/test_sd_package.py && tools/check_no_game_content.sh`.
- [ ] **Step 4:** commit `feat: pacote do SD e trava do default.xex`.

## Fora do loop (Guillerme + console)

- **H2:** copiar `out/sd/switch/gh3-nx/` + `game/` (do próprio disco) para o SD; abrir em title takeover (segurar R ao abrir um jogo instalado).
- **H3:** primeiro com `--gh3_renderer=emulated` (prova de vida, 1–5 FPS esperado), depois `native`; mandar `logs/` de volta.
- Medir FPS com Horizon OC Monitor, clocks de fábrica, portátil e dock. Isso abre o marco D.

## Tarefas para o harness

```
- [ ] T1: SDK patcheado do código-fonte (plano: Task 1)
  aceite: .venv/bin/python -m pytest -q tests/test_setup_sdk.py && tools/setup_sdk.sh --check
- [ ] T2: GH3 no Mac com o SDK patcheado (plano: Task 2)
  aceite: GH3_BUILD_DIR=out/build/mac-arm64-p8sdk python3 tools/boot_test.py --frames 300 --timeout 180
- [ ] T3: renderer nativo no Mac (plano: Task 3)
  aceite: GH3_BUILD_DIR=out/build/mac-arm64-p8sdk python3 tools/boot_test.py --frames 300 --timeout 180 -- ./run-gh3.command --gh3_renderer=native --log_level debug
- [ ] T4: toolchain Switch e NVK (plano: Task 4)
  aceite: test -f out/driver-sdk/lib/libvulkan.a && nm out/driver-sdk/lib/libvulkan.a 2>/dev/null | grep -q vkCreateInstance
- [ ] T5: NRO (plano: Task 5)
  aceite: .venv/bin/python -m pytest -q tests/test_check_nro.py && python3 tools/check_nro.py build/switch/gh3.nro
- [ ] T6: trava do executável e pacote do SD (plano: Task 6)
  aceite: .venv/bin/python -m pytest -q tests/test_sd_package.py && tools/check_no_game_content.sh
```

`ORACLE_CMD='.venv/bin/python -m pytest -q tests'`. T4 é independente de T1–T3 e pode rodar em paralelo (Docker).

## Riscos

- devkitA64 em Docker amd64 emulado no M1: lento; se inviável, construir numa máquina Linux/CI.
- Patches do project8-sw podem ter suposições do THPS (ex.: `0001-thp8-runtime-stack.patch`); ler cada um antes de aplicar e anotar no playbook o que é específico do jogo.
- Bink no Switch: o project8-sw tem cutscenes travadas por áudio (ver `known-issues.md`); o GH3 abre com vídeo Bink — testar cedo no H3.
- Meta de 60 FPS: o project8-sw mira 30 no mesmo motor; o GH3 é mais leve, mas a meta só se confirma no marco D.
