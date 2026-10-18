# Passagem do harness — gh3-marco-c (2026-10-18)

Para quem continua a fila sem o runner do Claude (ex.: OpenCode + DeepSeek). Leia junto: `AGENTS.md`,
`docs/porting-playbook.md` (§7 e **§8**), `.harness/gh3-marco-c/PLAN.md` e `PROGRESS.md`.

## Estado agora

- Worktree `~/gh3-nx-wt/marco-c`, branch `harness/gh3-marco-c`. `main` (em `~/gh3-nx`) e o GitHub
  (github.com/guillermedev75/gh3-nx, público) estão no mesmo commit que a branch, incluindo o wip do T16.
  Publicar: `git -C ~/gh3-nx merge --ff-only harness/gh3-marco-c && git -C ~/gh3-nx push origin main`.
- Suíte: `.venv/bin/python -m pytest -q tests` → 162 passed.
- Runner do Claude **parado** (cota). Ninguém mais está mexendo na worktree.
- **No Switch o jogo é limitado pela GPU** (playbook §8). Modo desempenho (sem sombra e bloom) ligado no
  `switch/gh3.toml`: FPS mediano ~34 → ~45 nos clocks de fábrica. Otimizar CPU do jogo não sobe FPS até a GPU
  baixar de 16,7 ms por quadro.

## Fila (PLAN.md, nesta ordem)

| Tarefa | Estado | Nota |
|---|---|---|
| T16 cena 3D escalada (`gh3_native_scene_scale`) | **[!] bloqueada** | implementação de pé e correta (menu 0,75 vs 1,0: mean_diff 0,7); o aceite de gameplay compara duas capturas separadas e a marca wall-clock cai em trechos diferentes da música (2578 vs 2507 presents). Ver PROGRESS T16. |
| T17 present em anel (3 command buffers) | **[!] bloqueada** | o fence único é o fallback de cor de teste (`ClearOutput`); o gameplay usa `renderer_.Swap` + presenter do SDK e o renderer já tem `kSubmissionSlots = 4`. Ver PROGRESS T17. |
| T18 `sub_824D0070` cede o núcleo | **[x] aceita** | gancho em `sub_824CE958` só no lr do laço; log "installed 3". Ganho de CPU, não de FPS. |
| T19 `sub_8272C7C0` nativo com verificação | **[!] bloqueada** | 1862 linhas, 28 fmadds, 231 loads/146 stores; reescrita bit a bit não é confiável nesta sessão e o FPS não muda enquanto a GPU for o limite. |
| T20 NRO + pacote SD (T16–T20) | **[x] aceita** | aceite exit 0; ELF em `out/elf/gh3-7d12d79.elf`; pacote `out/sd/switch/gh3-nx`. |
| T21 `spirv-opt` nos shaders traduzidos | **[!] bloqueada** | o SDK não constrói spirv-tools (só headers; `libSPIRV.a` é glslang); exige patch no SDK + build nos dois toolchains; o gargalo é fill-rate. |
| T22 FP16 nos pixel shaders | **[!] bloqueada** | o NAK não suporta FP16/VRS nesta GPU (medido pelo nfsmw). |
| T23 NRO + pacote SD (T21–T22) | **[!] bloqueada** | depende de T21/T22. |
| T9 brilho | `[!]` | resolvido na prática pelo modo desempenho (era o bloom) |

## Passagem do DeepSeek (2026-10-18)

Commits desta passagem (branch `harness/gh3-marco-c`): `e6992f9` T16 bloqueada, `617c2c6` T17
bloqueada, `e9e9aad` T18 aceita, `a251dcf` T19 bloqueada, `0e7fe80` T20 aceita, `b1096bb`
T21-T23 bloqueadas. Suíte 162. A implementação do T16 está pronta; o que falta é prender a captura
ao tempo de música (ou a um frame de jogo após o início) para o comparativo valer. O T18 foi medido
no Mac (log/captura); o ganho de CPU no Switch ainda não foi medido. O pacote do SD (T20) está em
`out/sd/switch/gh3-nx`, com T16 a 0.75 e o modo desempenho.

## Como trabalhar sem o runner (as mesmas regras que ele impõe)

