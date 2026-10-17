---
created: 2026-10-17
updated: 2026-10-17
status: aprovado (Guillerme, 2026-10-17: "sim, por favor, vamos fazer essa parada acontecer")
---

# gh3-nx — Guitar Hero III (Xbox 360) nativo no Nintendo Switch

Port não oficial do Guitar Hero III: Legends of Rock (Xbox 360, USA Rev 1) para o Switch desbloqueado,
por recompilação estática com ReXGlue, no mesmo caminho dos ports 360 → Switch de 2026.

## Contexto (da investigação de 2026-10-17)

- Todos os ports 360 → Switch de 2026 usam a mesma receita: **ReXGlue** (PowerPC → C++ → ARM64 com
  devkitPro), **renderer Vulkan nativo sobre NVK** (mesa-switch) em vez de emular a GPU do 360 (que dá 1–5
  FPS), shaders traduzidos antes (XenosRecomp) com cache no SD, execução em title takeover e espaço de
  endereço de 39 bits.
- **Base mais próxima: `R-drg/project8-sw`** (Tony Hawk's Project 8, motor Neversoft — mesma família do
  GH3), GPL-3.0, com o renderer do `StevensND/nfsmw-nx` adaptado ao motor e a camada Horizon do SDK em
  `patches/rexglue-sdk/`.
- **Escada de desempenho de referência**: `nfsmw-nx/docs/performance-history.md` (1–3 → 35 FPS: renderer
  nativo; cortes de GPU; custo por draw; GPU 460,8 MHz via apm; chamadas diretas + LTO + PGO + ordenação de
  funções; funções quentes nativas com guarda).
- `luisxl15/Guitar-Hero-3-Recomp-` já recompila o GH3 para PC/Android, mas **não declara licença**: serve só
  de referência (manifest: USA Rev 1, XGD2, title id `0x415607F7`, base `0x82000000`, entry `0x82136AC8`,
  ReXGlue 0.9.0). Nenhum código dele entra aqui sem permissão do autor.

## Licença

GPL-3.0 (exigida por reaproveitar project8-sw / nfsmw-nx). Créditos explícitos no README.

## Regras de dados

- Nenhum arquivo do jogo no repositório: `extracted/` (arquivos do disco) e `generated/` (C++ gerado a
  partir do executável do jogo, derivado de código com direitos autorais) ficam no `.gitignore`.
- O jogo vem do disco do próprio Guillerme. Ninguém baixa ROM/ISO para este projeto.

## Metas

| Marco | Entrega | Onde | Verificação |
|---|---|---|---|
| **A — Mac, GPU emulada** | Codegen do `default.xex` sem chamadas não resolvidas; binário macOS (arm64) que abre o jogo até o menu com o runtime do ReXGlue (Vulkan/MoltenVK) | Mac | Automática: codegen limpo, build, teste de boot que conta quadros apresentados |
| **B — Mac, renderer nativo** | Renderer nativo portado do project8-sw substituindo a GPU emulada | Mac | Boot test + capturas comparadas à mão |
| **C — Switch** | NRO (devkitA64 + libnx + NVK) que abre o jogo em title takeover | Switch | Build automático; execução manual no console |
| **D — Desempenho** | 60 FPS estáveis numa música, sem overclock; latência de áudio calibrável | Switch | Medido no console (Horizon OC Monitor), percentis de frame time |

Cada marco ganha seu próprio plano depois do anterior. Este documento só detalha o marco A no plano
`docs/superpowers/plans/2026-10-17-gh3-nx-marco-a-mac.md`.

## Requisitos específicos de jogo de ritmo (marco D)

- Alvo de **60 FPS** (o GH3 rodava a 60 no 360; o sa2-nx prova que jogos leves chegam a 60 sem overclock).
- **Ritmo de quadro estável** > FPS médio: olhar quadros acima de 16,7 ms, não a média.
- **Áudio em thread própria** com buffer curto; manter a calibragem de atraso de áudio/vídeo do próprio jogo.
- **Cache de shaders gerado inteiro na primeira execução** (palcos são poucos), para não travar no meio de
  uma música.

## Riscos

- ReXGlue 0.9.0 → 0.10.0: o manifest de referência pode precisar de ajuste.
- NVK no Switch é recente; bugs de driver podem exigir contornar shaders (o LibertyRecompAndroid pulou um).
- Sem o disco do GH3 de 360 copiado e sem Switch desbloqueado, o marco A para no codegen e os marcos C/D
  não são testáveis.

## Critério de sucesso final

GH3 abre pelo HOME do Switch (forwarder) e uma música inteira toca a 60 FPS estáveis em modo portátil, sem
overclock, com áudio sincronizado.
