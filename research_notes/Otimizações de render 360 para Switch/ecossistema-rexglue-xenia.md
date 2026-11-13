# Ecossistema ReXGlue/Xenia: render e performance para port Switch

Notas de pesquisa para o GH3-nx (Guitar Hero III, Xbox 360 → Switch). Tudo abaixo foi consultado em **2026-10-19**. Convenção: **[verificado]** = README, código ou issue do próprio projeto; **[relato]** = relato de usuário sem confirmação do mantenedor; **[inferência]** = conclusão minha a partir das fontes, não afirmação de nenhum projeto. Termos técnicos ficam em inglês de propósito.

## O que é o ReXGlue, como o codegen funciona e que hooks/APIs de renderer nativo ele expõe

### Takeaway

O ReXGlue é um **static recompiler** de `default.xex` para C++23, com um runtime derivado do Xenia, mas sem JIT: o jogo inteiro é traduzido offline e linkado como C++ nativo. O backend gráfico padrão (`rexgpu-xenos`) é o caminho **emulado** derivado do Xenia — command processor PM4, EDRAM e tradução de shader em runtime — e é injetado no runtime via `IGraphicsSystem`. Não existe uma API pública e estável de "native renderer": os ports que trocam o renderer substituem esse sistema gráfico e/ou leem o PM4 ring do jogo.

### Cited Findings

