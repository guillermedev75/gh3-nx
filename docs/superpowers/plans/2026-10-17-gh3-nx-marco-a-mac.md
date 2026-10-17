# gh3-nx — Marco A: GH3 recompilado rodando no Mac (GPU emulada) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Formato do harness: aceites binários na seção final.

**Goal:** Recompilar o `default.xex` do Guitar Hero III (Xbox 360, USA Rev 1) com ReXGlue 0.10.0 e ter um binário macOS arm64 que abre o jogo até o menu, com um teste de boot automático. Marco mínimo: só prova que a recompilação está correta antes de ir para o Switch.

**Architecture:** Ferramentas próprias em Python (verificação do XEX, codegen, teste de boot) testadas com pytest; o app é um projeto CMake sobre o ReXGlue SDK para macOS (runtime com GPU emulada via MoltenVK). Nada do jogo entra no git.

**Tech Stack:** ReXGlue SDK 0.10.0 (`rexglue-sdk-0.10.0-mac-arm64.zip`), CMake ≥ 3.25, Ninja, LLVM/clang do Homebrew, Python 3 + pytest.

**Spec:** `docs/superpowers/specs/2026-10-17-gh3-nx-design.md`

## Global Constraints

- Licença do repositório: GPL-3.0. Código de `luisxl15/Guitar-Hero-3-Recomp-` **não** é copiado (sem licença): só fatos públicos do executável (endereços, IDs).
- `extracted/`, `generated/`, `build/`, `out/`, `tools/rexglue/` nunca entram no git.
- Versão do jogo suportada: USA Rev 1, title id `0x415607F7`, media id `0x43048167`, base `0x82000000`, entry `0x82136AC8`.
- Oráculo do projeto: `.venv/bin/python -m pytest -q tests`

---

### Task 0: Repositório, toolchain e primeiro teste verde (no chat, antes do loop)

**Files:** Create `tests/test_smoke.py`, `tools/__init__.py`, `tools/fetch_rexglue.sh`, `README.md`, `LICENSE` (GPL-3.0)

- [ ] **Step 1: `tools/fetch_rexglue.sh`**

```bash
#!/usr/bin/env bash
# Baixa o ReXGlue SDK 0.10.0 para macOS arm64 em tools/rexglue (ignorado pelo git).
set -euo pipefail
cd "$(dirname "$0")"
v=0.10.0
[ -x rexglue/mac-arm64/bin/rexglue ] && { echo "rexglue $v já presente"; exit 0; }
curl -fL -o /tmp/rexglue-$v.zip "https://github.com/rexglue/rexglue-sdk/releases/download/v$v/rexglue-sdk-$v-mac-arm64.zip"
mkdir -p rexglue && unzip -q -o /tmp/rexglue-$v.zip -d rexglue
ls rexglue
```

- [ ] **Step 2:** `bash tools/fetch_rexglue.sh` e registrar no README o caminho real do binário `rexglue` dentro do zip (ajustar o teste do Step 3 se for outro).
- [ ] **Step 3: `tests/test_smoke.py`**

```python
import pathlib, subprocess

def test_rexglue_cli_runs():
    candidates = list(pathlib.Path("tools/rexglue").rglob("rexglue"))
    exe = next(p for p in candidates if p.is_file())
    out = subprocess.run([str(exe), "--help"], capture_output=True, text=True, timeout=60)
    assert out.returncode == 0 or "codegen" in (out.stdout + out.stderr)
```

- [ ] **Step 4:** `.venv/bin/python -m pytest -q tests` → verde. **Step 5:** commit `chore: scaffold, ReXGlue 0.10.0, smoke test`.

---

### Gate humano H1: copiar o disco (fora do loop)

O Guillerme copia o **próprio** disco do GH3 de 360 para `extracted/` (precisa de `extracted/default.xex` e dos arquivos de dados). Não há forma automática de verificar a origem; a Task 1 verifica a versão.

---

### Task 1: `tools/check_xex.py` — confirmar que o XEX é o GH3 USA Rev 1

**Files:** Create `tools/check_xex.py`, `tests/test_check_xex.py`

**Interfaces:** Produces `parse_xex(data: bytes) -> dict` com chaves `image_base`, `entry_point`, `title_id`, `media_id`; e `check(path) -> list[str]` (lista de problemas; vazia = OK). CLI: `python3 tools/check_xex.py extracted/default.xex` sai 0 se OK.

