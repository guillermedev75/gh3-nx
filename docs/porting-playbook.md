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