- O ReXGlue "converts Xbox 360 PowerPC code into portable C++ that runs natively on modern platforms"; o próprio README avisa que o projeto é experimental e que a API pública pode quebrar. — [README do rexglue-sdk](https://github.com/rexglue/rexglue-sdk)
- O codegen tem duas fases: **Analysis** (7 estágios: Register, Scan, Discover, GapFill, Discover, Merge, Validate) e **Recompilation**, que percorre o `FunctionGraph` selado e emite C++. A tabela de dispatch cobre ~350 builders de instrução PPC, incluindo `lvx/stvx`, `vperm`, `vmaddfp`, `fsel`, `mtctr/bctr`. — [Codegen Pipeline Overview](https://github.com/rexglue/rexglue-sdk/wiki/Codegen-Pipeline-Overview) (editado em 2026-04-27)
- O codegen detecta MMIO por `eieio` e por `lis/oris` com metades altas conhecidas: `0x7FC8–0x7FCF` para registradores de GPU e `0x7FEA–0x7FEF` para XMA/APU; endereços de GPU viram `PPC_MM_LOAD_*`/`PPC_MM_STORE_*`. — [Codegen Pipeline Overview](https://github.com/rexglue/rexglue-sdk/wiki/Codegen-Pipeline-Overview)
- O runtime é "derived from Xenia's emulator core"; `Processor` virou `FunctionDispatcher` (tabela guest→host), sem JIT e sem code cache. As funções recompiladas têm a assinatura `void sub_XXXXXXXX(PPCContext& ctx, uint8_t* membase)`. — [Runtime Architecture Overview](https://github.com/rexglue/rexglue-sdk/wiki/Runtime-Architecture-Overview) (editado em 2026-04-27)
- `IGraphicsSystem`, `IAudioSystem` e `IInputSystem` são **injetados** pelo `RuntimeConfig`; não são possuídos pelo core do SDK. Isso é o ponto de substituição do backend gráfico. — [Runtime Architecture Overview](https://github.com/rexglue/rexglue-sdk/wiki/Runtime-Architecture-Overview)
- O plugin gráfico `rexgpu-xenos` exporta `rex_gpu_abi_version()` e `rex_gpu_create(abi_version, GpuCreateInfo*)`; o backend é escolhido por `info->backend` (`"any"`, `"vulkan"` ou `"d3d12"`). — [plugin_main.cpp](https://github.com/rexglue/rexglue-sdk/blob/main/src/graphics/plugin_main.cpp)
- A árvore `src/graphics/` do SDK contém `command_processor.cpp`, `primitive_processor.cpp`, `shared_memory.cpp`, `register_file.cpp`, `packet_disassembler.cpp`, `shaders/`, `vulkan/`, `d3d12/` e `xenos.cpp` — a mesma decomposição do GPU do Xenia. — [listagem de src/graphics](https://github.com/rexglue/rexglue-sdk/tree/main/src/graphics)
- Flags gráficas do SDK confirmam o caminho emulado: `native_2x_msaa` (default `true`), `gamma_render_target_as_unorm16` (default `true`), `depth_float24_round` (default `false`), `depth_transfer_not_equal_test` (default `true`) e `use_fuzzy_alpha_epsilon` (default `false`, descrita como "prevent flickering on NVIDIA graphics cards"). — [flags.cpp](https://github.com/rexglue/rexglue-sdk/blob/main/src/graphics/flags.cpp)
- O SDK tem um tradutor **SPIR-V em runtime**: o doc do project8-sw diz que os shaders são "translated at run time by the SDK's SPIR-V translator, on a worker thread". — [project8-sw docs/native-renderer.md](https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md)
- O tradutor SPIR-V do ReXGlue ainda tem bugs de correção: o PR #490 corrige o uso de `fetch_constant_word_3` para o exponent bias, portado de um commit do Xenia Canary, e cita Daytona USA, Forza Horizon e Lollipop Chainsaw como afetados. — [PR #490](https://github.com/rexglue/rexglue-sdk/pull/490) (aberto em 2026-10-10)
- O caminho nativo real do project8-sw/nfsmw-nx **não** usa a API gráfica do SDK: ele deixa o Direct3D do jogo rodar e lê o **PM4 ring** numa thread própria, mantendo o register file e nunca emulando EDRAM. — [nfsmw-nx docs/native-renderer.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md)
- Para manter o jogo em passo, o port escreve o valor que o jogo espera em `WAIT_REG_MEM` → `SCRATCH_REG/SCRATCH_ADDR`; para identificar shaders, hooka as funções de criação de shader e anota qual container original cada shader do Direct3D representa. — [nfsmw-nx docs/native-renderer.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md)
- O project8-sw substitui o graphics system do SDK por `src/game/src/native/thps_native_system.*`, adaptado de `nfsmw_nativo_sistema.*`; render targets são imagens Vulkan com chave por EDRAM base, pitch, formato e sample count, sem cópia de/para um buffer de EDRAM. — [project8-sw docs/native-renderer.md](https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md)
- O wiki do ReXGlue diz "**v0.3.x** (active development) ... Graphics, audio, and input backends are in flux", enquanto o repositório/releases já estão em v0.10.0. Há defasagem de documentação; trate o wiki como visão arquitetural, não como referência de API da versão atual. — [wiki Home](https://github.com/rexglue/rexglue-sdk/wiki); [releases](https://github.com/rexglue/rexglue-sdk/releases)

### Inferences

- O "hook de renderer nativo" na prática é um destes dois: (a) implementar/substituir `IGraphicsSystem` e criar o backend via `rex_gpu_create`; (b) manter o Direct3D do guest e consumir o PM4 ring, como nfsmw/project8 fazem. Não há, hoje, uma interface alto nível documentada do tipo "registre um renderer".
- Como o runtime e o backend Xenos vêm do Xenia, as regras de correção do Xenia (ownership de EDRAM, formatos, gamma, half-pixel, resolves) valem como referência direta mesmo quando o port usa Vulkan nativo.
- A ausência de uma API estável significa que cada port copia/adapta o renderer de outro port; foi exatamente o que o project8-sw fez com o nfsmw-nx (GPL-3.0).

### Gaps

- Não achei documentação oficial do ReXGlue sobre a arquitetura interna de PM4/EDRAM/render target cache; só o código e o wiki do codegen/runtime.
- Não achei uma página de wiki do ReXGlue dedicada a "native renderer" ou à ABI de `IGraphicsSystem`.
- Não há benchmark público ReXGlue-Xenos vs Xenia no Switch; os números de "1–5 FPS" são relatos dos ports, não medições independentes.

## Quais projetos usam ReXGlue e o que cada um documenta sobre renderer e performance

### Takeaway

O ecossistema tem dois grupos claros: **ports Switch** (project8-sw e nfsmw-nx) que trocaram o Xenos emulado por um renderer Vulkan nativo no NVK, e **ports PC** que em geral mantêm o backend Xenos/D3D12 emulado (AC6, Downpour) ou escrevem um renderer nativo próprio (Hell's Gate, Pinyon Shift). Só os projetos com `docs/` de performance (nfsmw-nx, project8-sw, Downpour) têm números e causas documentadas; a maioria só tem README e issues.

### Cited Findings

| Projeto | Tipo | Alvo | Renderer | Performance / correções documentadas |
|---|---|---|---|---|
| [project8-sw](https://github.com/R-drg/project8-sw) | Recomp ReXGlue | Switch | Vulkan nativo em NVK/Mesa | Alvo 30 FPS; 768 MHz handheld; 2.000–3.000 draws/frame; primeiros minutos compilam pipelines; cutscenes Bink com blocos verde/magenta; resolve copiado no tamanho amostrado; depth resolve por compute. — [README](https://github.com/R-drg/project8-sw) e [docs/native-renderer.md](https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md) |
| [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) | Recomp ReXGlue | Switch | Vulkan nativo em NVK | 16,1 FPS (mediana) → 26,0 → 30,3 → ~34 → 35,4; 32–35 FPS no release; GPU 60,2 ms → ~23 ms; 3 work slots; 512 MB de textura; pipeline cache + 113/113 pipelines em 0,3 s. — [README](https://github.com/StevensND/nfsmw-nx), [performance-history.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md), [native-renderer.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md) |
| [Project8Recomp](https://github.com/R-drg/Project8Recomp) | Recomp ReXGlue | PC | Base do project8-sw; detalhes de renderer não verificados | É a recompilação PC de origem do project8-sw. — [README do project8-sw, Credits](https://github.com/R-drg/project8-sw) |
| [Pinyon Shift](https://github.com/arcanite24/pinyon-shift) | Recomp com fork **ShiftGlue** (fork do ReXGlue) | Windows, Linux, Steam Deck, macOS Apple Silicon, Android alpha | Vulkan nativo ("a renderer written for Forza Horizon") | 60/120 FPS; 1x–4x de resolução interna; FSR 1, FXAA, HDR; RTX 4080: 119 FPS em 1x/2x, 102 FPS em 3x; Deck: 60 FPS em 1x a 15 W; Android Snapdragon 8 Gen 2 ~60 FPS. — [README](https://github.com/arcanite24/pinyon-shift) |
| [Hell's Gate (Dante's Inferno)](https://github.com/florinp93/hells-gate-recomp) | Recomp ReXGlue | Windows, Linux (x86-64/ARM64) | Nativo em **DiligentCore (Vulkan)**, default desde 0.8.0; `--renderer=xenos` é o fallback D3D12 | Até 240 FPS, SMAA, anisotropic filtering, shadow map em escala 1–4; substitui a camada Direct3D do jogo. — [README](https://github.com/florinp93/hells-gate-recomp) |
| [DownpourRecomp](https://github.com/LittleBitUA/DownpourRecomp) | Recomp ReXGlue | Windows | D3D12 emulado com **ROV** e **RTV** (`render_target_path_d3d12`); experimento de renderer nativo foi removido | 60 FPS; ~1.370 PSOs pré-aquecidos; `pso_missing_policy='skip'`; `d3d12_pso_no_block_at_submission_end=true`; `d3d12_pso_library_enable=true`; correção de chromatic noise via depth→7e3 ownership transfer. — [README](https://github.com/LittleBitUA/DownpourRecomp) |
| [AC6Recomp](https://github.com/sal063/AC6_recomp) | Recomp ReXGlue | Windows | Backend Xenos/D3D12 **emulado** ("visible rendering currently remains authoritative in the vendored RexGlue/Xenia graphics backend") | 60 FPS; resolução 2x/3x; terreno em resolução cheia; performance é "known issue" por falta de native renderer; fixes de deswizzle, DoF, trails, water line, cutscene resync. — [README](https://github.com/sal063/AC6_recomp) |
| [NocturneRecomp](https://github.com/birabittoh/NocturneRecomp) | Recomp ReXGlue | Windows, Linux | Não verificado | Castlevania: SotN (XBLA); sem doc de renderer/performance no README. — [repo](https://github.com/birabittoh/NocturneRecomp) |
| [Condemned2Recomp](https://github.com/psxrestore/Condemned2Recomp) | Recomp ReXGlue | PC (plataforma não declarada no README) | Não verificado | README curto; sem doc de renderer/performance. — [repo](https://github.com/psxrestore/Condemned2Recomp) |

Outros projetos do ecossistema, relevantes para vocabulário de renderer mas sem detalhe verificado nesta rodada: [PGR4-Recomp](https://github.com/beatrixzy/PGR4-Recomp), [DPRecomp](https://github.com/LittleBitUA/DPRecomp) (D3D12 nativo em teste, até 4x), [FM4-Recomp](https://github.com/Alexbeav/fm4-recomp), [Estacado/The Darkness](https://github.com/invinceble55-wq/Estacado-The-Darkness-Recomp), [Army of Two](https://github.com/OpenPortroid/ArmyOfTwoRecompilation), [Skate 2](https://github.com/christianortel/skate2-recompile-pc). O **SpeedBreaker** (NFSMW para PC/Deck/Mac) é listado com a tag **XenonRecomp**, não ReXGlue — não misture as linhagens sem confirmar no código. — [SpeedBreaker](https://github.com/SpeedBreakerProject/speedbreaker)

### Técnicas diretamente aplicáveis ao GH3-nx

**GPU (o gargalo atual do GH3):**

- **Desenhar sem tiling de EDRAM.** O nfsmw eliminou o desenho em tiles que o Xbox 360 fazia para caber nos 10 MB de EDRAM; no Switch (immediate-mode, sem tiling) isso é trabalho extra. O GH3 já segue esse modelo no renderer nativo. — [nfsmw performance-history §3](https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md); [platform-notes](https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md)
- **Resolve sem cópia quando o próximo uso é clear.** Trocar as imagens em vez de copiar rendeu 2,36 ms/frame no nfsmw; quando o conteúdo antigo é necessário depois, ele é trazido de volta. — [nfsmw performance-history §3](https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md)
- **Alias MSAA → alvo 1x quando o jogo reusa a mesma EDRAM com pitch/amostras diferentes.** O GH3 já tem `AliasMsaa()`/`SyncAliasTarget()`; a armadilha documentada é que o sync precisa rodar em `Draw()`, não só em `GetRenderTarget()`, porque setups em cache não passam pelo segundo. — [docs/porting-playbook.md §7](https://github.com/guillermedev75/gh3-nx/blob/main/docs/porting-playbook.md)
- **Constantes por dynamic uniform buffer.** No nfsmw: +18–23% de FPS (36 → 43 FPS), GPU 25,9 → 21,4 ms, frames >33 ms de 13–15% para 1–2%; custo de 1,1 µs de CPU por draw. — [nfsmw docs/shaders.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/shaders.md)
- **Pré-computar `1/size` das texturas.** 160 consultas de tamanho em 109 pixel shaders viraram 0, mantendo as 435 amostras. — [nfsmw docs/shaders.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/shaders.md)
- **Fundir blocos predicados e remover `max(a,a)`.** 1.806 `if (p0)` viraram 641; 727 `max(a,a)` em pixel shaders e 482 em vertex shaders foram eliminados. — [nfsmw docs/shaders.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/shaders.md)
- **ZCULL: ganho real no nfsmw, regressão no GH3.** O nfsmw ganhou ~2 ms de GPU ao habilitar ZCULL no NVK; o GH3 mediu ~22 FPS com ZCULL contra ~34 sem, e desligou. Não trate ZCULL como ganho universal: depende do patch do Mesa, do build e do padrão de profundidade do jogo. — [nfsmw docs/mesa.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/mesa.md); [GH3 playbook §8](https://github.com/guillermedev75/gh3-nx/blob/main/docs/porting-playbook.md)
- **Cortar passes caros primeiro.** No GH3, desligar shadow e bloom levou de ~34 para ~45 FPS, com renderer CPU/GPU de 21–39/17–29 ms para 13/13 ms e frames >50 ms de 46–108 para 8 por 10 s. No nfsmw, shadow maps eram 13,6 ms dos 60,2 ms iniciais. — [GH3 playbook §8](https://github.com/guillermedev75/gh3-nx/blob/main/docs/porting-playbook.md); [nfsmw performance-history](https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md)
- **Pipeline cache + prewarm.** Criar pipeline no NVK custa ~59 ms (82 ms no primeiro race); o nfsmw salva o `VkPipelineCache` e recria 113/113 pipelines em 0,3 s numa thread de fundo. O project8 prefere **pular o draw** até o pipeline ficar pronto (objetos somem por um instante) em vez de travar o frame. — [nfsmw native-renderer](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md); [project8 native-renderer](https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md)
- **Resolução: não é botão mágico.** O nfsmw tentou baixar a resolução interna e não ganhou, porque o custo era shading por pixel; o project8 usa MSAA como amostras da EDRAM; o GH3 planeja renderizar o palco 3D a 0,75x e subir com blit LINEAR antes da HUD. Meça o custo por passe antes de assumir. — [nfsmw porting-another-game §7](https://github.com/StevensND/nfsmw-nx/blob/main/docs/porting-another-game.md); [project8 README](https://github.com/R-drg/project8-sw); [GH3 PLAN T16](https://github.com/guillermedev75/gh3-nx/blob/main/.harness/gh3-marco-c/PLAN.md)

**CPU do renderer (ring thread):**

- **Três work slots** com command buffers, fences, upload buffer de 64 MB e read-back buffers por slot, para a CPU gravar o próximo frame enquanto a GPU desenha o atual. — [nfsmw native-renderer](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md)
- **Sem alocação, log ou query pool reset no ring thread.** No nfsmw: resets de query pool custavam 1,2 ms/frame; logs formatados no ring thread foram movidos; contadores de um único thread não podem ser atômicos (Cortex-A57 não tem `fetch_add` rápido; ~83.000 pacotes/frame custavam 1,1 ms). — [nfsmw native-renderer](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md)
- **Fingerprint de textura + untiling em worker.** Comparar hash dos bytes só quando muda, com frequência decrescente até 1/32 frames; no PC caiu de 66 µs para 4,2 µs por draw no menu. Até 512 MB de cache, blocos de 16 MB, untiling em grupos de 16 bytes com NEON. — [nfsmw native-renderer](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md)
- **Vertex/index deduplicados por conteúdo**, checados no máximo uma vez por frame por range. — [project8 native-renderer](https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md)
- **Não usar extended dynamic state no NVK.** O nfsmw testou EDS 1/2/3: 13% menos binds, mas cada bind subiu de 2,95 para 4,36 µs; perda. — [nfsmw native-renderer](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md)
- **Hooks nas esperas do guest.** O Direct3D do jogo gira em `WAIT_REG_MEM`/vblank/job slot; trocar por sleep/yield que devolve exatamente o valor original libera núcleos. No GH3 há `guest_swap_wait.h`, `guest_job_wait.h` e `guest_waits.h`. — [nfsmw platform-notes §Threads](https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md); [project8 native-renderer](https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md); [GH3 repo](https://github.com/guillermedev75/gh3-nx)
- **Frame pacing antes do present.** Segurar o frame antes de apresentar, não depois, evita o padrão "frame cedo + frame 16,7 ms atrasado" que parece hitch. O project8 marca `thps_pace_before_present` como **implementado, não verificado em hardware**. — [project8 native-renderer §Pacing](https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md)
- **Present em anel.** O GH3 ainda usa **um** command buffer/fence no present, então cada frame espera a GPU terminar o anterior; o T17 do plano troca por 3 slots. — [GH3 PLAN T17](https://github.com/guillermedev75/gh3-nx/blob/main/.harness/gh3-marco-c/PLAN.md); [HANDOFF](https://github.com/guillermedev75/gh3-nx/blob/main/.harness/gh3-marco-c/HANDOFF.md)

**Build e código do jogo:**

- **Chamadas diretas + LTO** no código recompilado; **PGO + function ordering** quando a CPU for o limite. No nfsmw isso levou de ~30,3 para ~34 FPS; no GH3, PGO deu ~31 contra ~34 e foi considerado sem ganho porque o jogo é GPU-bound. — [nfsmw performance-history §6](https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md); [GH3 playbook §8](https://github.com/guillermedev75/gh3-nx/blob/main/docs/porting-playbook.md)
- **Substituir funções quentes do jogo por C++ nativo, com guard.** O nfsmw rodava nativo e original em paralelo nas primeiras 50k–200k chamadas e depois 1/4096, comparando resultados; ganho de 34 para 35,4 FPS. — [nfsmw native-renderer §Game functions](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md)
- **Cuidado com FMA e NaN** ao escrever versões nativas: a ordem das operações precisa ser a mesma do código recompilado, e inputs NaN devem ir para o original. — [nfsmw native-renderer](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md)

### Inferences

- Para o GH3, o maior retorno imediato não é "mais uma otimização de shader", e sim **fechar os dois maiores passes de GPU** (o plano já mostra shadow/bloom valendo ~11 FPS) e **sobrepor o present** (T17), porque o perfil atual mostra threads esperando fence, não calculando.
- O padrão do nfsmw de **prewarm de pipelines + cache em disco** é mais adequado ao GH3 do que o "skip draw" do project8, porque o GH3 já tem problemas de imagem (palco/brightness) e um draw pulado pode ser lido como flash.
- O padrão de **alias MSAA** do GH3 é específico do motor Neversoft e deve ser mantido; ele não aparece no nfsmw.

### Gaps

- Não há números públicos de project8-sw em clocks de fábrica (o README só mede 768 MHz handheld).
- Não há detalhe público do renderer do Pinyon Shift além do README (o código do renderer não está no repo).
- Hell's Gate e AC6 documentam features, mas não publicam breakdown de GPU por passe.
- Vários repositórios (Nocturne, Condemned2, PGR4) não declaram se usam renderer nativo; não assumi.

## Xenia: quais escolhas de renderer e otimizações um port estático pode imitar ou deve evitar

### Takeaway

O valor do Xenia para um port estático é o **modelo de correção**, não o custo de emulação: command processor PM4, semântica de `IssueSwap`/frontbuffer, render target cache com ownership de tiles de EDRAM, tratamento explícito de formatos/gamma/depth/MSAA e tradução de shader com cache. Um port deve copiar as **regras** (quem é dono de qual tile, qual formato, qual resolve) e evitar o **mecanismo** (emular 10 MB de EDRAM e transferir ownership a cada draw).

### Cited Findings

- O Xenos recebe comandos do driver DirectX por um **ring buffer na memória do sistema**; o grosso do processamento está em `src/xenia/gpu/command_processor.cc`. — [docs/gpu.md](https://github.com/xenia-project/xenia/blob/master/docs/gpu.md)
- O `CommandProcessor` implementa os packet types PM4 (Type 0/1/2/3), incluindo `XE_SWAP`, `INDIRECT_BUFFER`, `WAIT_REG_MEM`, `REG_RMW`, `EVENT_WRITE*`, `DRAW_INDX`, `DRAW_INDX_2`, `SET_CONSTANT`, `IM_LOAD`, `IM_LOAD_IMMEDIATE` e `VIZ_QUERY`; `IssueSwap(frontbuffer_ptr, width, height)` é virtual e a implementação não pode assumir que o frontbuffer é um resolve (pode ser textura escrita pela CPU ou carregada de arquivo). — [command_processor.h](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/command_processor.h)
- O Xenia mantém `SwapState` com front/back buffer texture, flag `pending` e dados de backend; também tem gamma ramp de 256 entradas e PWL, e um pós-processamento de swap (`kNone`, `kFxaa`, `kFxaaExtreme`). — [command_processor.h](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/command_processor.h)
- O `RenderTargetCache` tem dois caminhos: `kHostRenderTargets` (render targets hospedeiros + transferência de ownership de tiles) e `kPixelShaderInterlock` (output-merger customizado, só em hosts com raster-ordered writes). O caminho de host RT é descrito como potencialmente "irreparably inaccurate" por depender do fixed-function blending do host, especialmente ao trocar formato. — [render_target_cache.h](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/render_target_cache.h)
- A EDRAM é modelada em **tiles de 80×16 samples**; a chave de render target é `base_tiles` (11 bits), `pitch_tiles_at_32bpp` (8 bits), `msaa_samples`, `is_depth` e `resource_format`. A transferência de ownership copia a região de um RT para outro "bit-exactly from the guest's perspective", com cuidado para NaN em float16, desenhando retângulos com pixel shader que reinterpreta o formato. — [render_target_cache.h](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/render_target_cache.h)
- O Xenia separa ownership de depth `unorm24` e `float24` porque o host pode usar float32 para os dois; converter `f32→f24` e `f32→u24` não é equivalente, e misturar os dois perde precisão. Também trata `k_8_8_8_8_GAMMA` como caso especial: a codificação piecewise não é sRGB, e alguns jogos calculam a codificação no próprio código; trocar por sRGB quebra a imagem. — [render_target_cache.h](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/render_target_cache.h)
- O cache evita trocar de render target desnecessariamente (importante em GPUs tile-based), mantém `last_update_accumulated_render_targets_` e faz "resolve clears" com cutout para não transferir áreas que serão limpas. — [render_target_cache.h](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/render_target_cache.h)
- Resolution scaling no Xenia é **somente inteiro**; o próprio comentário explica que 1,5x (720p→1080p) causa problemas de cobertura de pixel, half-pixel offset e primitivas de 1 pixel usadas em redução de luminância/HDR. — [render_target_cache.h](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/render_target_cache.h)
- O Xenia tem tradução de shader para SPIR-V e para DXBC, além de interpretador e compilador standalone; `docs/gpu.md` documenta `--dump_shaders=path/`, `xe-gpu-shader-compiler` e o Shader Playground. — [docs/gpu.md](https://github.com/xenia-project/xenia/blob/master/docs/gpu.md); [shader_translator.cc](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/shader_translator.cc); [spirv_shader_translator.cc](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/spirv_shader_translator.cc); [dxbc_shader_translator.cc](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/dxbc_shader_translator.cc)
- Flags de correção do Xenia: `half_pixel_offset` (default `true`) existe porque desligar pode quebrar post-processing, mas em alguns jogos foi ignorado e causa desde bordas extremamente brilhantes (4D5307E6) até leitura entre texels (58410954); `non_seamless_cube_map` reproduz o D3D9; `query_occlusion_fake_sample_count` evita hang quando o jogo lê contadores de oclusão. — [gpu_flags.cc](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/gpu_flags.cc)
- O README do Xenia diz que ele é um emulador experimental e aponta para a wiki; `docs/gpu.md` é curto e tem um "TODO: More documentation" em EDRAM. A listagem de `src/xenia/gpu` em 2026-10-19 (truncada) mostra `d3d12/` e `null/`; o `docs/gpu.md` ainda cita `src/xenia/gpu/vulkan/vulkan_gpu_flags.cc`, mas esse arquivo retornou **404** — documentação defasada; não assuma um backend Vulkan atual no upstream sem conferir a árvore. — [README](https://github.com/xenia-project/xenia); [docs/gpu.md](https://github.com/xenia-project/xenia/blob/master/docs/gpu.md); [listagem de src/xenia/gpu](https://github.com/xenia-project/xenia/tree/master/src/xenia/gpu)

### O que imitar

- **Semântica de swap/frontbuffer**: `IssueSwap(ptr, w, h)` com a possibilidade de o frontbuffer não ser um resolve. Ports que só sabem apresentar o resultado de um resolve quebram telas de disclaimer/vídeo.
- **Chave de render target** com base, pitch, formato, MSAA e depth; e **ownership explícito** quando a mesma EDRAM é reusada com outro pitch/formato. É exatamente a classe de bug do GH3 (palco 4x → alvo 1x) e do ReXGlue #508/#513.
- **Formatos de depth separados** (`unorm24` vs `float24`) e conversão ciente de `D24S8`/`D24FS8`, incluindo `(depth24 << 8) | stencil` quando o jogo amostra o resolve como palavra.
- **Gamma/PWL**: usar as funções de referência do Xenia (`PWLGammaToLinear`, `LinearToPWLGamma`, `Float7e3To32`, `Float20e4To32`) em vez de assumir sRGB. — [xenos.cpp](https://github.com/rexglue/rexglue-sdk/blob/main/src/graphics/xenos.cpp)
- **Resolve clear com cutout** e **não trocar RT sem necessidade**.
- **Cache de shader/pipeline** com chave por microcode/fingerprint e dump para debug.
- **Half-pixel offset** como flag de correção, não como detalhe opcional.

### O que evitar

- **Emular EDRAM e ownership em runtime** no Switch: é o que derruba o backend Xenos para 1–5 FPS. O nfsmw mediu 80 µs de CPU por draw só na imitação. — [nfsmw native-renderer](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md)
- **Conversões de formato de render target no host** só para "ficar parecido": o próprio Xenia avisa que blending com formato diferente muda o resultado (clamp de normalized vs float). Prefira manter o formato guest ou fazer a conversão no shader de resolve.
- **Resolution scale fracionário** (1,5x) sem tratar cobertura/half-pixel; o Xenia só suporta inteiro por esses motivos.
- **Compilar shader de forma síncrona no meio do frame**: no NVK, ~59 ms por pipeline. Use cache + prewarm/async e uma política explícita para draw sem pipeline.
- **Assumir que o driver limpa padrões óbvios**: `max(a,a)` vira `fcanonicalize`/`fmul` no Mesa; `saturate(x-0.4)` pode perder o clamp no `FADD32I` do SM50. — [nfsmw shaders.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/shaders.md); [nfsmw mesa.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/mesa.md)

### Inferences

- O bug do ReXGlue #508 (resolve de cor lendo depth RT após memexport) e o #513 (clear de depth "roubando" ownership de cor) são a versão ReXGlue do problema que o `RenderTargetCache` do Xenia tenta resolver. Um port que reimplementa o cache sem as regras de ownership repete esses bugs.
- O GH3 já tropeçou na mesma classe: o palco 4x de pitch P desenhado e depois lido como alvo 1x de pitch 2P; sem `AliasMsaa`/`SyncAliasTarget`, o alvo 1x nunca recebia o palco e só as luzes aditivas apareciam (branco). — [GH3 playbook §7](https://github.com/guillermedev75/gh3-nx/blob/main/docs/porting-playbook.md)

### Gaps

- Não há uma especificação pública do `RenderTargetCache` além dos comentários do header; os detalhes de implementação (D3D12/Vulkan) estão no `.cc` e não foram lidos linha a linha.
- Não consegui confirmar o estado atual do backend Vulkan do Xenia upstream (arquivo citado no doc retornou 404).
- Não há medição pública de "quanto custa" cada regra do Xenia em um port estático; os números vêm dos ports ReXGlue.

## Listas da comunidade e projetos relevantes para render/performance

### Takeaway

Não existe uma lista canônica mantida de "projetos de recompilação de Xbox 360". O índice de fato é o **GitHub Topics**: `rexglue` tinha 66 repositórios e `static-recompilation` 367 em 2026-10-19; a busca da API por "rexglue" retornava 238 repositórios (incluindo forks e menções). O topic `xbox-360-recompilation` estava vazio. Para lições de render, os repositórios com `docs/` de performance valem mais que a contagem de estrelas.

### Cited Findings

- `github.com/topics/rexglue`: 66 repositórios públicos em 2026-10-19, ordenados por estrelas; os primeiros são Pinyon Shift (790), DownpourRecomp (312), ForzaHorizon1-ReImagined (235), Condemned2Recomp (234), PGR4-Recomp (195), DPRecomp (166), WWE SVR 2011 (104), FM4-Recomp (103), Estacado/The Darkness (87), Lost Odyssey (84), Army of Two (80/78), DBZ Burst Limit (76), Skate 2 (68), Asura's Wrath (53), GoldenEye 007 (44), Geometry Wars 2 (43), Project8Recomp (41), Wet (39), Perfect Dark Zero (39). — [topic rexglue](https://github.com/topics/rexglue)
- `github.com/topics/static-recompilation`: 367 repositórios, mas mistura PS2, PSP, Switch e Xbox 360; o primeiro é PS2Recomp (3.601 estrelas), que não é 360. — [topic static-recompilation](https://github.com/topics/static-recompilation)
- O topic `xbox-360-recompilation` não tem nenhum repositório público associado. — [topic xbox-360-recompilation](https://github.com/topics/xbox-360-recompilation)
- A API de busca do GitHub retornou 238 repositórios para `rexglue` e 42 para `project8-sw` (a maioria homônimos de Swift), confirmando que busca por nome é ruidosa e que o topic é um filtro melhor. — [API search rexglue](https://api.github.com/search/repositories?q=rexglue&sort=stars&order=desc&per_page=30); [API search project8-sw](https://api.github.com/search/repositories?q=project8-sw&sort=stars&order=desc&per_page=20)
- Para render/performance, os repositórios com documentação técnica são: nfsmw-nx (`docs/performance-history.md`, `native-renderer.md`, `shaders.md`, `mesa.md`, `platform-notes.md`), project8-sw (`docs/native-renderer.md`, `known-issues.md`, `SWITCH_PORT_ANALYSIS.md`), DownpourRecomp (`docs/v1.0-performance.md`, `chromatic-noise-fix.md`), AC6Recomp (`docs/RENDERER_ARCHITECTURE.txt`) e Hell's Gate (`docs/NATIVE_RENDERER_DESIGN.md`). — [nfsmw docs](https://github.com/StevensND/nfsmw-nx/tree/main/docs); [project8 docs](https://github.com/R-drg/project8-sw/tree/main/docs); [Downpour README](https://github.com/LittleBitUA/DownpourRecomp); [AC6 README](https://github.com/sal063/AC6_recomp); [Hell's Gate README](https://github.com/florinp93/hells-gate-recomp)
- O README do nfsmw aponta explicitamente para `docs/porting-another-game.md` como guia para quem vai portar outro jogo 360 para Switch; o do project8 aponta para `docs/NFSMW_NX_REUSE_ANALYSIS.md`, que documenta o que foi reusado. — [nfsmw README](https://github.com/StevensND/nfsmw-nx); [project8 README](https://github.com/R-drg/project8-sw)

### Inferences

- Para um mapa vivo, vale manter uma lista própria com quatro colunas: **jogo / tipo (recomp|decomp) / alvo / renderer (nativo|Xenos emulado) / doc de performance**. O topic não carrega essas informações.
- Priorize os ports que publicam **medições e causas**: nfsmw-nx, project8-sw, Downpour e AC6. Pinyon Shift é o mais ambicioso em features, mas o renderer não está no repositório.
- Cuidado com a linhagem: SpeedBreaker usa XenonRecomp; Pinyon usa ShiftGlue (fork de ReXGlue); Downpour tem fork próprio do SDK. "ReXGlue" não implica a mesma base de código.

### Gaps

- Não existe lista com status de renderer/performance por projeto; as informações estão dispersas em READMEs e issues.
- Vários projetos não declaram se o renderer é nativo ou emulado; não inferi.
- A API do GitHub tem rate limit e a busca por código exige autenticação; uma varredura exaustiva de forks não foi feita.

## Pitfalls de correção de render e o que pode explicar flashes de cor entre telas

### Takeaway

Flashes breves de cor são, na maioria dos casos documentados, um problema de **ciclo de vida do frame/present** ou de **ownership/resolve de render target**, não de shader. Há evidência direta de "brief flashes" causados por swapchain/drawable no ReXGlue (macOS/MoltenVK), de frontbuffer preto por resolve lendo o depth RT, e de clear de depth que "rouba" a posse da cor. Para o GH3, a hipótese mais forte é: um frame é apresentado antes de todos os draws/resolves terminarem, ou o resolve que alimenta o present lê o alvo errado (ou um alvo ainda não sincronizado pelo alias MSAA).

### Cited Findings

- **ReXGlue PR #487 (macOS/MoltenVK):** no port de Ridge Racer 6, "a game that produced audio and accepted controller input while its window displayed **changing solid colors and brief flashes**". A causa foi a revisão pinada do MoltenVK deixar o drawable Metal em **1×1 após recriação do swapchain**, esticando um pixel pela janela; a correção atualiza o MoltenVK para o fix upstream de swapchain recreation. O PR também habilita primitive restart para o pipeline de triangle strip do guest. — [PR #487](https://github.com/rexglue/rexglue-sdk/pull/487) (fechado em 2026-10-08)
- **ReXGlue issue #508 (Vulkan, host RT path):** o resolve final do frontbuffer lia um **depth/stencil host render target** em vez do color RT que continha o menu; o frontbuffer ficava todo zero e o título apresentava preto. O suspeito é o `RenderTargetCache` transferir ownership de EDRAM para o depth RT em draws memexport que não rasterizam; o caminho FSI funciona. — [issue #508](https://github.com/rexglue/rexglue-sdk/issues/508) (aberto em 2026-10-12)
- **ReXGlue PR #513:** com `execute_unclipped_draw_vs_on_cpu=false`, o `DrawExtentEstimator` devolve 8192 para draws sem clipping; o clear de depth "wrapa" além do tile 2047 e toma ownership de dados de cor abaixo da base. No Tekken 6, o resolve da cena vinha preto e só a HUD aparecia; o workaround é `execute_unclipped_draw_vs_on_cpu = true`. — [PR #513](https://github.com/rexglue/rexglue-sdk/pull/513) (aberto em 2026-10-15)
- **DownpourRecomp:** o "chromatic noise" foi rastreado com RenderDoc até a **transferência de ownership depth→7e3**; a correção é `skip_depth_color_7e3_aliasing_transfers=true`. O changelog também menciona um bug de decode de **7e3 float** (cor de clear HDR com mantissa no bit errado) corrigido por port do upstream v0.10. — [README do Downpour](https://github.com/LittleBitUA/DownpourRecomp)
- **Xenia:** `k_8_8_8_8_GAMMA` usa codificação piecewise que não é sRGB; jogos como 4541080F calculam a codificação no código, e trocar a encoding na memória guest produz resultados visivelmente errados. `2_10_10_10_FLOAT` tem ranges diferentes de float16; `16_16`/`16_16_16_16` têm range -32..32, não -1..1; misturar `unorm24` e `float24` no mesmo float32 do host perde precisão. — [render_target_cache.h](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/render_target_cache.h)
- **Xenia:** `half_pixel_offset` default `true`; desligar pode causar bordas extremamente brilhantes (4D5307E6) ou leitura entre texels (58410954). — [gpu_flags.cc](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/gpu_flags.cc)
- **ReXGlue flags:** `use_fuzzy_alpha_epsilon` existe para "prevent flickering on NVIDIA graphics cards"; `native_2x_msaa` e `gamma_render_target_as_unorm16` são defaults do caminho emulado. — [flags.cpp](https://github.com/rexglue/rexglue-sdk/blob/main/src/graphics/flags.cpp)
- **nfsmw (relato de usuário, sem causa confirmada):** "During races, white flashes appear on the screen from time to time for a fraction of a second; ... more frequently on the right-hand side". O mantenedor não diagnosticou a causa na thread; a resposta foi sobre versão/tradução fan. — [issue #11](https://github.com/StevensND/nfsmw-nx/issues/11) (2026-09-30)
- **nfsmw (relato de usuário):** paredes de túnel e prédios "flicker black and become dark"; o thread não fecha causa. — [issue #4](https://github.com/StevensND/nfsmw-nx/issues/4)
- **project8-sw:** cutscenes Bink mostram o primeiro frame "partly decoded (green and magenta blocks)" e congelam; o renderer recebe os frames de vídeo (um plano de brilho 1280×720 e dois planos de cor 640×360), então a causa provável é o caminho de áudio que controla o clock do Bink, não o renderer. — [docs/known-issues.md](https://github.com/R-drg/project8-sw/blob/main/docs/known-issues.md)
- **project8-sw:** resolve copiado para uma textura **maior** que a amostrada fez o skatista sair duplicado com halo; resolves agora são copiados exatamente no tamanho amostrado. Também: liberar memória de GPU enquanto um frame dividido em vários submissions ainda a usava causou tela preta e fechamento. — [docs/known-issues.md](https://github.com/R-drg/project8-sw/blob/main/docs/known-issues.md)
- **Xenia:** o frontbuffer de swap **não é necessariamente um resolve**; pode ser textura escrita pela CPU ou carregada de arquivo (o disclaimer do começo de 4D530AA4 é exemplo). Um port que assume "sempre resolve" pode apresentar lixo/clear color. — [command_processor.h](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/command_processor.h)
- **nfsmw:** metade dos frames era descartada porque um diálogo invisível de achievements se registrava como UI drawer; com um drawer, o presenter pintava da UI thread e fundia frames. Isso não é flash de cor, mas é a mesma classe de "frame errado chegando à tela". — [nfsmw performance-history §4](https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md)
- **GH3:** tela preta no macOS com `immediate`/fullscreen foi rastreada ao stall do `CAMetalLayer nextDrawable` no MoltenVK; a solução foi janela + FIFO. Redimensionar a janela ainda deixa a tela preta. — [docs/porting-playbook.md §6](https://github.com/guillermedev75/gh3-nx/blob/main/docs/porting-playbook.md); [README](https://github.com/guillermedev75/gh3-nx)
- **GH3:** o palco 4x de pitch P desenhado e depois lido como alvo 1x de pitch 2P ficava branco; o conserto foi `AliasMsaa()` + `SyncAliasTarget()` com `vkCmdBlitImage` NEAREST 2x2, chamado também em `Draw()`. — [docs/porting-playbook.md §7](https://github.com/guillermedev75/gh3-nx/blob/main/docs/porting-playbook.md)

### Síntese para o sintoma do GH3 ("flashes de cor entre telas")

**[inferência]** Ordem de probabilidade, com o que verificar em cada caso:

1. **Present/swapchain antes da hora.** Um frame é apresentado com o command buffer/fence errado, ou o swapchain image é reusado enquanto a GPU ainda lê. Evidências: o GH3 ainda usa **um** command buffer/fence no present (T17 pendente); o ReXGlue #487 mostra flashes por drawable 1×1 após recriação de swapchain; o nfsmw documenta frames fundidos pelo presenter. *Teste:* forçar FIFO, desligar o present ring/thread, esperar a fence antes do present e capturar 10 frames em volta da transição.
2. **Resolve lendo o alvo errado (depth vs color).** Um clear de depth ou um draw memexport muda o ownership e o resolve final pega o depth RT; o frame sai preto/sólido. Evidências: ReXGlue #508 e #513; Xenia separa ownership de depth unorm24/float24. *Teste:* logar `resolve src/dst`, `RT key` e `clear` por frame; comparar com RenderDoc.
3. **Resolve/alias MSAA fora de sincronia.** O GH3 já teve o palco branco por isso; se a transição de tela limpa/reusa o alvo 1x antes do `SyncAliasTarget`, um frame pode mostrar o conteúdo antigo ou o clear. *Teste:* desligar o alias/MSAA e ver se o flash muda de cor ou desaparece; comparar `alias_seq`.
4. **Formato/gamma/7e3 errado.** Um clear color HDR decodificado como 7e3 com mantissa errada, ou um `k_8_8_8_8_GAMMA` tratado como sRGB, produz cor sólida errada por um frame. Evidências: Downpour 7e3; Xenia gamma; ReXGlue `gamma_render_target_as_unorm16`. *Teste:* capturar o clear color e o resolve em formato linear; comparar com o emulado.
5. **Video/fullscreen plane.** Se o flash acontece em telas com vídeo de fundo (menus do GH3 têm vídeos), o caminho de upload YUV/planos pode estar um frame atrasado ou com formato trocado. Evidências: project8 recebe planos 1280×720 + 2×640×360 e tem blocos verde/magenta; o GH3 tem caminho de vídeo próprio. *Teste:* congelar o vídeo e ver se o flash some; logar `frame counter`/`pts` do vídeo.
6. **Draw pulado por pipeline não pronta.** O project8 pula draws até o pipeline ficar pronto; isso deixa objetos ausentes e pode expor o clear color por um frame. O nfsmw evita com prewarm. *Teste:* logar `skipped by reason` e prewarm os pipelines da tela.
7. **Alpha test/fuzzy alpha no NVIDIA.** O ReXGlue tem flag específica para flicker de alpha em NVIDIA; no Switch o NAK/SM50 também tem bugs de clamp. *Teste:* ligar `use_fuzzy_alpha_epsilon` e comparar; testar o mesmo shader no Mac/MoltenVK.

**Plano de instrumentação mínimo [inferência]:**

- Logar por swap: `frame`, `frontbuffer_ptr`, `w×h`, `format`, `source` (resolve/CPU/texture), `resolve src/dst`, `RT key`, `clear color`, `alias_seq`, `fence wait ms`, `present mode`.
- Capturar 10 frames antes/depois da transição com `gameplay_capture.py` e comparar com o emulado; um flash de um frame aparece como um pico de diferença de cor média.
- Repetir com `gh3_native_msaa=false`, `gh3_native_skip_shadows/bloom` on/off, present ring 1 vs 3, e `vblank_late_no_wait` on/off; mudar uma variável por corrida.
- Se o flash for visível no Mac/MoltenVK também, priorizar present/swapchain; se for só no Switch/NVK, priorizar resolve/ownership/NAK.

### Gaps

- Não há issue pública do GH3-nx descrevendo "flashes de cor entre telas"; o sintoma é do contexto do usuário, não um bug documentado.
- O nfsmw #11 relata white flashes, mas o mantenedor não confirmou causa; não trate como diagnóstico.
- Não há captura/medição do GH3 que isole o flash; a lista acima é um plano de investigação, não uma conclusão.
- Não consegui verificar se o GH3 usa vídeo de fundo nas telas onde o flash aparece; isso precisa ser confirmado no log de comandos.