Formato XEX2 (big-endian): magic `XEX2` em 0x0; `u32` em 0x14 = número de cabeçalhos opcionais; a partir de 0x18, pares `(key u32, value u32)`. Chaves usadas: `0x00010100` entry point (valor direto), `0x00010201` image base (valor direto), `0x00040006` execution info (valor = offset de uma struct de 24 bytes: media_id u32 @0, version u32 @4, base_version u32 @8, title_id u32 @12, ...).

- [ ] **Step 1: teste que falha — `tests/test_check_xex.py`**

```python
import struct
from tools.check_xex import parse_xex, check_bytes

def fake_xex(entry=0x82136AC8, base=0x82000000, title=0x415607F7, media=0x43048167):
    headers = [(0x00010100, entry), (0x00010201, base), (0x00040006, 0x100)]
    b = bytearray(0x200)
    b[0:4] = b"XEX2"
    struct.pack_into(">I", b, 0x14, len(headers))
    for i, (k, v) in enumerate(headers):
        struct.pack_into(">II", b, 0x18 + 8 * i, k, v)
    struct.pack_into(">IIIIII", b, 0x100, media, 0, 0, title, 0, 0)
    return bytes(b)

def test_parse_fields():
    info = parse_xex(fake_xex())
    assert info == {"entry_point": 0x82136AC8, "image_base": 0x82000000, "title_id": 0x415607F7, "media_id": 0x43048167}

def test_gh3_rev1_accepted():
    assert check_bytes(fake_xex()) == []

def test_other_game_refused():
    problems = check_bytes(fake_xex(title=0x12345678))
    assert any("title_id" in p for p in problems)

def test_not_xex_refused():
    assert check_bytes(b"\x00" * 64) == ["não é um XEX2"]
```

Run: `.venv/bin/python -m pytest -q tests/test_check_xex.py` → FAIL (módulo não existe).

- [ ] **Step 2: `tools/check_xex.py`**

```python
"""Confere se um default.xex é o Guitar Hero III USA Rev 1 suportado por este port."""
import struct, sys

EXPECTED = {"title_id": 0x415607F7, "media_id": 0x43048167, "image_base": 0x82000000, "entry_point": 0x82136AC8}

def parse_xex(data: bytes) -> dict:
    if data[:4] != b"XEX2":
        raise ValueError("não é um XEX2")
    (count,) = struct.unpack_from(">I", data, 0x14)
    info = {}
    for i in range(count):
        key, value = struct.unpack_from(">II", data, 0x18 + 8 * i)
        if key == 0x00010100:
            info["entry_point"] = value
        elif key == 0x00010201:
            info["image_base"] = value
        elif key == 0x00040006:
            media, _, _, title = struct.unpack_from(">IIII", data, value)
            info["media_id"], info["title_id"] = media, title
    return info

def check_bytes(data: bytes) -> list:
    try:
        info = parse_xex(data)
    except ValueError as e:
        return [str(e)]
    return [f"{k}: esperado 0x{v:08X}, encontrado 0x{info.get(k, 0):08X}" for k, v in EXPECTED.items() if info.get(k) != v]

def check(path) -> list:
    with open(path, "rb") as f:
        return check_bytes(f.read(0x10000))

if __name__ == "__main__":
    problems = check(sys.argv[1])
    print("OK: Guitar Hero III USA Rev 1" if not problems else "\n".join(problems))
    sys.exit(1 if problems else 0)
```

- [ ] **Step 3:** testes verdes. **Step 4:** commit `feat: verificação do default.xex (GH3 USA Rev 1)`.

---

### Task 2: Manifest próprio e codegen com ReXGlue 0.10.0

**Files:** Create `gh3_manifest.toml`, `config/gh3_functions.toml` (vazio no início), `tools/run_codegen.py`, `tests/test_codegen_report.py`

**Interfaces:** `run_codegen.py` roda `rexglue codegen gh3_manifest.toml`, salva o log em `out/codegen.log` e escreve `out/codegen_summary.json` com `{"exit": int, "unresolved": int, "functions": int}` (contagens tiradas do log). Sai 0 só se `exit == 0` e `unresolved == 0`.

- [ ] **Step 1:** ler a wiki do ReXGlue (`https://github.com/rexglue/rexglue-sdk/wiki`) para o formato de manifest da 0.10.0 e o texto exato que o codegen usa para chamadas não resolvidas e para a contagem de funções. Anotar no topo de `tools/run_codegen.py` os padrões de linha usados.
- [ ] **Step 2: teste que falha** — `tests/test_codegen_report.py` com um log sintético contendo 2 linhas de chamada não resolvida e 1 linha de total de funções (no formato anotado no Step 1), verificando que `summarize(log_text)` devolve `unresolved == 2` e `functions == <total>`.
- [ ] **Step 3:** implementar `summarize()` e o CLI; `gh3_manifest.toml` escrito do zero com `[project] name = "gh3"`, `sdk_version = "0.10.0"`, `[entrypoint] file_path = "extracted/default.xex"`, `out_directory_path = "generated/default"`, `includes = ["config/gh3_functions.toml"]`.
- [ ] **Step 4:** testes verdes; commit `feat: manifest e codegen (ReXGlue 0.10.0)`.

