---
created: 2026-10-17
updated: 2026-10-18
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
| SDK pronto (zip 0.10.0) não tem camada Horizon/renderer nativo | as mudanças do project8-sw são patches sobre o código-fonte | `tools/setup_sdk.sh` → `sdk/` = `f5337cd` + 16 patches; `--check` compara a árvore exata. Ordem do README da série: aplicar com `--index` **antes** do `submodule update` (o 0002 move o gitlink do MoltenVK) |
| Checkout novo sem `generated/` não configura (o `CMakeLists.txt` inclui `generated/rexglue.cmake`, que só o codegen escreve) | o glue é saída do `rexglue codegen` | `tools/render_glue.py` escreve o glue do template do `sdk/`; o build roda o codegen (`<proj>_codegen`) e regenera; configurar de novo depois do codegen para entrar o `sources.cmake`. Tudo em `tools/build_host_sdk.sh` |
| Build sobre o SDK patcheado | — | `tools/build_host_sdk.sh` → SDK em `out/sdk-install` (887 passos ninja), preset `mac-arm64-p8sdk`, `GH3_BUILD_DIR=out/build/mac-arm64-p8sdk ./run-gh3.command`. Codegen do rexglue patcheado: 291 arquivos em 53 s, 13 avisos `Unresolved` (desvios em 0x8211–0x8272), 59 575 endereços registrados; 300 quadros em ~13 s de boot (medido, 2026-10-17). O `cmake --install` do SDK registra `rexglue` no *user package registry* (`~/.cmake/packages/rexglue`) — um `find_package(rexglue)` sem `rexglue_DIR` pode cair nele |
| Driver NVK para o Switch | Vulkan no Switch = NVK do mesa-switch (`d4a00ea0ab3`) + 4 patches do project8-sw, linkado no NRO | `tools/build_mesa.sh` → `out/driver-sdk/lib/{libvulkan.a,libnak_rs.a}` (19 libs juntas por script MRI do `aarch64-none-elf-ar`, conferidas e indexadas pelo `stage-mesa-archive.py`: 2892 objetos AArch64); `--check` confere o `vkCreateInstance`. Docker amd64 emulado no M1: 1ª execução 1021 s até a falha abaixo (imagens + build parcial); com imagens em cache, build inteiro (1234 passos) + junção em **364 s** (medido, 2026-10-17) |
| `error[E0463]: can't find crate for std` em `libcompiler.rlib` (alvo `aarch64-unknown-linux-gnu`) | o `Docker.rust` do mesa-switch só instala o std do host; num host Linux arm64 ele vem de graça, num amd64 não | `docker/switch/mesa-rust.Dockerfile` (`rustup target add`) por cima da imagem; o `build_mesa.sh` monta antes de chamar o `build-switch-p8.sh` |
| NRO do Switch | `cmake/switch.cmake` (do project8-sw) com `-DGH3_SWITCH=ON`: o `CMakeLists.txt` faz `return()` antes do `generated/rexglue.cmake`, porque o codegen é do host e o container só compila o `generated/default/` que já existe | `docker run --platform linux/amd64 -v <repo>:/project -w /project gh3-switch-clang sh -c 'cmake -S . -B build/switch -G Ninja -DCMAKE_BUILD_TYPE=Release -DGH3_SWITCH=ON -DCMAKE_TOOLCHAIN_FILE=/project/cmake/switch-rexglue-gcc.cmake -DREXSDK_DIR=/project/sdk -DREXGLUE_SWITCH_NVK_SDK=/project/out/driver-sdk && cmake --build build/switch -j 6'` → `build/switch/gh3.nro` (56,8 MiB; ELF sem strip `gh3` 84,8 MiB). 647 passos em **512 s** com `-j 6`, Docker com 10 CPUs/7,7 GiB (medido, 2026-10-17); `python3 tools/check_nro.py` confere magia, tamanho e asset section |
| `vkCreateInstance` duplicado no `libvulkan.a` (T4) | `libvulkan.a` e `libnvk.a` do mesa trazem os mesmos objetos | não quebrou o link: o toolchain usa `-Wl,--allow-multiple-definition` (o linker fica com a 1ª definição). Só aparece no console se as cópias divergirem |
| `nm … \| grep -q` falha num script com `set -o pipefail` mesmo com o símbolo presente | o `-q` sai no 1º acerto e o `nm` morre de SIGPIPE | `grep … >/dev/null` |
| `default.xex` errado ou ausente vira abort no teardown (parece bug do port) | o `generated/` vale para um único executável | trava em `OnConfigurePaths` (`src/dump_gate.h`, do project8-sw): SHA-256 contra `src/supported_dumps.h` (`python3 tools/gen_supported_dumps.py`, só hash/tamanho/title id; `--check` acusa cabeçalho velho), recusa com exit 3 e mensagem, sem flag para desligar. No Switch, sem argv, a raiz é `game/` ao lado do NRO. O log do runtime ainda não existe nesse gancho: a mensagem só sai no stderr |
| Pacote do SD | — | `tools/make_sd_package.sh [--nro build/switch/gh3.nro] [--out out/sd]` → `out/sd/switch/gh3-nx/{gh3-nx.nro,gh3.toml,LICENSES.txt}` (pasta recriada do zero, NRO validado). `tools/check_no_game_content.sh [DIR]` (do project8-sw, reescrito sem `mapfile` para o bash 3.2 do macOS) confere nomes do disco (`*.xex`, `*.xen`, `DATA/`, `generated/`…) e cabeçalho `XEX2` por conteúdo; sem DIR, os arquivos publicáveis do repo (+ limite de 8 MB) |

## 2. Boot no Mac (GPU emulada)

| Sintoma | Causa | Ação |
|---|---|---|
| Tela preta, sem som, CPU ocupada | ReXGlue 0.10 tirou a GPU do núcleo; `gpu_plugin` vazio | `rexglue_setup_target(<proj> GPU_PLUGINS xenos)` + `--gpu_plugin xenos` |
| Jogo roda com som, janela preta, F4 não abre | modo immediate + tela cheia trava em `CAMetalLayer nextDrawable` (MoltenVK) | janela + FIFO: `--fullscreen=false --vulkan_allow_present_mode_immediate=false --vulkan_allow_present_mode_mailbox=false` |
| Janela fica preta ao redimensionar | recriar o swapchain trava em `nextDrawable` (mesma causa do immediate); `host_present_from_non_ui_thread=true` não resolve (testado) | não redimensionar; correção real exige mexer no presenter do runtime |
| Milhares de `primitive restart` no log | limitação do Metal | inofensivo até agora |
| Teclado não controla | `mnk_mode` desligado por padrão | `--mnk_mode=true` + `--keybind_*` (ver `run-gh3.command`) |
| Renderer nativo do project8-sw (`src/native/`) não linka no Mac: `RegisterFile`, `Shader::AnalyzeUcode`, `draw_util::*` indefinidos | no desktop o código de GPU do SDK fica no `librexgpu-xenos.dylib` com `CXX_VISIBILITY_PRESET hidden`; o project8-sw só compila o nativo no Switch, onde o plugin é estático | `GH3_NATIVE_RENDERER=ON` (preset `mac-arm64-p8sdk`) compila o plugin inteiro dentro do executável (`gh3_gpu_xenos`, como o SDK faz no Switch) e o `Gh3App` cria o xenos por `rex_gpu_create` em vez de abrir o .dylib, porque duas cópias registram os mesmos cvars e a 2ª é ignorada. `--gh3_renderer=native`: 300 quadros (`XELOG_GPU PRESENT native`) em < 180 s; GPU emulada no mesmo build continua passando (medido, 2026-10-17) |

