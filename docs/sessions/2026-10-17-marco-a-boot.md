---
created: 2026-10-17
updated: 2026-10-17
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

## Pendente no marco A

- T5: teste de boot automático (contar `PRESENT` ≥ 300 quadros) no oráculo.
- Avisos `primitive restart` do MoltenVK (milhares por execução): inofensivos até aqui.
- Validar menu e controles; testar uma música.
