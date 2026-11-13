# Otimizações de render 360 → Switch (Tegra X1 / NVK)

Notas de pesquisa para o GH3-nx — renderer Vulkan nativo, 720p, handheld a 460,8 MHz de GPU e 1331,2 MHz de memória.
Foco: o que ainda dá ganho **depois** de 1 sample, `depth24`, scene scale 0.75 e modo desempenho (sem sombra/bloom).

Convenções:
- **medido** = número de log/benchmark na máquina; **estimado** = aritmética ou inferência, marcada como tal.
- Termos técnicos em English (render target, resolve, loadOp, etc.), prosa em pt-BR.
- Números do GH3 vêm de `docs/porting-playbook.md` §8 (Switch Mariko, portátil, CPU 1020 MHz / GPU 460,8 MHz / RAM 1331 MHz).
- Números do nfsmw-nx vêm da documentação pública do port (mesma classe de problema: recompilação estática 360 + NVK).

---

## 1. Onde o tempo vai num Tegra X1 a 720p

### 1.1 Orçamento de hardware

| Item | Switch handheld | Switch docked | Xbox 360 (Xenos) |
|---|---|---|---|
| GPU | GM20B, 2 SMs, 256 cores, 460,8 MHz (boost oficial; padrão 307,2) | 768 MHz | 500 MHz, 240 GFLOPS FP32 |
| FP32 peak | ~236 GFLOPS (256 × 2 × 460,8 MHz) — **estimado pela arquitetura** | ~393 GFLOPS | ~240 GFLOPS |
| Memória | LPDDR4 1331,2 MHz, **21,3 GB/s** | LPDDR4 1600 MHz, 25,6 GB/s | 512 MB GDDR3, 22,4 GB/s + **eDRAM** |
| Memória “de render” | DRAM unificada, **256 KB de L2** | idem | **10 MB eDRAM, 256 GB/s internos**, 32 GB/s para o GPU |
| MSAA | 1/2/4/8 samples (máscara 0xF), sem resolve barato | idem | 4× MSAA, z-buffer e alpha blend “sem custo apreciável” |
| Latência de memória | >400 ns | >400 ns | eDRAM local, muito menor |