## 3. Quedas `Call to invalid or unregistered function at guest address X`

Causa: funções minúsculas (2–18 instruções) chamadas só por ponteiro (vtable/callback), no buraco entre duas
funções registradas. A análise estática do ReXGlue não as vê.

Fluxo, do mais barato ao mais caro:

1. **Varredura estática** (faz quase tudo de uma vez):
   ```
   GH3_DUMP_IMAGE=out/image.bin ./run-gh3.command          # grava a imagem descriptografada (hook em src/gh3_app.h)
   python3 tools/find_missing_funcs.py out/image.bin --toml >> config/gh3_functions.toml
   cmake --build out/build/mac-arm64-p8sdk > out/build.log
   python3 tools/find_missing_funcs.py --prune out/build.log  # tira candidatos que partiram funções
   cmake --build out/build/mac-arm64-p8sdk
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
- **FPS da gameplay (Mac, `tools/gameplay_fps.py`)**: roda o roteiro com `--gh3_renderer=native` até a marca
  `gameplay` e lê as linhas `[native] last 10 s: ... draws ... swaps` e `per frame: ... waits for the game, N ms
  waiting`. Descarta o 1º bloco de 10 s depois da marca (pega o trecho anterior) e tira a média dos `--seconds/10`
  seguintes. Grava `swaps_per_s`, `draws_per_s`, `wait_ms` e `game_fps` = `draws_per_s` / mediana de `per frame:
  N draws` (estimativa: não depende de o swap se repetir quando o jogo atrasa). `--min-vs base.json` sai 1 se
  `game_fps` cair mais de 5%. Linha de base (2026-10-17, Mac, nativo, 30 s): `out/perf/baseline.json` — game_fps
  53,0, swaps/s 46,5, 19386 draws/s, 7,0 ms de espera. Uma corrida leva ~2 min.
- **Ruído do `game_fps` é grande** (mesmo build, 30 s): 55,6 / 56,8 / 64,8 / 66,2 / 69,1; as piores corridas foram logo
  depois de compilar. Um `--min-vs` vermelho isolado não prova regressão: repetir após a máquina esfriar.
- **Registradores como locais (T11)**: as opções do codegen (`ctr_as_local`, `xer_as_local`, `cr_as_local`,
  `non_volatile_as_local`) valem no arquivo `includes` (`config/gh3_functions.toml`), no topo, antes de `[functions]`;
  no manifesto são ignoradas. Ligadas ctr/xer/cr: `ctx.xer|ctr` 155854 → 0 nos `gh3_recomp.*.cpp`; gameplay ok;
  game_fps 64,8–66,2 (baseline 66,4; sem ganho mensurável acima do ruído). `non_volatile_as_local = true` derruba o
  jogo (`write of guest 0x00000084`, 16 PRESENT): funções partidas leem registradores antes de escrever; precisa do
  `share_registers` + varredura "lê antes" do nfsmw (`tools/lee_antes.py`) antes de ligar. Rebuild: apagar
  `cmake_pch.hxx.pch` antes, senão o PCH fica velho.

## 6. Revisão do marco C (2026-10-17)

- O preset do SDK 0.10.0 pronto (`mac-arm64-release`) foi aposentado: a trava do `default.xex` precisa de
  `rex/crypto/sha256.h`, que só o SDK patcheado tem. `src/gh3_app.h` dá `#error` claro; tudo usa `mac-arm64-p8sdk`.
- `check_no_game_content.sh` tinha brechas (pasta `$SYSTEMUPDATE`, `DIR/` com barra pulando a checagem por
  conteúdo, maiúsculas, contêineres STFS/GOD `CON `/`LIVE`/`PIRS`, symlinks): corrigidas, com teste para cada uma.
- O pacote do SD é só para uso pessoal (o NRO tem o `generated/` compilado).

## 7. Marco D — otimizações (2026-10-17/18)

| Item | Resultado | Como |
|---|---|---|
| Medidor de FPS da gameplay no Mac | linha de base nativo: 53 swaps/s, ~342 draws/quadro; ruído de corrida ~±10% (3 corridas do mesmo build: 55,6/66,2/64,8 de game_fps) | `tools/gameplay_fps.py` (roteiro até a marca `gameplay`, lê `[native] last 10 s`) |
| Registradores como locais (`ctr/xer/cr_as_local`) | 155.854 usos de `ctx.xer/ctr` → 0; sem ganho acima do ruído no Mac | as opções vão no topo de `config/gh3_functions.toml` (no manifesto são ignoradas) |
| `non_volatile_as_local` | derruba o jogo (violação em 0x84) | funções partidas leem registradores antes de escrever: precisa do `share_registers` + `lee_antes.py` do nfsmw-nx |
| Chamadas diretas + LTO no NRO | 88.007 `sub_X(ctx, base)` → `__imp__sub_X` em 142 arquivos; NRO com `-flto=6`, `_start` em 0 | `tools/llamadas_diretas.py` no fim do `tools/codegen.sh`; `cmake/switch.cmake` |
| Linha de base no Switch (fábrica, 1 jogador) | ~27–35 swaps/s, ~230 draws/quadro; CPU do renderer 22–28 ms e GPU ~31–36 ms reais por quadro | log `logs/gh3_*.log` (FTP do DBI); GPU do driver × 1,627 |
| Palco com 1 amostra por pixel (T15) | palco 4x de pitch P desenhado em 1 amostra (640x1024 em vez de 1280x1328 de profundidade), `white=0,001` no Mac; ganho de GPU só medível no Switch | `gh3_native_msaa_as_samples=false` (agora no `switch/gh3.toml`) |

Lições do harness nesta fase:
- Conferir trabalho de uma sessão com `git status --short`/`git diff --cached`, não só `git diff`: o agente deixa
  tudo no índice, e um `reset --hard` apaga (recuperável com `git fsck --lost-found`, os blobs ficam soltos).
- Sessão que termina e fica ociosa só cai pelo `ITER_TIMEOUT_SEC`; correção proposta no próprio harness.
- Padrão anti-skip `xit(` casa com `sys.exit(` em Python: usar padrões da linguagem do projeto.

App no Switch: título "Guitar Hero III: Legends of Rock"; ícone recortado do menu principal do próprio jogo por
`tools/make_icon.py` → `out/switch/icon.jpg` (fora do git, arte da publisher).

### Palco 1x sem o branco: alias 1 amostra → alvo 1x (T15)

- Sintoma: `--gh3_native_msaa_as_samples=false` com 1 amostra (`gh3_native_msaa=false`) → palco branco
  (`white=0,77`). Causa: o GH3 desenha o palco como superfície 4x de pitch P (640x360 px × 4 amostras) e depois
  lê a mesma EDRAM como alvo 1x de pitch 2P (1280x720); com chaves por pitch são duas imagens, e a 1x nunca
  recebia o palco (só as luzes aditivas, que somavam até branco).
