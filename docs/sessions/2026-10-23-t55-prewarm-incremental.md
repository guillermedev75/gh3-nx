# 2026-10-23 — T55: prewarm incremental + diagnóstico da rajada

## Ponto de partida (medido no Switch)
- `gh3_033` (1ª abertura, cache SPIR-V novo): `spirv cache: 7 hits, 40 stored` → 140; prewarm de 159
  pipelines **bloqueia o boot por 40 s** (10:29:19 → 10:29:59).
- `gh3_034` (2ª abertura): `spirv-opt: 0 shaders | spirv cache: 178 hits, 0 stored`; prewarm **1,9 s**.
  O cache funciona; só a primeira abertura pagava os 40 s.

## Prewarm incremental
- `PrewarmPipelines` não compila mais nada: parse → `prewarm_queue` e log `N queued, incremental over the
  first frames`.
- `DrainPrewarm()` roda no thread do anel a cada quadro (na função de fim de quadro): prepara até
  `kPrewarmBatch` entradas (tradução, geometria, layout) dentro de `kPrewarmBudgetMs = 2 ms` e as entrega
  ao worker de pipelines; no máximo `kPrewarmInFlight = 2` em voo, liberadas em `DrainCompletedPipelines`.
- `prewarm::Budget` (header puro): média móvel do custo por entrada; `Take(pending)` devolve quantas cabem no
  orçamento, no mínimo 1 (progresso) e no máximo o lote.
- `gh3_native_pipeline_prewarm_incremental=false` restaura o comportamento antigo (A/B).
- `tools/check_prewarm_spread.py`: exige ≥2 fatias, `done ... over F frames` com F ≥ 2 e ≥1
  `XELOG_GPU PRESENT` **antes** do done.

## Diagnóstico da rajada
- `gh3_native_gpu_profile_every` (padrão 16) e `gh3_native_gpu_profile_slow_ms` (padrão 10): cada submissão
  acima do limiar loga `GPU by pass, slow frame (X ms, N draws): | pass ...`, no máx. 1 linha/250 ms e 12
  por período. Com `every=1` perfila todos os quadros.
- Dados que motivaram (gh3_034): 511 quadros >50 ms em 7 min (~4 %), em rajadas de 6–12 quadros; GPU
  25–38 ms nelas, draws 379–575 contra ~270; clocks fixos (CPU 1020, GPU 460,8,
  RAM 1331 MHz) e SoC 37–39 °C. (O "5,3 ms normal" que estava aqui era `frame_fence_wait_ns`, espera do
  fence, não GPU: a medição por timestamps do `gh3_035` dá p50 26,0 ms no portátil — ver §13 do playbook.)

## Verificação
- `pytest -q tests`: **326 passed** (7 novos: 1 de orçamento no `test_pipeline_prewarm.py`, 6 no
  `test_prewarm_spread.py`).
- Captura `out/parity/native-t55.png`: white=0,013 stddev=53,2 (aceite do T55); log `142 queued` e
  `done in 3782 ms over 141 frames (140 submitted, 0 failed, 2 skipped)`; `check_prewarm_spread.py` ok.
- Mac: 1º `XELOG_GPU PRESENT` **0,21 s** depois de `Renderer ready` (log antigo da mesma captura: 1,33 s).
- `GPU by pass, slow frame` validado com `--gh3_native_gpu_profile=true --gh3_native_gpu_profile_every=1
  --gh3_native_gpu_profile_slow_ms=20`: 18 linhas em ~30 s, sem enxurrada.
- NRO Switch `0x3d96000` (64.625.734 B), sha256 `debd25d4f06767226d2d157bcd7853375fb3addfe9c281210620eec2082d78fe`;
  pacote `out/sd` conferido com `tools/check_no_game_content.sh`.

## Pendente (gate humano)
- Switch: medir a 1ª abertura (antes 40 s de bloqueio) e rodar ~30 s com
  `--gh3_native_gpu_profile=true --gh3_native_gpu_profile_every=1` para ver qual passada come os 25–38 ms
  da rajada (suspeita: a passada extra de ~110 draws, possivelmente a plateia — o print do quadro
  replicado de 10:00 continua valendo).