---

### Task 3: Zerar chamadas não resolvidas

**Files:** Modify `config/gh3_functions.toml`

- [ ] Rodar `python3 tools/run_codegen.py`; para cada alvo de chamada não resolvida do log, adicionar a fronteira de função em `config/gh3_functions.toml` (formato da wiki), comentando de onde veio cada endereço (log do codegen). Repetir até `unresolved == 0`. Nada de copiar o arquivo equivalente do luisxl15.
- [ ] Commit `fix: fronteiras de função até codegen limpo`.

---

### Task 4: App macOS sobre o ReXGlue

**Files:** Create `CMakeLists.txt`, `src/main.cpp`, `src/app_config.h`

- [ ] Seguir o template de app da wiki do ReXGlue (`find_package(rexglue)`, `rexglue_DIR=tools/rexglue/mac-arm64/lib/cmake/rexglue`, fontes de `generated/default`), com o runtime padrão (GPU emulada, Vulkan via MoltenVK incluído no SDK), lendo os dados de `extracted/`.
- [ ] Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -Drexglue_DIR=$PWD/tools/rexglue/mac-arm64/lib/cmake/rexglue && ninja -C build`
- [ ] Commit `feat: app macOS (runtime ReXGlue, GPU emulada)`.

---

### Task 5: Teste de boot automático

**Files:** Create `src/frame_counter.cpp` (hook no ponto em que o runtime apresenta um quadro — o equivalente a `VdSwap`; localizar no SDK), `tools/boot_test.py`, `tests/test_boot_test.py`

**Interfaces:** o app grava `out/frames.txt` com o total de quadros apresentados a cada segundo. `boot_test.py --frames 300 --timeout 180` inicia o app, espera e sai 0 se `frames.txt` atingir o alvo antes do timeout; mata o app no fim.

- [ ] Teste que falha (`tests/test_boot_test.py`): com um processo falso que escreve números crescentes em `frames.txt`, `boot_test.py` sai 0; com um que trava em 10, sai 1 no timeout.
- [ ] Implementar hook + script; commit `feat: teste de boot por quadros apresentados`.

---

## Fora do loop (humano)

- **H1** — copiar o próprio disco para `extracted/`.
- Conferir visualmente que o menu do GH3 aparece no Mac (a Task 5 só prova que quadros estão saindo).

## Tarefas para o harness

T0 no chat. Loop em `.harness/gh3-marco-a/PLAN.md`:

```
- [ ] T1: check_xex — confirmar GH3 USA Rev 1 (plano: Task 1)
  aceite: .venv/bin/python -m pytest -q tests/test_check_xex.py
- [ ] T2: manifest e codegen ReXGlue 0.10.0 (plano: Task 2)
  aceite: .venv/bin/python -m pytest -q tests/test_codegen_report.py && test -f gh3_manifest.toml
- [ ] T3: codegen sem chamadas não resolvidas (plano: Task 3)
  aceite: python3 tools/check_xex.py extracted/default.xex && python3 tools/run_codegen.py
- [ ] T4: app macOS compila (plano: Task 4)
  aceite: cmake --build build && test -x build/gh3
- [ ] T5: teste de boot — 300 quadros (plano: Task 5)
  aceite: .venv/bin/python -m pytest -q tests/test_boot_test.py && python3 tools/boot_test.py --frames 300 --timeout 180
```

`ORACLE_CMD='.venv/bin/python -m pytest -q tests'`. T3–T5 dependem do gate H1 (disco copiado).

## Marcos seguintes (planos próprios, depois do A)

- **C — Switch + renderer nativo:** camada Horizon de `project8-sw/patches/rexglue-sdk/`, devkitA64 + libnx + NVK (`danfromtico/mesa-switch`), NRO em title takeover, espaço de endereço de 39 bits, e o renderer nativo de `R-drg/project8-sw` (`docs/native-renderer.md`) portado direto para o console. (O antigo marco B no Mac foi removido em 2026-10-17; volta se o C travar.)
- **D — desempenho:** escada do `nfsmw-nx/docs/performance-history.md` desde o primeiro dia; medir sem overclock; 60 FPS e percentis de frame time; áudio de baixa latência; cache de shaders completo na primeira execução.