- Conserto: a chave do alvo ganhou `alias_msaa` (amostras do guest da superfície desenhada com 1 amostra;
  `AliasMsaa()`); cada uso dessa imagem incrementa `alias_seq`; ao usar um alvo 1x (draw, resolve, clear)
  `SyncAliasTarget` faz um `vkCmdBlitImage` NEAREST 2x2 da imagem 1 amostra de pitch/2 na mesma base, se ela foi
  usada depois do último sync (cor e profundidade). Região por `edram::AliasBlitExtent` (testada, sem GPU).
- Armadilhas: o sync tem de rodar em `Draw()` e não só em `GetRenderTarget`, porque setups em cache não
  passam por `GetRenderTarget`; `gh3_native_msaa` agora é `false` por padrão (com `true` e samples-as-pixels
  desligado não há alias e o palco sai branco). Sombra do palco fica em 640x360 de sombreamento, com
  bordas em 1 amostra (sem a grade 2x2 de amostras do guest).
- `gameplay_capture.py`/`gameplay_fps.py` aceitam cvars extras depois de `--` (`-- --cvar=valor`).

## 8. Diagnóstico no Switch: o gargalo é a GPU (2026-10-18)

Medido no aparelho (Mariko, portátil, CPU 1020 MHz / GPU 460,8 MHz / RAM 1331 MHz), logs puxados pelo FTP do
DBI (`ftp://<ip>:5000/switch/gh3-nx/logs/`): `gh3_NNN.log` (renderer) e `rex/rex_perfil.log` (profiler do SDK
do project8-sw: FPS a cada 10 s, CPU por thread com amostragem de pilha, chamadas libnx, relógios).

| Mudança | FPS mediano do jogo | Renderer CPU / GPU por quadro (gameplay) | Veredito |
|---|---|---|---|
| base (T13+T14) | ~34 | 21–39 / 17–29 ms | — |
| PGO (`GH3_PGO=gerar` → jogar → `usar`) | ~31 | ~25 / ~21 ms | sem ganho visível (o gargalo não é CPU) |
| ZCULL (`gh3_native_zcull` + patch Mesa 0005) | ~22 | 33–41 / 25–30 ms | pior ou ruído; desligado |
| `gh3_vblank_late_no_wait` | — | — | sem ganho (quadros lentos não são quantização de vblank); desligado |
| **sem sombra e bloom** (`gh3_native_skip_shadows/bloom`) | **~45** | **13 / 13 ms**; quadros >50 ms por 10 s 46–108 → 8 | **ligado no `switch/gh3.toml`** |

Como ler o profiler sem se enganar:
- O "CPU %" por thread conta amostras paradas em syscall de espera. Simbolizado, quase todo o topo era
  `svcSleepThread`, `svcWaitProcessWideKeyAtomic`, `svcWaitSynchronization`, não código do jogo. Os threads
  estão **esperando**, não calculando. As funções do jogo mais quentes (`sub_824D0070`, `sub_8272C7C0`) ficam em
  ~13–18% de um núcleo.
- `fences esperadas (… ms)` por segundo é o sinal de GPU: 505–656 ms/s na base, 341 ms/s sem sombra e bloom.
- "renderer CPU" do `gh3_*.log` inclui espera; ele caiu junto com a GPU quando só passes de GPU foram cortados.
- Sinais externos que batem: o OC de **GPU** melhora até a intro, que é leve (3,6 ms de CPU e 5,7 ms de GPU por
  quadro).
- Para simbolizar, **guarde o ELF de cada NRO testado** (`build/switch/gh3` → `out/elf/gh3-<sha>.elf`; o NRO
  não tem símbolos). Offsets `imagen+0x…` = endereço no ELF; `llvm-nm -n` + bisect. Com ELF de outro build os
  nomes saem errados (aparecem `tmemCreate`/`ProfilerMain` em thread do jogo).
- Compare corridas na **mesma música e no mesmo trecho**: dentro de uma corrida o FPS de 10 s varia de 19 a 49.

GPU por passe (`gh3_native_gpu_profile=true`, ms por quadro): passe principal 720p cor+profundidade 7–11 ms,
segundo passe 720p 3–5 ms, resolves ~1,5–2 ms; sombra e bloom juntos ~5–10 ms (pelo ganho do corte).

O esbranquiçado da cena no renderer nativo (T9, bloqueado) **some sem o bloom**: o passe de bloom (alvo base 1440,
pitch 320, 320x184) é o que clareava demais. Consertar o bloom é opcional; o modo desempenho já resolve.

Outras peças desta fase:
- T13 `src/guest_waits.h`: esperas no contador de swap/vblank dormem após N leituras iguais (só no Switch).
- T14 `src/switch_apm.h`: `apm performance configuration 0x92220008 (anterior 0x00020003)` confirmado no log.
- PGO `src/pgo.cpp` + `cmake/switch.cmake` (do nfsmw-nx): o build de gravação desvia os `.gcda` para
  `sdmc:/switch/gh3-nx/pgo/` com `--wrap=fopen` e grava a cada 30 s; `-fno-profile-values` porque o perfil de
  chamadas indiretas lê `tpidr_el0`, que é 0 no Horizon. Os perfis ficam em `out/pgo/` (fora do git).
- Overlay de FPS do SDK: `src/frame_stats.h` registra o provider; F3 no Mac, L+R+Cima no Switch.

Lição de processo: **dois agentes na mesma worktree se misturam.** O OpenCode/DeepSeek trabalhou em paralelo ao
harness, e o `git add -A` do harness levou o trabalho dele para dentro dos commits T13/T14/T15 (PGO no commit do
T14, `vblank_late_no_wait` no do T15). Um agente por worktree; o outro usa a sua.

### Pesquisa de portes (2026-10-19)

Relatório completo em `docs/render-research-2026-10-19.md` (5 frentes, prioridades P0–P3, com fontes).
Resumo acionável:

- O 360 tem ~256 GB/s de eDRAM; o Tegra X1 handheld ~21,3 GB/s. O gargalo é **fragment shading/overdraw/
  passes de tela cheia**, não FLOPs (~236 vs ~240 GFLOPS). Emular a eDRAM é o que derruba o backend Xenos.
- Próximas alavancas: dynamic resolution 0.5–0.75 com histerese + CAS; shaders/NAK (`spirv-opt`,
  `max(a,a)`, `textureSize`); resolve sem cópia só com restauração e paridade; present ring (3 slots, 1
  submit/frame — o renderer já tem 4 **work** slots, não confundir); overdraw exato (blend 0×/1×, alpha
  NEVER, `EarlyFragmentTests` só sem Z-write).
- Becos sem saída medidos/documentados: ZCULL sem o pacote completo, `vblank late-no-wait` como FPS,
  baixar resolução sozinho, FP16/VRS (NAK/GM20B), AFBC, depth prepass, occlusion queries, FSR2, 4º work
  slot, thread de apresentação, EDS 1/2/3, sombra a cada N frames.