Fontes: clocks/bandwidth/L2/latência — [nfsmw platform-notes](https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md); LPDDR4/SoC — [Switchbrew Hardware](https://switchbrew.org/wiki/Hardware); eDRAM/256 GB/s/32 GB/s/4× MSAA — [Wikipedia: Xbox 360 technical specifications](https://en.wikipedia.org/wiki/Xbox_360_technical_specifications); Maxwell GM20B/2 SMs — [Wikipedia: Tegra](https://en.wikipedia.org/wiki/Tegra); máscara MSAA e extensões — [project8-sw Vulkan capabilities](https://github.com/R-drg/project8-sw/blob/main/docs/SWITCH_VULKAN_CAPABILITIES.md).

**Ponto central:** a 460,8 MHz o pico FP32 do GM20B é praticamente o mesmo do Xenos (~236 vs ~240 GFLOPS). O que a Switch perde não é “força bruta de shader”; é **banda** (21,3 GB/s contra 256 GB/s dentro da eDRAM) e a capacidade de fazer 4× MSAA + blend + z-test sem sair do chip. O port não vai “compensar” a arquitetura 360 com mais FLOPs; vai compensar cortando pixels, passes e cópias.

### 1.2 Banda por render target a 1280×720

Cálculo próprio sobre 21,3 GB/s (handheld). 1280×720 = 921.600 pixels.

| Operação | Bytes | ms a 21,3 GB/s |
|---|---:|---:|
| Escrever 1 target RGBA8 (32 bpp) | 3,69 MB | 0,173 |
| Escrever 1 target D32S8 (64 bpp) | 7,37 MB | 0,346 |
| Color + depth D32S8, 1× | 11,06 MB | 0,519 |
| Color 4× MSAA | 14,75 MB | 0,692 |
| Depth D32S8 4× MSAA | 29,49 MB | 1,384 |
| Color + depth 4× MSAA | 44,24 MB | **2,077** |
| Resolve 4× color → 1× (read + write) | 18,44 MB | 0,866 |
| Read+write de um target 32 bpp (blit/CAS) | 7,37 MB | 0,346 |

Orçamento: a 60 FPS, 21,3 GB/s dão **355 MB por frame**; a 30 FPS, 710 MB. Isso equivale a ~96 (60 FPS) ou ~192 (30 FPS) escritas de um target 32 bpp de 720p por frame. Parece muito, mas **não é o gargalo principal do GH3**: o custo medido está em sombrear fragmentos e em passes de tela cheia, não em mover 300 MB.

### 1.3 Medições reais

**GH3-nx (medido, 2026-10-18, `docs/porting-playbook.md` §8):**

| Configuração | FPS mediano | GPU real/frame | Comentário |
|---|---:|---:|---|
| Base (1 sample + 0.75 + depth24) | ~34 | 17–29 ms | renderer CPU 21–39 ms |
| + ZCULL (patch Mesa 0005 + cvar) | ~22 | 25–30 ms | **pior**; desligado |
| Sem sombra e bloom | **~45** | **13 ms** | frames >50 ms: 46–108 → 8 por 10 s |

Por passe (medido): passe principal 720p cor+depth **7–11 ms**; segundo passe 720p **3–5 ms**; resolves **1,5–2 ms**; sombra + bloom **5–10 ms** (pelo ganho ao cortar).

**nfsmw-nx (medido, outro jogo 360, mesma arquitetura de port):**

| Estágio | GPU ms/frame |
|---|---:|
| Scene | 27,2 |
| Shadow maps | 13,6 |
| Copies/resolves | 7,4 |
| Cubemap + blur | 7,0 |
| Post-processing | 3,9 |
| **Total inicial** | **60,2** |
| **Depois das otimizações** | **~23** |

Fonte: [nfsmw performance-history](https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md). Cada resolve custava **3–4 ms** de GPU no início; “resolve without copy” economizou **2,36 ms**; ZCULL rendeu **~2 ms** de scene. Fonte: [nfsmw native-renderer](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md).

### 1.4 Interpretação

- O GM20B é **immediate-mode**: não há tiling/on-chip framebuffer. Todo passe de tela cheia que não cabe nos 256 KB de L2 vai e volta da DRAM. O nfsmw documenta exatamente isso: “passes that cover the whole screen are expensive, because nothing that big fits in 256 KB of L2” — [platform-notes](https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md).
- Em 720p, um target RGBA8 é 3,69 MB; 4× MSAA + depth D32S8 passa de 44 MB, **muito** acima da L2. No 360, os 10 MB de eDRAM também não cabiam 44 MB, por isso o jogo tila em strips; mas o custo de reter cor/depth/MSAA era local e barato.
- O documento do nfsmw é explícito: no primeiro perfil, “it spent its time shading fragments, not moving memory” — [performance-history](https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md). O GH3 confirma: cortar **pixels/passes** (sombra+bloom) deu +11 FPS; cortar cópias daria ~1–2 ms.
- Estimativa de ALU: 236 GFLOPS / 60 FPS = 3,93 GFLOP por frame. Com 1,84 M fragments (720p × 2 de overdraw), sobram ~2.130 FLOP/fragment. É folgado para shaders simples, mas shaders 360 com `pow`, `sin/cos`, normalização e muita textura consomem SFU/TMU e instruções, não só FMA. O gargalo real é **instrução + textura + overdraw**, não o papel de parede de FLOPs.

---

## 2. Técnicas que reduzem banda/pixels

### 2.1 Tabela de decisão

| Técnica | O que reduz | Suporte Tegra X1 / NVK | Efeito esperado no GH3 | Confiança |
|---|---|---|---|---|
| **Dynamic resolution / scene scale agressivo** | pixels sombreados + banda de todos os passes da cena | suportado (é só viewport/scissor/imagem menor) | **maior alavanca restante**; 0.75→0.5 corta ~55% dos pixels da cena | estimado |
| **1× + post-AA (FXAA/CAS)** | samples, depth, resolve | suportado (compute/fullscreen) | troca 4× MSAA por 1× + ~0,3–0,8 ms | medido no GH3 que o caminho de 4× amostras como pixels custa 31–36 ms; post-AA estimado |
| **Depth D24S8 em vez de D32S8** | 64→32 bpp no depth | suportado; cvar `gh3_native_depth24` já **true** | 0,17 ms por passe de 720p; 0,3–0,7 ms no frame | medido que está ligado; ganho estimado |
| **Evitar color 16F/RGBA16F** | 64→32 bpp | suportado | halva banda se algum target HDR for 16F | estimado; auditar formatos |
| **Efeitos em half-res** (bloom, sombra, partículas, reflexos) | pixels de passes secundários | suportado | bloom já é 320×184; sombra pode cair 50% + 30 Hz | estimado |
| **Shadow map menor / update a cada 2 frames** | pixels + draws de sombra | suportado | devolve sombra com 2–4 ms em vez de 5–10 | estimado |
| **Resolve direto / sem cópia / lazy resolve** | read+write de resolves | suportado | resolves medem 1,5–2 ms; eliminar 1 resolve ≈ 0,3–0,8 ms | medido o custo; ganho estimado |
| **Alias 1× (já feito)** | pixels do palco 4× | suportado | evita 4× de shading/banda no palco | medido (T15) |
| **Scissor/render area correto** | pixels fora do alvo | suportado | cap de 2048 rows já evita target inflado | medido (T14) |
| **Overdraw: ordenar front-to-back / batching** | fragmentos sombreados | suportado, mas custa CPU | ganho se overdraw >2–3×; CPU já é 13–39 ms | estimado |
| **Depth prepass** | overdraw | suportado | **provavelmente perde**: dobra banda de depth e não elimina shading de alpha-test | estimado |
| **ZCULL / early-Z** | fragmentos escondidos | ZCULL existe, mas exige patch e teve regressão no GH3 | medido **pior** (34→22 FPS); não repetir sem o pacote completo | medido |
| **Occlusion queries** | draw calls escondidos | suportado | **não vale**: sync/leitura na mesma queue, CPU já limitada | inferência |
| **AFBC / compressão de framebuffer** | banda de cor | **não é alavanca prática**; NVK só expõe `NVK_DEBUG=no_compression` | nenhum ganho esperado | ver §2.6 |
| **FSR 1 / CAS** | melhora qualidade ao renderizar menor | CAS: Vulkan, single pass; FSR1: compute | CAS ~0,3–0,8 ms a 720p; FSR2 não vale | CAS medido em existência; custo estimado |
| **VRS (variable rate shading)** | pixels de shading | **não suportado** (Turing+) | 0 | fonte explícita |
| **FP16 shaders** | 2× ALU | hardware tem, **NAK não emite em SM50**; `shaderFloat16` só Turing+ | 0 | fonte explícita |
| **Texturas BC + mips + aniso baixo** | banda de textura | BC1–BC5 suportado; aniso 2 já configurado | manter; subir aniso custa banda | medido no código; custo estimado |
| **`storeOp = DONT_CARE` em alvo não lido** | writeback | suportado | baixo; em immediate-mode o load/store “quase não importa” | fonte nfsmw |
| **Menos submissions / uma queue** | idle de GPU | 1 queue só | splitting custou 3,6 ms de GPU idle no nfsmw | medido |

### 2.2 Dynamic resolution — a alavanca que sobrou

O GH3 já está em `gh3_native_scene_scale = 0.75` (960×540 para a cena, HUD em 1280×720) — `switch/gh3.toml`. Se o passe principal de 720p custa 7–11 ms e é dominado por shading, reduzir a escala para **0.5–0.65** corta 40–55% dos fragmentos desse passe. Estimativa: **3–5 ms** de GPU, possivelmente mais se o segundo passe também for escalado.

O nfsmw tornou a resolução **automática** (“until the resolution became automatic”) e o project8-sw expõe `thps_native_msaa`, `thps_native_depth24` e um cap de 30 FPS; o padrão da indústria em port de Switch é exatamente esse: resolução interna variável, efeitos em metade da taxa, alvo de 30 FPS — [nfsmw native-renderer](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md), [project8-sw native-renderer](https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md).

Como implementar sem oscilar:
- meça a mediana de GPU (não o frame time de um frame), com histerese de 2–3 frames;
- passos de 0,05 (0.75 → 0.70 → 0.65…), clamp 0.5–0.75;
- alvo 16,7 ms se quiser 60 FPS, 33,3 ms se aceitar 30 FPS estável;
- **não** deixe a resolução mudar a cada frame: o olho vê “pulsar” de nitidez; espere 0,5–1 s no mesmo nível.

Para um jogo de ritmo, 30 FPS **estável** com frame pacing é melhor que 45 FPS variando entre 33 e 50 ms. O nfsmw mediu que a 30 FPS o desvio do que o jogador vê ainda é ~9 ms se o frame time variar 20%; a combinação que zera o desvio é **mediana ~28 ms** (90% dos frames cabendo em 31 ms) **e** swap interval 2 — [nfsmw measuring](https://github.com/StevensND/nfsmw-nx/blob/main/docs/measuring.md).

### 2.3 Formatos de render target

O código já tem o caminho certo, mas vale auditar:

- `ColorFormatToVk`: `k_8_8_8_8` → `R8G8B8A8_UNORM`; `k_16_16_16_16_FLOAT` → `RGBA16F`; `k_16_16_16_16` → `RGBA16_SNORM`. `IsColorFormat64bpp` existe e já é usado para calcular pitch — `src/native/native_renderer.cpp:147–179`.
- `DepthFormatFor`: com `gh3_native_depth24=true`, `kD24S8` → `D24_UNORM_S8_UINT` (32 bpp) e `kD24FS8` (sombra) → `D32_SFLOAT` (32 bpp); sem a flag, tudo `D32_SFLOAT_S8_UINT` (64 bpp) — `src/native/native_renderer.cpp:181–186, 424–434`. **A flag já está ligada** em `switch/gh3.toml`.
- Se algum target de bloom/reflexo estiver em `RGBA16F` sem precisar de HDR real, trocar por `RGBA8` halva a banda. O bloom do GH3 já é 320×184; o custo deve ser pequeno, mas o formato pode ser 16F — vale logar `VkFormat` por target no `gh3_native_gpu_profile`.

Custo do depth: um D32S8 de 720p é 7,37 MB; D24S8 é 3,69 MB. Por passe, 0,17 ms de banda a 21,3 GB/s; com 3–4 passes que tocam depth, 0,5–0,7 ms — **estimado**. Como a flag já está on, o ganho já está no baseline; o item é “não desligar” e verificar se os shaders que amostram `k_24_8` continuam corretos.

### 2.4 Resolves e cópias

O GH3 mede **1,5–2 ms** de resolves por frame. O nfsmw mede **3–4 ms por resolve** no início e economiza 2,36 ms com “resolve without copy” (trocar as imagens em vez de copiar quando o próximo uso é clear). O desenho do GH3 já tem:
- alias 1× do palco para o target 1× que compartilha EDRAM (`native_edram_samples.h`, T15);
- cap de 2048 rows (`gh3_native_alias_cap`, T14);
- resolves por compute para color/depth (`copy_color.cs`, `depth_resolve.cs`).

O que ainda pode render:
- **não resolver** um target que só será limpo no próximo frame (lazy resolve); o nfsmw documenta essa troca;
- resolver **direto para a imagem que será amostrada**, sem destino intermediário;
- para cópia same-size, preferir `vkCmdCopyImage` a `vkCmdBlitImage` (blit faz filtragem e pode ser mais caro);
- evitar `vkCmdResolveImage` — com MSAA desligado ele não é necessário; o custo de um resolve 4× color é ~0,87 ms só de banda (tabela acima);
- depth resolve: se o jogo não amostra depth naquele frame, pular; o GH3 já tem fallback por buffer quando não há pipeline direto (`native_renderer.cpp:1768`).

### 2.5 Overdraw, ZCULL e early-Z

- **ZCULL** é o early depth test hierárquico da NVIDIA. No nfsmw, ligar ZCULL rendeu **~2 ms** de scene e exigiu três correções: informar os tamanhos de ZCULL no `nvkmd_switch_pdev.c`, ligar `nouveau_horizon_channel_bind_zcull()` ao channel Vulkan e **remover `TRANSFER_DST` das depth images** (a imagem que pede `TRANSFER_DST` não pode usar ZCULL). Também é preciso **zerar o buffer de ZCULL** antes de usar `LOAD_ZCULL`, senão a GPU trava — [nfsmw mesa](https://github.com/StevensND/nfsmw-nx/blob/main/docs/mesa.md).
- No GH3, o patch local 0005 + `gh3_native_zcull` **piorou** (34 → ~22 FPS; GPU 25–30 ms). O motivo provável é que o caminho local só liga ZCULL com `loadOp = CLEAR` no primeiro passe (`native_renderer.cpp:2814–2819`), o que força um clear de depth de 720p por frame; o nfsmw diz que, uma vez que a depth image tem a parte de ZCULL, **não precisa de `loadOp = CLEAR`**. Há conflito entre a documentação do nfsmw e o comentário local; sem ler o `nvk_cmd_draw.c` do Mesa local, não dá para saber qual comportamento vale nesta versão. **Conclusão prática: não insistir em ZCULL no GH3 sem antes portar o pacote completo do nfsmw (pdev + channel bind + remoção de TRANSFER_DST) e medir com A/B alternado.**
- **Early-Z** já acontece por padrão quando o shader não escreve depth nem usa `discard`. Shaders com alpha-test/`discard` desligam early-Z; se o GH3 tem muito alpha-test (folhagem, partículas), um **depth prepass** só compensa se o overdraw for muito alto — e ele adiciona um passe de depth de 720p (0,35 ms de banda + geometria). **Estimativa: não compensa no GM20B com 2 SMs; medir antes.**
- **Occlusion queries**: não. A leitura de resultado sincroniza, há uma única queue e a CPU do renderer já é 13–39 ms. O ZCULL faz o equivalente por pixel sem ida e volta.

### 2.6 Compressão (AFBC e a compressão do NVK)

- **AFBC** (ARM Frame Buffer Compression) é uma tecnologia de GPU ARM/Mali; não é uma feature Vulkan e **não encontrei fonte primária de que o NVK no GM20B a use para render targets**. O caminho NVIDIA tem compressão transparente, controlada só por `NVK_DEBUG=no_compression` (“Disables image compression”) — [NVK docs](https://docs.mesa3d.org/drivers/nvk.html).
- O nfsmw observa que uma **dedicated allocation** só traria o benefício de compressão, mas ele “never applies to images with `SAMPLED | TRANSFER_DST` usage”, que é o caso das texturas do port — [nfsmw platform-notes](https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md).
- **Conclusão:** não planeje ganho de AFBC. No máximo, faça um A/B com `NVK_DEBUG=no_compression` para descobrir se alguma imagem já está sendo comprimida (se desligar piorar, estava ativa; se não mudar, não é alavanca). **Não suportado/com exposição não documentada** para render targets no NVK/Tegra X1.

### 2.7 FSR / CAS

- **CAS** (Contrast Adaptive Sharpening) é um passe único, Vulkan, que sharpen e opcionalmente escala; foi desenhado para **Dynamic Resolution Scaling** — [AMD GPUOpen CAS](https://gpuopen.com/fidelityfx-cas/). Custo estimado a 720p: read+write de 7,37 MB = **0,35 ms** só de banda, mais ALU; total **~0,3–0,8 ms**.
- **FSR 1** (EASU + RCAS) é mais caro em ALU (2 passes), estimado **1–2 ms** a 720p no GM20B. **FSR 2** precisa de motion vectors, depth, histórico e passes múltiplos; **não vale** para este port.
- O GH3 hoje sobe a cena por **blit linear** (`scene::BlitExtent`, `native_scene_scale.h`). Se a 0.5–0.65 a imagem ficar muito mole, CAS é a opção mais barata para recuperar nitidez; FSR1 só se sobrar GPU.

### 2.8 VRS e FP16: o que **não** existe

- **VRS**: “Variable rate shading needs Turing too.” — [nfsmw mesa](https://github.com/StevensND/nfsmw-nx/blob/main/docs/mesa.md). GM20B é Maxwell (SM50); **não suportado**.
- **FP16**: o hardware do Tegra X1 faz FP16 em 2×, mas o NAK **não consegue emitir** `HADD2`/`HMUL2`/`HFMA2` para SM50; `nak_nir.c` converte 16-bit float em 32-bit em GPUs anteriores a Turing (SM70). Por isso o NVK só expõe `shaderFloat16` em Turing+ — [nfsmw mesa](https://github.com/StevensND/nfsmw-nx/blob/main/docs/mesa.md). **Não perca tempo convertendo shaders para FP16.**
- **Outras ausências** (probe do project8-sw): `VK_EXT_fragment_shader_interlock` e `VK_EXT_shader_stencil_export` não expostos; geometry shader, `fillModeNonSolid`, dynamic rendering, `VK_KHR_image_format_list`, YCbCr e robustness2 expostos — [project8-sw Vulkan capabilities](https://github.com/R-drg/project8-sw/blob/main/docs/SWITCH_VULKAN_CAPABILITIES.md).

---

## 3. NVK / Mesa / NAK: o que importa na prática

### 3.1 O pacote do nfsmw (mesa-switch)

O port do nfsmw roda Mesa 26.2.1 com o port `danfromtico/mesa-switch` e um patch único, `mesa/mesa-switch-nfsmw.patch`, contra o commit `1a8c1a66d6f`. As mudanças com efeito medido:

| Mudança | Efeito | Fonte |
|---|---|---|
| ZCULL (pdev + channel bind + depth sem `TRANSFER_DST`) | ~2 ms de scene | [mesa.md](https://github.com/StevensND/nfsmw-nx/blob/main/docs/mesa.md) |
| NAK: latência de memória 32 → **200 cycles** no scheduling | separa reads de textura das instruções que os usam; mais reads em voo | idem |
| NAK: `peephole_select` 0 → **8** | converte `if` pequeno em código predicado, evita SSY/BRA/SYNC | idem |
| `nvk_shader.c`: revisão do compilador | invalida pipeline cache/disco depois de mudar NAK | idem |
| Draw path: state writes mais baratos, **38–45% menos rebinds de constant buffer**, prefetch de pipeline | menos CPU por draw (~2.000 draws/frame) | idem |
| Set 4 por diferença | **1 write em vez de 4** por draw | idem |
| `NVK_SWITCH_CPU_WRITE_MEM_UNCACHED` | command buffers/uploads sem cache de CPU; evita clean a cada write | idem |
| Fences só para o que a CPU espera; sem clean de descriptor tables por submit | menos syscalls | idem |
| **Tentativa descartada:** extended dynamic state (EDS 1/2/3) | 13% menos binds, mas bind **2,95 → 4,36 µs**; perda | idem |

O GH3 usa uma versão anterior desse caminho (patch 0005). **Vale portar o pacote de draw path/NAK, não só ZCULL**: o custo de CPU por draw do nfsmw caiu com rebinds e set4; o GH3 tem ~230 draws/frame, mas o renderer CPU de 13–39 ms ainda é relevante quando a GPU cai para 13 ms (modo desempenho). O ganho de GPU é 0; o ganho é não virar CPU-bound a 60 FPS.

### 3.2 NAK: o que checar

- **Spills**: “running out of registers (a spill) is expensive on this GPU”; use `NVK_SHADER_STATS=1` e mantenha `local memory size = 0` — [nfsmw mesa](https://github.com/StevensND/nfsmw-nx/blob/main/docs/mesa.md).
- **`FADD32I` + clamp**: em SM50, `FADD32I` não tem bit de clamp; o NAK já foi corrigido para não usar quando há `.SAT`. Se aparecer cor errada (ex.: “mar preto”), desconfie de clamp descartado — idem.
- **Debug**: `NAK_DEBUG=print,annotate` e `NVK_DEBUG=print` para disassembly/IR; `NVK_DEBUG=no_compression`, `gart`, `coherent` (Tegra) — [NVK docs](https://docs.mesa3d.org/drivers/nvk.html).
- **Shader simplification** foi um dos maiores ganhos do nfsmw: merge de blocos predicados (removeu a maioria dos 1.806 branches), remover `max(a, a)`, não consultar `textureSize` por draw (160 queries → 1 inverse size) — [performance-history](https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md). No GH3, o equivalente é revisar os shaders traduzidos pelo XenosRecomp/DXC: constante folding, branches, `texelFetch` vs `texture`, precisão.

### 3.3 Memória e apresentação (NVK no Horizon)

- **Duas memórias**: type 0 `HOST_CACHED` (precisa `vkFlushMappedMemoryRanges`), type 1 `HOST_COHERENT` (NvMap sem cache). Use uncached para uploads/command buffers — [nfsmw platform-notes](https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md).
- **Toda fence é syscall**; todo `vkQueueSubmit` é IPC. A WSI adiciona um submit vazio por present. Dividir a cena em 2 submissions deixou a GPU **3,6 ms idle** por frame. **Mantenha 1 submission por frame** — idem.
- **Swapchain**: 1 queue, **3 imagens** (pedir 4 dá 3), 1 acquire por vez, só **FIFO e IMMEDIATE**; MAILBOX/FIFO_RELAXED caem para FIFO. IMMEDIATE é `nwindowSetSwapInterval(nw, 0)` e **não rasga**; o compositor continua a 60 Hz — idem.
- **Alocação**: dedicated allocation custa ~1,9 ms de CPU (nvMapCreate 422 µs, MapBufferEx 626 µs); texturas devem vir de blocos de 16–32 MB. Limite de ~4.000 handles NvMap (8 MB de transfer memory do libnx); BO cache do driver limitado a 256 entradas, não a MB — idem.
- **Pipeline**: 59–160 ms por pipeline sem cache; salve `VkPipelineCache` e pré-crie a lista de pipelines em worker. O GH3 já tem `gh3_native_async_pipelines` — idem; [project8-sw native-renderer](https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md).
- **Timestamps**: `timestampPeriod` diz 1 ns, mas no Switch 1 unidade = **1,627 ns**. Multiplique o GPU time bruto por 1,627 antes de comparar — [nfsmw measuring](https://github.com/StevensND/nfsmw-nx/blob/main/docs/measuring.md).
- **Variáveis de ambiente** são lidas na criação da instance/device; não adianta setar depois. `MESA_SHADER_CACHE_DISABLE` se o app já tem pipeline cache; `NOUVEAU_HORIZON_BO_CACHE_MB`; `NVK_SWITCH_PERF_LOG` — [platform-notes](https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md).

### 3.4 Configuração atual do GH3 (para não repetir)

`switch/gh3.toml` já tem: `gh3_native_msaa=false`, `gh3_native_msaa_as_samples=false` (1 sample por pixel de guest, 1/4 dos pixels sombreados), `gh3_native_max_anisotropy=2`, `gh3_native_depth24=true`, `gh3_native_record_thread=true`, `gh3_native_scene_scale=0.75`, `gh3_native_skip_shadows=true`, `gh3_native_skip_bloom=true`, `gh3_switch_gpu_boost=true` (apm `0x92220008` = GPU 460,8 + RAM 1331,2).

O que **não** está ligado e vale testar: dynamic resolution de verdade (não só 0.75 fixo), sombra em half-res/30 Hz, CAS, e o pacote NAK/draw-path do nfsmw.

---

## 4. O que ports nativos de Switch fazem para chegar a 60 FPS a 720p

Evidência primária disponível (ports de recompilação 360, mesma classe do GH3):

**nfsmw-nx**
- alvo 30 FPS (não 60); “a race went from 1–3 FPS to 30 FPS and more”;
- resolução **automática** (dynamic resolution) depois de um período fixo;
- pede `apm` 460,8 MHz / RAM 1331,2 no handheld;
- sombra: PCF 3×3 mais barato, sem vegetação, distância/LOD reduzidos, slope bias;
- “resolve without copy” e um shadow map copy trocado por mínimo de dois;
- shaders traduzidos simplificados (branches, `max(a,a)`, texture size);
- pipeline cache + lista de pipelines pré-criada; frame pacing com swap interval;
- limita aniso, texturas em blocos, vertex upload sem duplicatas.

Fontes: [performance-history](https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md), [native-renderer](https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md).

**project8-sw**
- alvo “steady 30 FPS”;
- `thps_native_msaa=false` (1 sample), `thps_native_depth24`, `thps_native_max_anisotropy=16`, `thps_native_async_pipelines`;
- pacing explícito (`thps_min_frame_vblanks`, `thps_pace_before_present`) porque o jogo não tinha frame pacing;
- 30 FPS cap em vez de tentar 60.

Fonte: [project8-sw native-renderer](https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md).

**Padrão que um port de recompilação 360 pode copiar:**
1. reduzir resolução interna e efeitos antes de mexer em shader;
2. 30 FPS estável com pacing, não 45 variável;
3. sombra/bloom em half-rate ou desligados;
4. eliminar resolves/cópias;
5. pré-aquecer pipelines e evitar stutter de compilação;
6. simplificar shaders traduzidos (o codegen do 360 não é “shader de PC”).

**Lacuna:** não consegui buscar uma análise pública da Digital Foundry nesta sessão (URLs 404 / busca bloqueada). As afirmações sobre jogos AAA de Switch (DOOM, Wolfenstein, Witcher 3 etc.) ficam de fora por falta de fonte verificada; os dois ports acima são a evidência primária usada.

---

## 5. Pitfalls de clears, formatos, vídeo e present (flashes de cor)

Sintomas observados/possíveis no caminho nativo do GH3, com o ponto de código:

| Sintoma | Causa provável | Correção/teste |
|---|---|---|
| **Flash verde/ciano no boot ou ao trocar de estado** | `ClearOutput` pinta um **test colour animado** (verde sobe/desce com o swap) quando `renderer_ready_` é falso — `src/native/gh3_native_system.cpp:1325–1337`. É intencional para o `boot_test.py`, mas vira flash se o primeiro frame nativo demora (pipeline compile, vídeo). | Use **preto** como fallback em produção; deixe o test colour atrás de flag de boot test. Nunca deixe `renderer_ready_` oscilar no meio da sessão. |
| **Primeiro frame preto/branco** | swapchain image com `initialLayout = UNDEFINED` e área não coberta; o present compute só escreve `width×height` — `native_renderer.cpp:4695–4745`. Se a imagem de destino for maior que a fonte, o resto fica indefinido. | Limpe a swapchain inteira antes do primeiro present ou faça o dispatch cobrir a extensão total; garanta que a imagem foi escrita antes de apresentar. |
| **Bordas/faixas coloridas** | `vkCmdDispatch((width+15)/16, (height+7)/8, 1)` cobre só a fonte; o presenter pode escalar/letterbox para uma área maior. | Despache para a extensão do **destino** ou faça clear do destino antes; verifique `display_width/height` vs `source_width/height`. |
| **R/B trocados** | ignorar o swizzle do fetch constant no `VdSwap`. O código já corrige: `guest_swizzle = fetch.swizzle`; sem isso, “red and blue come out swapped” — `native_renderer.cpp:4622–4632`. | Sempre aplicar o swizzle do guest na view do frontbuffer e nos resolve destinations. |
| **Cena lavada / gamma duplo** | swapchain sRGB vs shader que já aplica gamma; gamma ramp aplicada duas vezes ou no slot errado. O present copia `gamma_ramp` para `ramp_mapping + 256*4*slot` e usa um compute — `native_renderer.cpp:4695–4705`. | Um único ponto de aplicação de gamma; prefira swapchain UNORM + ramp no compute, ou sRGB sem ramp. Teste com um gradiente. |
| **Vídeo/intro com blocos verdes/magenta ou cor errada** | frame de vídeo parcialmente decodificado, matriz YUV errada (BT.601 vs BT.709), range limitado vs completo, ou textura de vídeo amostrada como sRGB. O project8-sw documenta exatamente “first frame, partly decoded (green and magenta blocks)” — [known issues via README](https://github.com/R-drg/project8-sw). | Só apresente o frame depois de decodificado; conversão YUV→RGB explícita com matrix/range corretos; não aplique gamma ramp em vídeo já em gamma. |
| **Flicker/ghosting entre resolve e sample** | falta de barrier entre o write do resolve e o first use como textura; `oldLayout = UNDEFINED` descartando conteúdo que ainda é necessário. | `FlushResolveBarrier()` antes de amostrar; nunca use `UNDEFINED` se o conteúdo anterior será lido; `srcAccessMask` correto. |
| **Alvo novo preto, mas o jogo esperava conteúdo** | todo render target novo é limpo para zero (cor) e 1.0 (depth) — `native_renderer.cpp:2700–2726`; se o guest espera “lixo”/conteúdo anterior, o primeiro draw pode parecer um flash. | O código já copia o conteúdo do target antigo quando redimensiona; para targets realmente novos, o clear é o comportamento correto. Não troque para `LOAD` sem init. |
| **Profundidade “vazando” de frame anterior** | depth não limpo no primeiro passe, ou alias target não sincronizado (T15). | `gh3_native_zcull` limpa depth; sem ZCULL, o depth é `LOAD` e depende do clear inicial. Garanta `SyncAliasTarget` para o alvo 1×. |
| **Cena branca/estourada** | bloom antigo somava demais (T9); o modo desempenho desliga e resolve. | Se religar bloom, metade da resolução + clamp; comparar captura com o emulado. |
| **Target 2048 rows / custo inflado** | scissor 8192 do guest contra alvo de 1280; o cap T14 limita a metade das rows. | Manter `gh3_native_alias_cap=true`; sempre limitar `renderArea`/scissor ao extent real. |
| **Frame mostrado 50 ms depois** | IMMEDIATE não rasga, mas o compositor mostra em passos de 16,67 ms; frame de 34 ms aparece por 50 ms. | Pace antes do present (`thps_pace_before_present` no project8); alvo 30 FPS estável; não conte com MAILBOX. |
| **`DONT_CARE` no primeiro uso** | conteúdo indefinido lido como textura ou apresentado. | Use `CLEAR` no primeiro uso; `DONT_CARE` só para alvo que nunca será lido. |

---

## 6. Plano priorizado para o GH3 (com aceite binário)

Ordem por relação ganho/risco. Cada item deve ser medido com A/B alternado na mesma música/trecho, como o nfsmw exige — [measuring](https://github.com/StevensND/nfsmw-nx/blob/main/docs/measuring.md).

| # | Mudança | Ganho esperado | Como medir | Aceite |
|---|---|---|---|---|
| 1 | **Dynamic resolution** 0.5–0.75 com histerese (alvo 16,7 ou 33,3 ms) | **3–6 ms** GPU (estimado) | `gh3_native_gpu_profile`, FPS mediano, frames >50 ms | +10% FPS mediano ou −2 ms GPU, sem aumentar frames >50 ms |
| 2 | **Sombra half-res + update 30 Hz** (se quiser recuperar sombra) | 2–4 ms vs 5–10 ms (estimado) | perfil por passe | sombra ligada com ≤2 ms de custo extra vs modo desempenho |
| 3 | **Auditar resolves**: lazy resolve, copiar direto, pular depth resolve não usado | 0,3–0,8 ms (medido o total de 1,5–2 ms) | contador `resolves` + perfil | −1 resolve/frame ou −0,5 ms |
| 4 | **Shaders/NAK**: merge de branches, remover `textureSize`, checar spills; aplicar patch NAK 200 cycles + peephole 8 | 1–3 ms (estimado; nfsmw mediu ganhos grandes) | `NVK_SHADER_STATS=1`, perfil por passe | `local memory = 0`, −1 ms no passe principal |
| 5 | **Draw path nfsmw** (rebinds, set4 diff, uncached writes) | CPU −10–20% (nfsmw: 38–45% menos rebinds) | renderer CPU/frame | renderer CPU ≤ baseline com mesmo FPS |
| 6 | **CAS** depois da resolução reduzida | +0,3–0,8 ms, nitidez | captura + FPS | custo ≤0,8 ms e melhora visível vs blit linear |
| 7 | **ZCULL completo** (pdev + channel bind + depth sem TRANSFER_DST, sem exigir CLEAR) | +2 ms no nfsmw; **−12 FPS no GH3 atual** | A/B alternado | só ligar se >5% e sem regressão; caso contrário, manter off |
| 8 | **60 FPS** | exige ~25% além dos 45 atuais | frame time | só depois de 1–5; senão 30 FPS pacing |

**Não fazer:** VRS, FP16, AFBC, occlusion queries, depth prepass sem medir, FSR2, 4 imagens de swapchain, MAILBOX, separar submissions, EDS 1/2/3.

---

## 7. O que não é suportado / não é alavanca (resumo)

| Recurso | Status no Tegra X1 / NVK | Fonte |
|---|---|---|
| VRS (`VK_KHR_fragment_shading_rate`) | **não**; Turing+ | [nfsmw mesa](https://github.com/StevensND/nfsmw-nx/blob/main/docs/mesa.md) |
| FP16 shaders (`shaderFloat16`) | hardware 2×, mas **NAK não emite em SM50**; exposto só Turing+ | idem |
| AFBC para render targets | **sem exposição documentada**; compressão NVK é transparente (`no_compression`) | [NVK docs](https://docs.mesa3d.org/drivers/nvk.html); [nfsmw platform-notes](https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md) |
| Transfer queue separada | **não**; 1 queue; Turing+ | [nfsmw platform-notes](https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md) |
| 4+ swapchain images | **não**; 3 | idem |
| MAILBOX / FIFO_RELAXED | caem para FIFO | idem |
| Fragment shader interlock / stencil export | **não expostos** | [project8-sw Vulkan capabilities](https://github.com/R-drg/project8-sw/blob/main/docs/SWITCH_VULKAN_CAPABILITIES.md) |
| Geometry shader / dynamic rendering / YCbCr / robustness2 | expostos | idem |
| MSAA 1/2/4/8 | exposto; o caminho de 4× amostras como pixels no GH3 custou 31–36 ms e foi desligado | idem; `switch/gh3.toml` |
| Compressão de textura BC1–BC5 | suportado (tabela de formatos do port) | `src/native/native_textures.cpp` |
| `VK_EXT_memory_budget` | usado pelo port (480–515 MB de 1,4–1,5 GB) | [nfsmw platform-notes](https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md) |

---

## 8. Fontes e lacunas

### Fontes primárias usadas

- Mesa/NVK docs — hardware support, `NVK_DEBUG=no_compression`, `coherent` em Tegra, `NAK_DEBUG`: https://docs.mesa3d.org/drivers/nvk.html
- nfsmw-nx — driver/ZCULL/NAK/patch de draw path: https://github.com/StevensND/nfsmw-nx/blob/main/docs/mesa.md
- nfsmw-nx — arquitetura do renderer, resolves, pipelines, texturas: https://github.com/StevensND/nfsmw-nx/blob/main/docs/native-renderer.md
- nfsmw-nx — histórico medido (60,2 → 23 ms; 30 FPS; dynamic resolution): https://github.com/StevensND/nfsmw-nx/blob/main/docs/performance-history.md
- nfsmw-nx — clocks, bandwidth, L2, immediate-mode, apresentação, memória, NVK: https://github.com/StevensND/nfsmw-nx/blob/main/docs/platform-notes.md
- nfsmw-nx — método de medição, timestamps ×1,627, frame pacing: https://github.com/StevensND/nfsmw-nx/blob/main/docs/measuring.md
- nfsmw-nx — glossário (EDRAM, resolve, ZCULL): https://github.com/StevensND/nfsmw-nx/blob/main/docs/glossary.md
- project8-sw — matriz de capacidades Vulkan no Tegra X1: https://github.com/R-drg/project8-sw/blob/main/docs/SWITCH_VULKAN_CAPABILITIES.md
- project8-sw — settings do renderer nativo (MSAA off, depth24, pacing): https://github.com/R-drg/project8-sw/blob/main/docs/native-renderer.md
- Switchbrew — hardware/LPDDR4/SoC: https://switchbrew.org/wiki/Hardware
- Wikipedia — Xbox 360: eDRAM 10 MB, 256 GB/s, 32 GB/s, 4× MSAA, 240 GFLOPS: https://en.wikipedia.org/wiki/Xbox_360_technical_specifications
- Wikipedia — Tegra X1/GM20B, 2 SMs, 20 nm, ASTC: https://en.wikipedia.org/wiki/Tegra
- AMD GPUOpen — CAS, Vulkan, single pass, DRS: https://gpuopen.com/fidelityfx-cas/
- Código local do GH3: `switch/gh3.toml`, `docs/porting-playbook.md` §8, `src/native/native_renderer.cpp` (cvars, formatos, clears, swap), `src/native/native_scene_scale.h`, `src/native/gh3_native_system.cpp` (ClearOutput/Present), `src/native/native_edram_samples.h`.

### Lacunas / conflitos

- **AFBC**: não achei fonte primária de que o NVK no GM20B exponha AFBC para render targets. A conclusão “não é alavanca” é inferência a partir do único knob documentado (`no_compression`) e da nota do nfsmw sobre `SAMPLED | TRANSFER_DST`. Tratar como **não suportado na prática**, não como fato de hardware.
- **ZCULL**: o comentário local diz que o NVK só liga ZCULL com `loadOp = CLEAR` (`native_renderer.cpp:2814`), enquanto o nfsmw afirma que, depois que a depth image tem ZCULL, **não precisa de CLEAR**. O conflito não foi resolvido sem ler o `nvk_cmd_draw.c` da versão local do Mesa. O resultado medido no GH3 (pior) é o dado que manda.
- **Digital Foundry / jogos AAA**: as URLs tentadas retornaram 404 e a busca ficou bloqueada; não usei números de ports comerciais. As recomendações de “o que ports fazem” vêm dos dois ports de recompilação 360 citados.
- **GH3 por passe**: os números de 7–11 / 3–5 / 1,5–2 / 5–10 ms são de 2026-10-18; não há uma nova medição depois de dynamic resolution/CAS/sombra half-res.
- **Depth24 A/B**: a flag está ligada, mas não há log de A/B ligado/desligado no GH3; o ganho de 0,3–0,7 ms é estimado pela banda, não medido.
- **FSR/CAS no GM20B**: não há benchmark publicado; os custos são estimativas de banda + ALU, não medições.
