---
created: 2026-10-17
updated: 2026-10-19
---

# 2026-10-17 — Marco A: GH3 roda no Mac com imagem e som

Estado: o `default.xex` (USA/Europa multi 5) recompilado roda no macOS arm64 com a GPU emulada (plugin
`xenos` sobre MoltenVK), intro em vídeo com som e imagem. Abrir com `run-gh3.command`.

## O que destravou cada etapa

| Sintoma | Causa | Correção |
|---|---|---|
| Tela preta, sem som | ReXGlue 0.10 tira a emulação de GPU do núcleo (`gpu_plugin` vazio) | `rexglue_setup_target(gh3 GPU_PLUGINS xenos)` + `--gpu_plugin xenos` |
| Quedas `Call to invalid or unregistered function` | chamadas indiretas que a análise estática não vê | `config/gh3_functions.toml`; `tools/discover_functions.sh` acha e registra sozinho (9 funções, 7 pelo laço) |
| Cada rodada levava 6 a 11 min | `gh3_register.cpp` = uma função com ~60 mil chamadas | `tools/split_register.py` em POST_BUILD do `gh3_codegen` → rebuild 7,5 s |
| Jogo rodando com som, janela preta, F4 não abre | modo de apresentação immediate em tela cheia trava em `CAMetalLayer nextDrawable` (MoltenVK) | janela 1280×720 + FIFO (`--vulkan_allow_present_mode_immediate=false --vulkan_allow_present_mode_mailbox=false`) |

## Gameplay (mesma noite)

- Teclado como guitarra no `run-gh3.command` (1–5 trastes, setas palhetada, Enter, Tab).
- Guillerme chegou ao início de música no Easy; quedas por função não registrada no carregamento da música.
- Em vez de corrigir uma por uma: `tools/find_missing_funcs.py` (dump da imagem + buracos entre funções +
  ponteiros) registrou 91 funções de uma vez (106 candidatos − 15 falsos positivos via `--prune`). Boot
  continua limpo (60 s, 3772 quadros). Detalhes e fluxo reaproveitável em `docs/porting-playbook.md`.

## Pendente no marco A

- T5: teste de boot automático (contar `PRESENT` ≥ 300 quadros) no oráculo.
- Avisos `primitive restart` do MoltenVK (milhares por execução): inofensivos até aqui.
- Validar menu e controles; testar uma música.

## Marco C — primeiro boot no Switch (mesmo dia, 18:20)

- NRO (`build/switch/gh3.nro`, 57 MB) + `game/` copiados para `sdmc:/switch/gh3-nx/`, aberto em title takeover num Switch Mariko.
- Resultado (relatado): roda inteiro, sem bugs além do brilho excessivo; abaixo de ~20 FPS nos clocks de fábrica, jogável com OC.
- Mac, renderer nativo: 38–44 FPS na gameplay (log), contra ~5 do emulado.
- Pendente: medir FPS no console pelo log (`logs/` ao lado do NRO, linhas `[native] last 10 s ... swaps`), consertar o brilho (T9), marco D (desempenho).

## Marco D (noite de 17 → 18)

- Linha de base no Switch tirada dos logs via FTP do DBI (sem cabo): fábrica/1 jogador ~27–35 swaps/s; GPU e CPU do
  renderer acima de 16,7 ms → GPU (T14, T15) entra na frente da fila.
- T10 (medidor), T11 (registradores locais) e T12 (chamadas diretas + LTO) feitos. T9 (brilho) adiado pelo Guillerme.
- Harness trocado para Sonnet 5.5 para economizar cota.
- App renomeado para "Guitar Hero III: Legends of Rock" com ícone do próprio jogo (gerado localmente).
- Repositório público no GitHub criado (só código GPL; checagem de conteúdo antes do push).

## Marco D, madrugada de 18 — diagnóstico no Switch

- Harness: T13 (esperas dormem), T14 (GPU 460,8 MHz, confirmado no log do aparelho) e T15 (palco 1 amostra) ✅.
  Pausas por limite de uso do Claude; nesse intervalo o Guillerme tocou o projeto no OpenCode com DeepSeek **na
  mesma worktree**, e o trabalho dele (PGO, overlay de FPS, `vblank_late_no_wait`, ZCULL) entrou de carona nos
  commits do harness. Os experimentos restantes foram commitados à parte com a origem escrita (`df57e51`).
- DeepSeek mediu no Switch: PGO, ZCULL e `vblank_late_no_wait` sem ganho. O corte de sombra + bloom deu FPS
  mediano **34 → 45** nos clocks de fábrica (quadros >50 ms por 10 s: 46–108 → 8).
- Perfil simbolizado (ELF guardado do mesmo NRO): threads majoritariamente **esperando** em syscall; o jogo é
  limitado pela **GPU**, não pela CPU — por isso PGO/T11/T12 não aparecem. Detalhes no playbook §8.
- Sem o bloom some o esbranquiçado (T9). O Guillerme aprovou o visual: modo desempenho ligado por padrão no
  `switch/gh3.toml` (`053423d`).
- Próxima fila (pedida pelo Guillerme, itens 3–5): 3D em resolução menor que a HUD; apresentação sem esperar a GPU
  (command buffers em rodízio); funções quentes do jogo em C++ nativo.

## 2026-10-19 — pesquisa de render, T24 (alvos 2048) e fallback verde

- **Pesquisa de portes** (5 frentes: refs nfsmw/project8, ReXGlue/Xenia, portes NFS, GTA/RAGE, Tegra/NVK):
  relatório em `docs/render-research-2026-10-19.md`. O 360 vence por **eDRAM (~256 GB/s)** contra os
  **~21,3 GB/s** do Tegra X1 handheld, não por FLOPs (~236 vs ~240 GFLOPS): o gargalo é sombrear
  fragmentos/overdraw/passes de tela cheia. Próximas alavancas: dynamic resolution 0.5–0.75 + CAS,
  shaders/NAK, resolve sem cópia (com restauração), present ring e overdraw exato. ZCULL, late-no-wait,
  FP16/VRS, AFBC, depth prepass, occlusion queries, FSR2 e 4º work slot já se mostraram becos sem saída.
- **T24 (achado):** o alvo de sombra/efeito era criado com **2048 linhas** (4 draws com scissor default,
  4–6 ms) — a mesma inflação do alvo principal. O cap agora usa a altura real do alvo 1x
  (`known_1x_height` + `edram::AliasHeight`) no render area; capturas no Mac OK; medir no Switch.
- **Piscadas verdes:** o fallback do renderer nativo limpava o guest output com a cor de teste verde
  (`gh3_native_system.cpp`); trocado por preto. Se persistir, os suspeitos são present/ownership/formato
  (ReXGlue #508/#513, ProjectTM), não o shading.
- **Fila:** T16 implementação correta (menu 0,75 vs 1,0 mean_diff 0,7), mas o aceite de gameplay é não
  determinístico (marca wall-clock vs animação por frame); T17 bloqueada (o fence único é o fallback);
  **T18 aceita** (espera do anel da GPU dorme; `installed 3`); T19 bloqueada (1862 linhas, GPU-bound);
  **T20 aceita** (NRO + pacote); T21–T23 bloqueadas (SDK sem spirv-tools; NAK sem FP16).
- **Suíte:** 163 testes; `check_no_game_content` limpo.