- Correção antes de otimização: instrumente swap/RT key/clear/resolve/fence; os flashes de cor do ReXGlue
  (#508/#513) e do ProjectTM vêm de ownership/formato/present, não de shader.
- **T24:** alvos pequenos (sombra/efeito) eram criados com 2048 linhas por scissor default; o cap pela
  altura real do alvo 1x (`known_1x_height`/`AliasHeight`) reduz o render area sem mudar a imagem.
- **Fallback verde** do guest output trocado por preto (piscadas entre telas).


### F1.3 — overdraw exato (2026-10-19)

O GH3 tem muitos draws que não mudam nenhum pixel: blend `0×src + 1×dst` (ADD) em todos os canais
escritos, sem escrita de Z/stencil. O `Draw` agora os pula (`src/native/native_invisible.h`, decisão pura
testada). Na captura local a quente: **53.248 draws pulados** (~17/quadro a ~35 fps), imagem idêntica
(white=0,001, stddev=50,0). Alpha test NEVER idem; desenhos que escrevem Z/stencil não são pulados.
No Mac o ganho de GPU é ~0 (não é o limite); o ganho real (CPU do ring + blend) é medido no Switch (F5).

### F0.1 — protocolo A/B (2026-10-20)

`tools/gameplay_fps.py --runs 3` repete a corrida na mesma música/trecho (o roteiro prende o início no mesmo
ponto) e agrega por mediana. Regras para comparar builds:

- `game_fps` no topo = **mediana** das corridas (com `--runs`); `run_results` guarda cada corrida, para não
  esconder uma corrida ruim.
- **p1 (1% low)** = `1000/p99` do frame time, sobre os `XELOG_GPU PRESENT native` de **todas** as corridas
  (não é a mediana de 3 p1s). `p99_ms` usa posto mais próximo; `pct_over_50ms` conta os quadros >50 ms.
- O frame time vem do timestamp de cada PRESENT (resolução de 1 ms), no mesmo trecho dos blocos de 10 s: o 1º
  bloco depois da marca é descartado. Sem PRESENTs no log (sem `--log_level debug`), `%>50 ms` cai para o
  contador `slow frames: N over 50 ms` do relatório por bloco.
- No Switch o timestamp é o do próprio log; o timestamp de GPU do NVK no profiler continua precisando de
  ×1,627 (playbook §8, tabela). Não misture as duas fontes.
- `--min-vs` continua comparando `game_fps` (agora mediana) com a linha de base, tolerância 5%.
- Custo: ~2 min por corrida no Mac (30 s medidos); 3 corridas = ~6 min. Medido em 2026-10-20 (Mac, nativo,
  20 s, 1 corrida, máquina quente): game_fps 63,3, median frame 17 ms (58,8 fps), p1 29,4, p99 34 ms,
  >50 ms 0%, 1021 quadros amostrados.

### F0.2 — log por swap (2026-10-20)

`--gh3_native_swap_log=true` (cvar `gh3_native_swap_log`, padrão false) escreve uma linha
`[native] swap: ...` por swap, formatada por `src/native/native_swap_log.h` (puro, testado sem SDK):

```
swap: frame=4992 fb=0CB68000 1280x720 fmt:37 src=1280x720 clear=-@00000000 depth=-@00000000
      resolve=41 00000000 fmt:37 -> 0CB68000 rt=base:00000000 pitch:1280 fmt:0 samples:1
      color alias:0 fence:10.437ms presented:1
```

- `fb` = endereço apresentado (fetch constant 0, já com swizzle), `fmt`/`src` = VkFormat e tamanho da
  imagem resolvida de onde o present copia.
- `clear`/`depth` = último clear pedido pelo guest no quadro (raw do `RB_COLOR_CLEAR`/`RB_DEPTH_CLEAR`
  e endereço do alvo); `-` = o quadro não fez esse clear pelo caminho de resolve.
- `resolve` = quantos resolves o quadro fez + origem (base do RT, VkFormat) e destino (`RB_COPY_DEST_BASE`)
  do último; `rt` = chave do último passe (base/pitch/formato guest/amostras/tipo); `alias` = `alias_seq`
  do T15 no alvo; `fence` = espera de fence acumulada no quadro (`frame_fence_wait_ns`).
- Custo no Mac (corrida de ~5.000 swaps): 178 testes verdes; captura white=0,014 stddev=55,2. A linha é
  `info` por swap, então ligue só na investigação (F0.3/F3.1), não no pacote do Switch.

### F0.4 — auditoria de formato (2026-10-20)

`--gh3_native_format_log=true` (cvar `gh3_native_format_log`, padrão false) escreve uma linha
`[native] format: ...` **quando um alvo é criado** (uma por alvo, não por quadro), formatada por
`src/native/native_format_log.h` (puro, testado sem SDK; o renderer tem `static_assert` ligando cada
valor de `VkFormat` usado ao enum do Vulkan):

```
format: frame=1234 color base:A0000000 pitch:1280 1280x720 samples:1 guest:6
        vk:VK_FORMAT_R8G8B8A8_UNORM hdr16f:0 gamma:1 clear:color=0 scene:0 alias:0
```

- `vk` = VkFormat real da imagem (colorido ou profundidade), `guest` = enum xenos de origem; `hdr16f`
  marca os alvos `R16G16B16A16_SFLOAT` (candidatos a cortar se não houver HDR de verdade — F1.4),
  `gamma` o `k_8_8_8_8_GAMMA`, `clear` o que o `CreateOrGetRenderTarget` limpa ao criar (cor 0 ou
  profundidade 1.0), `scene`/`alias` o scene scale (T16) e o alias de amostras (T15).
- Captura nativa (10 alvos): cor `R8G8B8A8_UNORM`, profundidade `D32_SFLOAT_S8_UINT` (com
  `gh3_native_depth24` aparece `D24_UNORM_S8_UINT`/`D32_SFLOAT`); nenhum alvo 16F no caminho atual.

### F1.4 — auditoria de formatos (2026-10-20)

`--gh3_native_format_log=true` também liga `src/native/native_format_audit.h` (puro, testado sem SDK):
os alvos criados pelo `CreateOrGetRenderTarget` entram num acumulador e o `Swap` loga uma linha
`[native] format audit:` a cada alvo novo depois do quadro 300 (o boot/menu já criou os seus; alvos da
gameplay entram na conta seguinte). Custou uma linha a cada poucos milhares de quadros, só na investigação.

```
format audit: frame=3833 targets:10 color:4 depth:6 bytes:86343680 memory:82.3MiB
  formats:VK_FORMAT_R8G8B8A8_UNORM=4,VK_FORMAT_D32_SFLOAT_S8_UINT=6
  16f:0 gamma:0 bpp64:0 depth_guest24:6 depth_stored_wide:6 depth24_save:35.5MiB
  expensive:depth24-storage hint:gh3_native_depth24=true ...
```

- `bytes`/`memory` = bytes/pixel do VkFormat × largura × altura × amostras, somado por alvo (é espaço
  de GPU, não tráfego por quadro).
- `16f` conta `R16G16B16A16_SFLOAT`, `gamma` o guest `k_8_8_8_8_GAMMA`, `bpp64` os guests
  `16_16_16_16`/`16_16_16_16_FLOAT`/`32_32_FLOAT` — os candidatos a corte do F1.4.
- `depth_guest24` é a profundidade de 24 bits do guest; `depth_stored_wide` é quantas estão em 8 B/px
  (`D32_SFLOAT_S8_UINT`). `depth24_save` é o que o `gh3_native_depth24=true` devolve (8 → 4 B/px).
- `expensive` só lista o que existe (`none` quando não há nada a cortar), e o `hint` aponta a cvar.

Medido na captura nativa (música Slow Ride, 10 alvos): **nenhum 16F, nenhum gamma e nenhum guest de
64bpp** no caminho atual — não há alvo caro sem HDR para eliminar. O único custo grande é a profundidade:
6 alvos de 24 bits guardados em `D32_SFLOAT_S8_UINT` (~71 MiB, dos quais **35,5 MiB** voltam com
`gh3_native_depth24`), cvar que o `switch/gh3.toml` já liga (Mac fica em 32 bits, para a paridade de
referência). Observação fora do escopo do F1.4: a captura ainda cria um depth `1280x2048` e outro
`1040x2048` (T24 só capa o alias `alias_msaa`, não esses).

### F1.1b — controlador dinâmico da escala da cena (2026-10-20)

`--gh3_native_scene_dynamic=true` (cvar `gh3_native_scene_dynamic`, padrão false no código e true no
`switch/gh3.toml`) usa o intervalo entre swaps como orçamento de quadro e move a escala do T16 entre 0.65
e 1.0 em passos de 0.05 (`src/native/native_scene_dynamic.h`, puro, testado sem SDK):

- Alvo 16.7 ms com margens: acima de 17.7 ms conta para descer, abaixo de 15.2 ms para subir; entre os
  dois é neutro e zera a histerese. Precisa de **3 quadros seguidos** no mesmo lado e **0.6 s no mesmo
  nível** (contados com o tempo real dos swaps) antes de mudar.
- O `gh3_native_scene_scale` vira só o ponto de partida; ele também cai na grade de 0.05.
- Ao mudar a escala, cada alvo `scene_scaled` é recriado no tamanho novo na próxima vez que o jogo o
  pede (`scale_used` no `RenderTarget`); o conteúdo antigo sobe/desce por `vkCmdBlitImage` LINEAR
  (NEAREST na profundidade) e o `SyncSceneTarget` sobrepõe o que o alvo cheio tiver de mais novo.
- Log: uma linha `[native] scene dynamic: enabled 0.65-1.00, target 16.7 ms, ...` ao iniciar e uma
  `[native] scene dynamic: 0.95 -> 0.9 (frame N, 24.5 ms)` por mudança.
- Corrida de verificação no Mac (captura nativa, partindo de 1.0): desceu 1.0 → 0.65 em ~65 s de
  script, cada passo com o tamanho novo (ex. `scene scale 0.65: 832x468`); captura a 0.65
  white=0,001 stddev=53,1. A recriação dos alvos custa um quadro longo (~300 ms no pior caso), então
  no Switch o controlador deve começar no piso (0.65) e só subir se sobrar orçamento.

### F1.1c — CAS no upscale da cena escalada (2026-10-20)

`--gh3_native_scene_cas=true` (cvar `gh3_native_scene_cas`, padrão false no código e true no
`switch/gh3.toml`; `gh3_native_scene_cas_sharpness`, padrão 0.2) troca o `vkCmdBlitImage` LINEAR que sobe
a imagem escalada do T16 pelo **CAS do FidelityFX** que o próprio SDK já usa no presenter:

- O shader é o SPIR-V pré-compilado `guest_output_ffx_cas_resample_ps` (mais o VS de retângulo
  `guest_output_triangle_strip_rect_vs`) de `sdk/src/ui/shaders/vulkan_spirv/`, incluído pelo build do
  Mac e do Switch; nada novo para compilar nem dependência nova.
- A parte pura (`src/native/native_scene_cas.h`, testada sem SDK) calcula o valor pós-setup
  `-1/(8-3*sharpness)`, o retângulo em NDC e a proporção origem/destino; os `static_assert` amarram os
  offsets dos push constants aos do SPIR-V (retângulo 0-15, output_offset 16, ratio 24, sharpness 32).
- Só o **upscale** (imagem escalada → alvo cheio) usa CAS; descer para a imagem escalada, profundidade e
  formato diferente de `R8G8B8A8_UNORM` continuam no blit LINEAR. Se a pipeline falhar, cai no LINEAR.
- Depois do CAS o renderer esquece `bound_pipeline`/`bound_layout`/`bound_viewport`/`bound_scissor`; sem
  isso o próximo draw do jogo acharia que o estado dele já estava ligado e herdaria o do CAS.
- Log: `[native] scene CAS: enabled (sharpness ...)` ao iniciar e
  `[native] scene CAS: upscaling 832x468 -> 1280x720 (ratio 0.65)` na primeira subida.
- Captura no Mac a 0.65: white=0,001 stddev≈50-53 (mesma faixa do LINEAR); o ganho é nitidez, não
  brilho/paridade de cor. Comparação visual e custo de GPU no Switch ficam para a F5.

### F2.1 — SPIRV-Tools/spirv-opt construído no SDK (2026-10-20)

O SDK fixado (f5337cd) não constrói o spirv-tools: `sdk/thirdparty/CMakeLists.txt` só criava o
`spirv-tools-headers` (INTERFACE) e o `libSPIRV.a` instalado é o do **glslang**. O `spvtools::Optimizer`
mora em `SPIRV-Tools-opt`, que depende de `SPIRV-Tools` + `SPIRV-Tools-link`.

- **Patch local**: `patches/rexglue-local/series` (`0001-spirv-tools-opt.patch`) entra depois da série do
  project8-sw e é aplicado pelo `tools/setup_sdk.sh` (setup e `--check`; `--list` continua só a série do
  P8, que os testes fixam). Ele adiciona `add_subdirectory(spirv-tools)` no bloco Vulkan com
  `SPIRV-Headers_SOURCE_DIR` apontando o `spirv-headers` do próprio SDK, `SPIRV_SKIP_EXECUTABLES`/
  `SPIRV_SKIP_TESTS` ON (só libs) e `SPIRV_WERROR=OFF` (GCC 15 do devkitA64 avisa em código de terceiro).
- **Mais barato que mexer na série do project8-sw**: a série externa é compartilhada com outros ports e o
  `--check` compara a árvore exata; a série local fica versionada no repositório.
- **Build/install**:
  - Mac: `tools/build_spirv_tools.sh` (chama `tools/build_host_sdk.sh --sdk-only` + `--check`) instala
    `out/sdk-install/lib/libSPIRV-Tools{,,-opt,-link,-lint,-diff,-reduce}.a` e
    `out/sdk-install/include/spirv-tools/{libspirv.h,libspirv.hpp,optimizer.hpp,linker.hpp}`.
  - devkitA64: `tools/build_spirv_tools.sh --switch` configura o subprojeto avulso com
    `cmake/switch-rexglue-gcc.cmake` no container `gh3-switch-clang` e instala em
    `out/sdk-switch-install/lib`. O mesmo alvo também compila dentro do build do NRO
    (`cmake --build build/switch --target SPIRV-Tools-opt`), porque o `sdk/` é subdiretório dele.
- `tools/build_spirv_tools.sh --check` valida o patch, o header e o símbolo `spvtools::Optimizer` na lib
  (`nm`), que é o oráculo do `tests/test_spirv_tools.py`.
- **Armadilhas**:
  - `patches/sdk/` seria ignorado pelo `.gitignore` (`sdk/` casa em qualquer nível): por isso a série
    local chama `patches/rexglue-local/`.
  - `nm lib.a | grep -q ...` morre de SIGPIPE sob `set -o pipefail` (mesma armadilha do Switch antiga):
    usar `grep ... >/dev/null`.
  - O build do SDK agora compila ~330 TUs a mais (spirv-tools + lint/diff/reduce, que o CMake do projeto
    não deixa desligar) tanto no Mac quanto em cada build do NRO.
- Consumo das libs (`gh3_native_spirv_opt`) é o T35/F2.2.

### F2.2 — spirv-opt nos shaders traduzidos (2026-10-20)

O tradutor do SDK gera SPIR-V verboso e o caminho emulado até desliga o otimizador. No renderer nativo,
`src/native/native_spirv_opt.h` roda `spvtools::Optimizer` (RegisterPerformancePasses) em cada módulo
traduzido antes de `vkCreateShaderModule`, com `gh3_native_spirv_opt` (padrão false; true no
`switch/gh3.toml`).

- **Um módulo por tradução**, criado no thread da tradução (ou no thread do ring, sem pipelines async) e
  guardado em `optimized_shader_modules`. Nunca por quadro; o otimizador custa dezenas de ms.
- **Propriedade**: o módulo otimizado é nosso e é destruído no `Shutdown`; o original continua sendo do
  `VulkanTranslation` (destruído em `shaders.clear()`). Em fallback não guardamos o módulo do SDK no mapa,
  senão seria destruído duas vezes.
- **Validador com scalar block layout**: o `XeSystemConstants` do tradutor põe um array de `vec4` em
  offset 28, fora do layout padrão. O SDK liga `VK_EXT_scalar_block_layout` no dispositivo
  (`vulkan_device.cpp`, `features_1_2.scalarBlockLayout`), então `SetScalarBlockLayout(true)` no
  `ValidatorOptions`. Sem isso **todo** shader cai em fallback (`invalid input`).
- **Fallback** ao original em: módulo pequeno demais, entrada inválida, `Optimizer::Run` falso/saída vazia
  ou saída inválida. O primeiro fallback de cada motivo loga `spirv-opt: using the original shader (...)`.
- **Log** a cada 10 s (`gh3_native_system.cpp`, `Report`): `[native] spirv-opt: N shaders, X% fewer
  instructions (M fallbacks)`, cumulativo (a linha continua saindo mesmo quando o intervalo não traduziu
  shader nenhum).
- **Medido no Mac** (captura do T35, 720p): 75 shaders no primeiro relatório com 28,4% menos instruções e
  0 fallbacks; captura white=0,012 stddev=53,2. O ganho é de GPU (o NAK compila menos), então a medição
  de FPS que vale é a da F5 no Switch.

### F2.3 — NAK do GM20B: latência 200 e peephole 8 (2026-10-20)

O NAK do NVK ordenava cada leitura de textura/memória como se custasse **32 ciclos**. No Tegra X1
(LPDDR4 compartilhada com a CPU) uma falha de cache custa centenas: o scheduler separa pouco a
leitura do primeiro uso e deixa menos requisições em voo. O nfsmw-nx mediu, além disso, que o
`nir_opt_peephole_select` com limite **0** só achata `if`s com os dois lados vazios — e o NAK não
tem passe de if-para-predicado, então todo `if` que sobra vira `SSY/BRA/SYNC` de verdade.

- **Já vinha na série do project8-sw fixada** (`patches/mesa-switch/0002-nak-gm20b-latency-and-flattening.patch`,
  tirado do nfsmw-nx, MIT): latência 200 para ops de textura e de memória em GPUs anteriores a
  Volta (SM70), `peephole_select` com limite 8 e a revisão `1 << 24` nas compiler flags para o
  cache de shaders não reusar binários do driver antigo. Não foi criado um patch local novo porque
  duplicar os mesmos hunks não aplicaria sobre a árvore já patcheada; o T36 tornou o caminho
  **verificável**.
- **Verificação**: `tests/test_nak_patch.py` e `tools/build_mesa.sh --check` conferem, no fonte do
  Mesa que o build compila (`out/mesa`, ou `--mesa DIR`), as duas ocorrências da latência 200, o
  limite 8 e a revisão. Sem qualquer uma delas o `--check` falha mesmo com `libvulkan.a` válida —
  antes ele só olhava `vkCreateInstance` e não provava que o driver tinha o NAK.
- **Ganho esperado (medido no nfsmw, não no GH3)**: leituras separadas dos consumidores e branches
  `1.806 → 641` nos shaders. A medição que vale aqui é a **F5 no Switch** (FPS e `GPU by pass`).
- **Risco**: achatar `if`s aumenta a pressão de registradores, e spill para memória local é caro no
  GM20B. Conferir com `NVK_SHADER_STATS=1` que "B de memoria local" fica em 0.
- **Fora do escopo**: o resto do pacote de draw path do nfsmw (rebinds de constant buffer, set 4 por
  diferenças, medição `nvk_switch_dibujo`) está entrelaçado com o layout de sets daquele renderer e
  precisa de medição no aparelho; fica como tarefa própria.
- **Build**: `tools/build_mesa.sh` refeito com as imagens em cache (331,7 s medido); driver em
  `out/driver-sdk/lib/{libvulkan.a,libnak_rs.a}`.

### F2.4 — simplificação dos shaders traduzidos (2026-10-20)

`src/native/native_shader_simplify.h` roda depois da tradução e antes de `vkCreateShaderModule`
(`gh3_native_shader_simplify`, padrão **true**; o `spirv-opt`, quando ligado, roda depois). Três regras,
todas tiradas dos ganhos que o nfsmw-nx mediu (`~/gh3-refs/nfsmw-nx/docs/shaders.md`):

- **`max(a,a)`/`min(a,a)` como cópia**: `GLSL.std.450` FMin/UMin/SMin/FMax/UMax/SMax com os dois
  operandos iguais vira `OpCopyObject`. No nfsmw essas instruções chegavam ao GPU como `fcanonicalize` →
  `fmul(a, 1.0)`; o NAK resolve a cópia como movimentação de registrador. A regra cobre NaN e zero
  negativo (a identidade vale para os dois).
- **Query de tamanho de textura repetida**: duas `OpImageQuerySizeLod`/`Size`/`Levels`/`Samples`/
  `QueryLod` com o mesmo tipo e os mesmos operandos, no mesmo bloco, viram uma query + cópia do primeiro
  resultado. O tamanho da imagem não muda dentro do bloco.
- **Branch redundante**: `OpBranchConditional` com os dois destinos iguais vira `OpBranch` (removendo o
  `OpSelectionMerge` do cabeçalho, senão o bloco fica inválido) e um bloco que só salta para outro
  (`OpLabel` + `OpBranch`) é fundido no predecessor quando tem um único predecessor e não é alvo de
  merge/phi/switch. É a versão pós-tradução do "1.806 `if (p0)` → 641" do nfsmw.

O módulo é **validado antes e depois**; qualquer falha devolve o original intacto e conta fallback (uma
linha por motivo no log). O passe custa uma validação por tradução (uma vez por shader, nunca por quadro).
O log de 10 s é `[native] shader simplify: N shaders, A max/min as copies, B texture queries reused, C
branches merged (X% fewer words, M fallbacks)`.

**Medido no GH3 (captura do T37, 1026 shaders)**: 0 de cada regra, 0 fallbacks, 0,0% menos palavras —
ou seja, **não há o que cortar aqui**: o tradutor do SDK (herdado do Xenia) já devolve `a` quando os
operandos de MAX/MIN são iguais (`spirv_translator_alu.cpp`, `if (a == b) return a;` e
`identical == used_result_components`), só emite `OpImageQueryLod` (nenhuma query de tamanho) e gera CFG
sem blocos-saltinho. A captura de gameplay ficou idêntica (white=0,001, stddev=57,2). O passe fica como
**guarda** para quando um tradutor/ucode futuro voltar a emitir esses padrões, com o custo de uma
validação no carregamento; o ganho de GPU do F2.4 no Switch é zero enquanto for assim (F5 confirma).

### F2.5 — prewarm de pipelines + cache versionado (2026-10-20)

O driver leva **59-160 ms** por pipeline novo no Switch. O `gh3_native_async_pipelines` já tira a
compilação do thread do anel, mas os draws que precisam do pipeline ficam sem desenhar até ele existir.
O F2.5 fecha isso guardando as chaves e recriando tudo **antes de o jogo rodar**.

- **`src/native/native_pipeline_prewarm.h`** (puro, testado sem GPU): formato do arquivo
  (`pipelines-<versão>.bin`, ao lado do cache do driver), cabeçalho com magic + versão + contagem +
  XXH3 do corpo, leitura validada (arquivo truncado, de outra versão, com hash diferente ou com
  contagem acima do teto é recusado inteiro), dedupe por chave e a linha de log.
- **Versão do cache** = `(formato << 8) | passes`: o spirv-opt (F2.2) e a simplificação (F2.4) mudam o
  SPIR-V que o driver compila, então trocar qualquer um muda o nome/versão do **cache do driver**
  também (`native-<título>-<vendor>-<device>-<driver>-<versão>.cache`) e nada antigo é reaproveitado.
  O cabeçalho do cache do driver ganhou a versão (era `{magic, tamanho, hash}`, agora
  `{magic, versão, tamanho, hash}`): um arquivo de outra versão é ignorado (pode apagar os antigos).
- **Registro**: `GetPipeline` copia a `PipelineKey` e o microcódigo dos shaders (ordem do host, como o
  `Shader` do SDK) na primeira vez que uma chave aparece; o arquivo é gravado em thread própria a cada
  10 s de mudança e no `Shutdown` (o capture encerra com SIGTERM, então a gravação periódica importa).
- **Prewarm** (`InitializeShaderStorage` → `InitializeDriverCache`, antes de o guest rodar): recria os
  shaders a partir do microcódigo (`LoadShader` do jogo depois acha o mesmo objeto pelo hash), traduz
  com o tradutor do thread principal (os workers ainda estão ociosos, então continua um thread só
  traduzindo), recria a geometria, o layout (contagens empacotadas em `key.layout`) e o pipeline.
  Tudo roda no mapa `pipelines`, então o primeiro draw da gameplay já acha o pipeline pronto.
- **Log**: `[native] pipeline prewarm: N of M pipelines from the cache (version 0x..., X failed, Y
  skipped)` na inicialização e, a cada 10 s, `pipeline prewarm: N at start (...), M new since` — o M é
  o número que o plano quer ver zerado na gameplay.
- **Medido no Mac (T38)**: 110 de 110 pipelines recriados em ~1,0 s de inicialização (cache do driver
  já quente); a captura ficou igual (white=0,002, stddev=51,6). Uma corrida seguinte ainda viu 7-10
  chaves novas: o manifesto é a união das telas já vistas e converge com execuções (a música não é
  determinística em wall clock, ver T16).
- **Cvar**: `gh3_native_pipeline_prewarm` (padrão true; desligar só tira o prewarm, o cache do driver
  continua versionado). O arquivo é **local da máquina**, derivado do jogo como o cache do driver —
  nunca entra no repositório nem no pacote do SD.

### F3.1 — espera de fence do presenter, medida por 10 s (2026-10-20)

O T17 mostrou que o present do gameplay não é o fallback de `ClearOutput`: ele passa pelo presenter do
SDK (`VulkanPresenter::RefreshGuestOutput` → `PaintAndPresent`). As esperas de fence que o profiler do
Switch mostra (playbook §8: 341-656 ms/s) acontecem lá, nos trackers de submissão do próprio presenter:
o anel de **paint** (`paint_context_.submission_tracker`, espera a submissão que vai ser reutilizada a
cada quadro) e o **refresh do guest output**
(`guest_output_image_refresher_submission_tracker_`, só quando o tamanho do frontbuffer muda). Os
`vkWaitForFences` do processador de comandos e os 4 **work slots** do renderer não entram na conta.

- **`patches/rexglue-local/0002-present-fence-wait.patch`** (aplicado pelo `tools/setup_sdk.sh`):
  - `include/rex/ui/vulkan/present_wait_stats.h`: `AddPresentFenceWaitNs` (acumula ns + contagem em
    atômicos) e `ConsumePresentFenceWaitNs` (troca atômica para 0, devolve a janela).
  - `VulkanSubmissionTracker::SetPresentFenceWaitAccounting(true)`: cronometra o `vkWaitForFences` de
    `AwaitSubmissionCompletion` só nos trackers marcados. O presenter marca os três dele
    (`guest_output_image_refresher_`, `ui_` e o do paint) em `InitializeSurfaceIndependent`.
- **Runtime** (`src/native/gh3_native_system.cpp`, `Report` a cada 10 s): lê e zera a janela e loga
  `[native] present fence wait X.X ms/10 s (N waits, Y.Y ms each, F frames)`, ao lado das linhas
  `[native] last 10 s`. Formatação pura em `src/native/native_present_wait.h` (testada sem SDK com `c++`).
- `vkAcquireNextImageKHR` **não** é fence e não entra: no Mac é ele que segura o quadro (vsync), não o
  fence; no Switch há o aviso de 3 s do presenter para esse caso.
- **Medido no Mac (captura T39)**: 0,0-1,6 ms/10 s com 0-2 esperas por janela em ~500 quadros — ou seja,
  no Mac o presenter não serializa CPU/GPU, como esperado (a GPU não é o limite). A decisão do anel de
  present (F3.2/T40) depende desta linha medida **no Switch** (F5); no Mac ela não justifica nada.
- Cuidado ao ler: ns e contagem são trocados em átomos separados, então uma espera que começa entre as
  duas trocas pode cair na janela seguinte (erro de no máximo uma espera por relatório).

### F3.2 — anel de saída (present) com 3 slots, conferido e preso ao SDK (2026-10-20)

O F3.2 pedia "present ring 3 output slots (1 submit/frame, IMMEDIATE no Switch)" **se o F3.1
justificasse**. A premissa de um "command buffer + fence único" no present era o **fallback de cor de
teste** (`ClearOutput`), como o T17 já tinha mostrado: o gameplay apresenta pelo `VulkanPresenter` do
SDK, que **já tem** o anel — `PaintContext::kSubmissionCount = 3` submissões de paint com um
`vkResetCommandPool`/fence por slot, uma submissão por quadro, `minImageCount = max(3,
minImageCount)`, e o mailbox de guest output também com 3 imagens. A cada quadro ele espera só a
submissão do slot reutilizado (`current - 3`), não todas.

- **`src/native/native_present_ring.h`** (puro, testado sem SDK): `kOutputSlots = 3`,
  `PlanSubmission(submission, slots)` (slot = `submission % slots`, espera a submissão `- slots` só
  quando o slot é reutilizado; `slots = 1` reproduz o comportamento antigo) e a linha de log.
- **Fiação** (`native_renderer.cpp`): `static_assert` de `VulkanPresenter::kMaxActiveGuestOutputImageVersions
  == kOutputSlots` (se o SDK mudar o anel, o build acusa), e a linha de inicialização
  `[native] present ring: 3 output slots (3 mailbox images, 1 submit/frame, immediate on/off), 4 work
  slots`. O log antigo (que dizia "present ring: 4 slots" para o `kSubmissionSlots`) virou
  `[native] work ring: 4 slots`: são anéis diferentes e o plano proíbe confundi-los ou acrescentar um
  quarto work slot (o nfsmw quebrou a imagem com isso). `switch/gh3.toml` declara
  `vulkan_allow_present_mode_immediate = true` (já era o padrão do SDK; o Mac desliga em
  `run-gh3.command` porque IMMEDIATE/mailbox em tela cheia travam o MoltenVK).
- **Medido no Mac (captura T40)**: log verde, imagem idêntica (white=0,001, stddev=46,2). Com a linha
  do F3.1 em 0-2 esperas por janela de 10 s, **não há o que ganhar** com outro anel aqui; o quarto
  slot/thread de apresentação continuam proibidos. A confirmação no Switch (IMMEDIATE/Wait do anel)
  fica para a F5, como no F3.1.

### F3.3 — resolve de profundidade sem cópia, com restauração (2026-10-20)

Na 360 um resolve de profundidade não é cópia: a textura resolvida e o render target são duas vistas da
mesma EDRAM. O F3.3 reproduz isso só quando é seguro: **alvo de profundidade inteiro**, 1 amostra,
destino do mesmo tamanho e formato, e o jogo amostrando o resolve como profundidade (não como as words
de memória).

- **`src/native/native_resolve_ownership.h`** (puro, sem Vulkan, testado com `c++`): `Decide()` devolve
  `handoff` (destino fica com a imagem), `restore first` (o conteúdo já está num destino: copia de volta
  antes), ou o motivo da recusa (`not depth`, `multisampled`, `not the whole target`, `size/format
  mismatch`); `RestoreBeforeWrite()` dispensa o restore quando o clear cobre o alvo inteiro; `Stats`/
  `Format()` produzem a linha `[native] resolve ownership: N without copy (X MB), M restores (Y MB),
  K copied (declined: motivo:conta)`.
- **`native_textures.cpp`**: `BindBorrowedDepth()` registra a imagem do alvo como destino de resolve no
  endereço do guest (sem copiar), com view de **aspecto depth**; `ResolveDestination.depth` separa esses
  destinos do `R32_SFLOAT` que já existia para leitura de memória (o `CopyToSized` de profundidade vai
  por `vkCmdCopyImage`; o compute de cópia é só de cor).
- **`native_renderer.cpp`**: `gh3_native_resolve_swap` (padrão false). No handoff o alvo ganha uma
  imagem nova (limpa em 1.0) e guarda `content_address`; `RestoreTargetContent()` traz o conteúdo de
  volta com `vkCmdCopyImage` antes de o alvo ser lido (resolve) ou escrito sem clear (passes com load,
  clears parciais). Cor fica de fora até a paridade (o jogo reusa alvos de cor sem limpar).
- **Medido no Mac (captura T41)**: **zero handoffs** — 276-9825 resolves de profundidade por janela de
  10 s, todos recusados como `not the whole target`: o alvo de profundidade da cena é 1280x2048 e o
  resolve cobre 1280x720; o mapa de sombra resolve 1024 de um alvo de 1040; os RTTs têm altura alinhada
  a 16 (320x192 com resolve de 184). Ou seja, no GH3 de hoje o caminho antigo (cópia) continua sendo o
  único exercitado, e a paridade é a de antes. O F3.3 é uma guarda para quando o alvo inteiro for
  resolvido; para render ganho real no GH3 seria preciso reduzir a alocação do alvo 1x de profundidade
  à altura usada (`known_1x_height`) — tarefa à parte, com risco no reuso do alvo.

### F4.1 — caches por draw no anel: IM_LOAD, sampler, query, zero log (2026-10-20)

No nfsmw-nx os mesmos quatro cortes valeram ~25 ms/s de thread do anel (IM_LOAD sem memcmp) e 1,2 ms
(menos resets de query) — `docs/performance-history.md` §5. No GH3 o consumidor do anel é
`src/native/gh3_native_system.cpp` + `native_renderer.cpp` (tudo no thread do anel), e a política pura
ficou em **`src/native/native_ring_caches.h`** (compila sem SDK/GPU; `tests/test_ring_caches.py`).

- **IM_LOAD_IMMEDIATE** (`guest_address == 0`): cada pacote trocava os bytes do microcódigo inteiro,
  calculava o XXH3 e procurava o shader no mapa. `ring_caches::ImLoadCache` (2 tipos × 32 conjuntos ×
  4 vias) escolhe a via por uma assinatura de tamanho + 4 palavras e **só aceita com todas as palavras
  iguais (memcmp)**; assinatura igual com conteúdo diferente é falta, nunca shader errado. O caminho
  por endereço (`shaders_by_location`, já existente) não passa pela cache: ali a verificação por
  cabeçalho + XXH3 uma vez por quadro continua mais barata.
- **Sampler entre quadros**: a chave do estado virou `ring_caches::SamplerKey()` (pura) e a cache
  `TextureCache::Impl::samplers` vale a sessão toda; `stats_.samplers_created` conta quantos
  `vkCreateSampler` aconteceram.
- **Query resets seletivos**: com `gh3_native_gpu_profile` o pool de 512 timestamps era limpo inteiro
  a cada quadro perfilado. `ring_caches::SelectiveQueryReset` limpa no começo só o que **aquele slot**
  escreveu na última vez e, se o quadro usar mais, em blocos de 32 antes de cada escrita. O carimbo do
  render pass passou a ser gravado **antes** do `vkCmdBeginRendering` (a limpeza de query não pode
  dentro de um render pass).
- **Zero log no anel**: saíram os cinco probes `[T9]` (cada um mantinha um `static unordered_set` e
  hashing por draw/resolve/swap, mesmo depois do teto de linhas). O que sobra do anel é o relatório de
  10 s (`ring caches: ...`), emitido fora do thread do anel.
- **Medido no Mac (captura T42, 80 s)**: **110.884 acertos / 110.891 IM_LOAD_IMMEDIATE** (~1.400/s),
  7 faltas, 0 evictions; **10 samplers criados** a sessão toda; com o perfil de GPU ligado, **~30-50
  queries limpas por quadro perfilado em vez de 512**. A imagem não mudou (white=0,002, stddev 41,5).
  O ganho de FPS no Switch depende da GPU (F5), mas é CPU do anel que deixa de ser gasta.
