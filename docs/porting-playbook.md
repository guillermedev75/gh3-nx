---
created: 2026-10-17
updated: 2026-10-17
---

# Playbook de port 360 → (Mac | Switch) com ReXGlue

Lições do gh3-nx que valem para o próximo jogo. Cada item: sintoma → causa → o que fazer (e a ferramenta
deste repo que já resolve). Atualizar sempre que algo novo for identificado.

## 1. Build

| Sintoma | Causa | Ação |
|---|---|---|
| Cada rebuild leva 6–11 min | `<proj>_register.cpp` = 1 função com ~60 mil `SetFunction`; `-O1/-O0` não ajudam (medido) | `tools/split_register.py` como `POST_BUILD` do `<proj>_codegen` → **7,5 s** |
| `-O0` num arquivo dá erro de PCH (`__OPTIMIZE__`) | flags por arquivo divergem do PCH | `SKIP_PRECOMPILE_HEADERS ON` no arquivo (ou não mexer em flags; preferir o split) |
| Editar arquivo gerado some no build | o passo de codegen do build regenera quando o stamp não bate | alterar gerados só dentro do build (POST_BUILD) |

## 2. Boot no Mac (GPU emulada)

| Sintoma | Causa | Ação |
|---|---|---|
| Tela preta, sem som, CPU ocupada | ReXGlue 0.10 tirou a GPU do núcleo; `gpu_plugin` vazio | `rexglue_setup_target(<proj> GPU_PLUGINS xenos)` + `--gpu_plugin xenos` |
| Jogo roda com som, janela preta, F4 não abre | modo immediate + tela cheia trava em `CAMetalLayer nextDrawable` (MoltenVK) | janela + FIFO: `--fullscreen=false --vulkan_allow_present_mode_immediate=false --vulkan_allow_present_mode_mailbox=false` |
| Janela fica preta ao redimensionar | recriar o swapchain trava em `nextDrawable` (mesma causa do immediate); `host_present_from_non_ui_thread=true` não resolve (testado) | não redimensionar; correção real exige mexer no presenter do runtime |
| Milhares de `primitive restart` no log | limitação do Metal | inofensivo até agora |
| Teclado não controla | `mnk_mode` desligado por padrão | `--mnk_mode=true` + `--keybind_*` (ver `run-gh3.command`) |

## 3. Quedas `Call to invalid or unregistered function at guest address X`

Causa: funções minúsculas (2–18 instruções) chamadas só por ponteiro (vtable/callback), no buraco entre duas
funções registradas. A análise estática do ReXGlue não as vê.

Fluxo, do mais barato ao mais caro:

1. **Varredura estática** (faz quase tudo de uma vez):
   ```
   GH3_DUMP_IMAGE=out/image.bin ./run-gh3.command          # grava a imagem descriptografada (hook em src/gh3_app.h)
   python3 tools/find_missing_funcs.py out/image.bin --toml >> config/gh3_functions.toml
   cmake --build out/build/mac-arm64-release > out/build.log
   python3 tools/find_missing_funcs.py --prune out/build.log  # tira candidatos que partiram funções
   cmake --build out/build/mac-arm64-release
   ```
   Validação: das 10 funções achadas na marra, a varredura acha 9 (todas apontadas por ponteiro).
   No GH3: 106 candidatos → 15 falsos positivos removidos pelo `--prune` → 91 registrados.
   **Não use `--all` em massa.** Registrar todos os buracos sem ponteiro (1226 no GH3) passou no build, mas o
   boot caiu de ~3700 para 19 quadros em 60 s: cauda de função virando função separada muda o fluxo. Use os
   buracos sem ponteiro só para conferir um endereço específico que caiu em runtime (ex.: 0x825E9670, que o jogo
   monta com lis/addi — nenhum ponteiro na imagem) e registre só ele.
   O `--prune` também tira candidatos que viram símbolo indefinido no link (ex.: dados no fim da seção de código).
2. **Laço automático no boot**: `tools/discover_functions.sh` abre o jogo, registra o endereço que caiu,
   recompila e repete até rodar 60 s limpo. Também serve de teste de regressão do boot.
3. **Manual (gameplay)**: o laço não passa do menu. Quando cair jogando: `tools/fix_last_crash.sh` pega o
   endereço em `out/last-run.log`, registra, recompila, roda o teste de boot e pronto (~2 min).

## 4. Medição

- Contar quadros: `grep -c PRESENT out/last-run.log` com `--log_level debug`.
- Toda mudança de build/boot entra no `registro-de-impacto` com antes/depois.

## 5. Desempenho (ideias registradas)

- Upscaling tipo DLSS/FSR (renderizar menor e ampliar) só ajuda se o gargalo for a GPU desenhando pixels. No Mac
  com GPU emulada o custo principal tende a ser CPU (tradução de comandos Xenos → Vulkan → Metal); medir antes.
- Geração de quadros (Lossless Scaling, FSR 3 FG) adiciona latência: fora para jogo de ritmo; opcional nos outros.