1. **Uma tarefa por vez, na ordem.** Antes: oráculo verde (`pytest -q tests`).
2. **Teste primeiro.** O `aceite:` tem que falhar antes e passar depois. Nunca editar a linha `aceite:` nem
   apagar/enfraquecer teste existente; nada de `pytest.skip`/`xfail`.
3. Pronto = **o comando `aceite:` inteiro sai 0** + oráculo verde. Rodar o comando exatamente como está no PLAN.
4. Marcar `[x]` no PLAN e acrescentar no `PROGRESS.md`, no formato das entradas anteriores:
   `### Tn — tentativa k` com `feito:`, `verificado:` (números), `decisões:`, `pendente:` e a linha
   `- AAAA-MM-DD HH:MM Tn ✅ aceita`.
5. Commit por tarefa: `harness(Tn): <título do PLAN>` (o PLAN/PROGRESS vão no mesmo commit). Sem a linha
   `Co-Authored-By: Claude` (não é o Claude que fez); escrever na mensagem que foi feito com DeepSeek.
6. Tarefa que falhar 3 vezes ou não tiver verificação automática possível: marcar `[!]`, explicar no PROGRESS,
   seguir para a próxima. **Nunca inventar aceite fraco.**
7. Achou algo novo (bug, medida, armadilha)? Registrar no `docs/porting-playbook.md` §8 no mesmo commit.

## Comandos que você vai usar

```bash
tools/build_host_sdk.sh                         # build do Mac (preset mac-arm64-p8sdk)
python3 tools/gameplay_capture.py --renderer native --out out/parity/x.png -- --cvar=valor
python3 tools/image_stats.py out/parity/x.png --max-white 0.15 --min-stddev 20
python3 tools/gameplay_fps.py --renderer native --seconds 30 --out out/perf/x.json --min-vs out/perf/baseline.json
# Switch (Docker); PGO desligado com -DGH3_PGO= (o cache atual está em "usar")
docker run --rm --platform linux/amd64 -v "$PWD":/project -w /project gh3-switch-clang sh -c \
  'cmake -S . -B build/switch -G Ninja -DCMAKE_BUILD_TYPE=Release -DGH3_SWITCH=ON -DGH3_PGO= \
   -DCMAKE_TOOLCHAIN_FILE=/project/cmake/switch-rexglue-gcc.cmake -DREXSDK_DIR=/project/sdk \
   -DREXGLUE_SWITCH_NVK_SDK=/project/out/driver-sdk && cmake --build build/switch -j 6'
cp build/switch/gh3 out/elf/gh3-$(shasum build/switch/gh3.nro | cut -c1-12).elf   # sempre: simbolizar perfis
tools/make_sd_package.sh --nro build/switch/gh3.nro
```

Medir no Switch (com o humano): FTP do DBI em `ftp://192.168.3.105:5000/switch/gh3-nx/`. Logs em `logs/gh3_NNN.log`
(linhas `[native] per frame: renderer CPU … GPU …` e `GPU by pass`) e `logs/rex/rex_perfil.log` (FPS a cada 10 s,
`fences esperadas`). Comparar sempre a **mesma música e o mesmo trecho**, pela mediana.

## Armadilhas conhecidas

- **Nunca commitar** `extracted/`, `generated/`, `sdk/`, `out/`, `build/`, `.gcda`, ELF/NRO nem arte do jogo.
  Antes de qualquer push: `tools/check_no_game_content.sh`. O pacote do SD é uso pessoal, nunca publicar.
- **Uma IA por worktree.** Se o runner do Claude voltar, ele faz `git add -A` e engole o que estiver solto.
- Medida no Mac tem ruído de ~±10%, e build do Docker rodando junto derruba o FPS do Mac (580% de CPU na VM).
- O profiler conta syscall de espera como CPU; simbolizar só com o ELF do **mesmo** NRO.
- `git diff` não mostra o que está no índice: conferir com `git status --short` e `git diff --cached`.

## Para retomar com o runner do Claude (quando houver cota)

```bash
cd ~/gh3-nx-wt/marco-c && git status --short   # tem que estar limpo
caffeinate -i ~/.claude/skills/harness/scripts/harness-loop.sh .harness/gh3-marco-c >> out/harness-loop.out 2>&1 &
```

Ele recomeça da primeira tarefa `[ ]` do PLAN.
