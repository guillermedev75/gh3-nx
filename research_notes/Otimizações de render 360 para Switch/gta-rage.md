# GTA IV/V, RAGE e o que transfere para o GH3-nx

**Escopo e método.** Pesquisa feita em 2026-10-19 por leitura de READMEs, documentação de arquitetura e API pública do GitHub; nenhum build ou execução de GTA foi feita nesta sessão. Marcadores usados: **[verificado]** = afirmação explícita em README/docs/código público; **[não verificado]** = relato de terceiro/guia comunitário sem reprodução independente; **[inferência]** = conclusão minha a partir das fontes; **[lacuna]** = não encontrei fonte confiável.

Contexto do GH3-nx: static recompilation ReXGlue (PowerPC → C++ → ARM64), renderer Vulkan nativo sobre NVK, GPU-bound em clocks de fábrica. As lições abaixo são avaliadas contra esse alvo, não contra um PC.

---

## 1. Existem static recompilation, decompilation ou ports de GTA IV e GTA V?

### Takeaway

**GTA IV tem, sim, static recompilation:** LibertyRecomp (PC), Theft4 (iOS/iPadOS), Theft4-Android e ProjectTM (Nintendo Switch, via Theft4-NX). **GTA V também tem:** SanRecomp (Xbox 360 → PC, estágio inicial, sem renderer) e um port comunitário de GTA V para Switch que **não é static recompilation** — é um pacote de build + patcher de assets da versão PC, distribuído como NSP, sem repositório público de motor. **Não encontrei projeto público de decompilation completa de GTA IV nem de GTA V** no estilo re3/reVC; re3/reVC são GTA III/Vice City (RenderWare), não RAGE. Não há port oficial de GTA IV/V para Switch; o único RAGE oficial no Switch é Red Dead Redemption.

### Cited Findings

**GTA IV**

- **LibertyRecomp** — “unofficial PC port of the Xbox 360 version of Grand Theft Auto IV created through the process of static recompilation”; runtime é um fork do ReXGlue SDK, com tradução PowerPC → C++ e Xenos shader translation. Status declarado: **early development, NOT meant for public use**; renderer/GPU pipeline e reverse engineering do RAGE ainda “in progress”. — [README](https://github.com/OZORDI/LibertyRecomp) **[verificado]**
- **Theft4** — static AOT recompilation do GTA IV Xbox 360 para **ARM64 iOS/iPadOS**, sem JIT; usa LibertyRecomp/ReXGlue como base. A versão 0.3.2 tem renderer Metal nativo, não só Vulkan/MoltenVK; o README reporta teste em M5 iPad a **2416×1359 sem upscaling**, mas explicitamente **não** estabelece 30 FPS travado em todos os devices/cenas. — [README](https://github.com/KoreanSeats1/Theft4) **[verificado]**
- **Theft4-Android** — port Android arm64-v8a do Theft4/LibertyRecomp, SDL3 + Vulkan, “bring your own game files”; repositório pequeno, 1 estrela, último push 2026-10-02. — [repo](https://github.com/deivid22srk/Theft4-Android) **[verificado como existência; não verificado como jogável]**
- **LibertyRecomp-MidRange-Edition** — port Android para GPUs Mali (testado em OPPO A79 5G), baseado em LibertyRecomp; 1 estrela, último push 2026-10-15. — [repo](https://github.com/emmahiguita/LibertyRecomp-MidRange-Edition) **[verificado como existência; não verificado como jogável]**
- **ProjectTM** — tentativa de levar GTA IV Xbox 360 ao **Nintendo Switch** por static recompilation, atualmente usando **Theft4-NX** como runtime. Status 0.2.2: chegou a “Starting New Game” e “The Cousins Bellic” no Switch, com renderização e áudio gerados pelo jogo; **ainda não é um port jogável de forma confiável**. O 0.2.2 inclui um “Vulkan/NVK compatibility fix for the Xenos color format that caused 0.2.1 to present black frames”, mas o fix ainda precisava de validação em hardware. — [README](https://github.com/iy26adchak/ProjectTM) **[verificado]**

**GTA V**

- **SanRecomp** — “GTA V Xbox 360 Static Recompilation Project”, usando **XenonRecomp** para PowerPC → C++ e visando Windows/Linux/macOS. Status declarado: early development; **XenosRecomp/HLSL, RAGE reverse engineering, GPU/rendering, áudio, saves e input ainda “in progress”**. Último push 2026-01-05; 37 estrelas. — [README](https://github.com/OZORDI/SanRecomp) **[verificado]**
- **GTA V Switch “port”** — existe uma cena comunitária que distribui um NSP com Title ID `0100b00b51230000`. O guia descreve: usar uma instalação **GTA V Legacy PC**, aplicar um **platform pack** e um **asset patcher** para Switch, repackar em NSP e instalar; os repos abertos são ferramentas de mod (DLC manager, ScriptHookV-NX, RPF radio editor), **não o motor**. — [guia](https://geekmaxxer.github.io/GTA5-NX-Tutorial/), [toolbox](https://github.com/niceboat0/GTA5-Switch-Toolbox), [ScriptHookV-NX](https://github.com/Geekmaxxer/ScriptHookV-NX) **[verificado que existe e que o fluxo é PC→Switch; não verificado como port nativo independente]**
- **[inferência]** Pelo fluxo do guia (build pack + patcher de assets PC, sem menção a XenonRecomp/ReXGlue/`default.xex`), o port de GTA V para Switch **não é static recompilation do build 360**. É um port comunitário baseado em código/assets da versão PC, distribuído como NSP; a proveniência exata do binário do motor não está documentada publicamente. Não confundir com SanRecomp.
- **RAGE oficial no Switch** — a engine RAGE lista Nintendo Switch entre as plataformas e Red Dead Redemption (jogo RAGE) tem versão Switch; isso prova que a Rockstar já levou RAGE ao Switch, mas **não** é GTA IV/V. — [Wikipedia RAGE](https://en.wikipedia.org/wiki/Rockstar_Advanced_Game_Engine) **[verificado]**

**Decompilation**

- **re3** = decompilation de **GTA III** (e base para Vice City); **reVC** = Vice City. O repo upstream `GTAModding/re3` respondia **HTTP 451** (Unavailable For Legal Reasons) na API do GitHub em 2026-10-19; ainda existem mirrors, p.ex. `hottabxp/re3` (495 estrelas) e forks. — [repo upstream](https://github.com/GTAModding/re3), [mirror](https://github.com/hottabxp/re3) **[verificado]**
- Busca de repositórios no GitHub por `gta iv recompilation` retornou Theft4/Theft4-Android; por `gta v decompilation` não retornou projeto de decomp de GTA V; os hits de “re3” são III/VC. — [busca IV](https://api.github.com/search/repositories?q=gta+iv+recompilation), [busca V](https://api.github.com/search/repositories?q=gta+v+decompilation) **[verificado em 2026-10-19]**
- **OpenRW** é uma reimplementação open-source de **GTA III** (RenderWare), GPLv3, para Linux/macOS/Windows/BSD; exige cópia legítima do jogo PC. Não é RAGE nem 360. — [README](https://github.com/rwengine/openrw) **[verificado]**

**Resumo de status**

| Projeto | Jogo | Alvo | Técnica | Status em 2026-10-19 |
|---|---|---|---|---|
| LibertyRecomp | GTA IV | PC (Win/Linux/macOS) | static recomp (ReXGlue) | early, renderer em progresso |
| Theft4 | GTA IV | iOS/iPadOS ARM64 | static AOT (ReXGlue) | 0.3.2, Metal nativo, não é 30 FPS garantido |
| Theft4-Android | GTA IV | Android arm64 | static AOT + Vulkan | inicial, não verificado |
| ProjectTM | GTA IV | Switch (NVK) | static AOT via Theft4-NX | 0.2.2, chega ao início do jogo, não confiável |
| SanRecomp | GTA V | PC | static recomp (XenonRecomp) | early, sem GPU/áudio/saves |
| GTA V NX “port” | GTA V | Switch | port PC + asset patcher (não recomp) | NSP comunitário, motor sem repo público |
| re3/reVC | GTA III/VC | PC/mobile | decompilation | upstream 451; mirrors/fork |
| OpenRW | GTA III | PC | reimplementação | GPLv3, ativo |

### Inferences

- O padrão que funcionou para GTA IV é exatamente o do GH3-nx: ReXGlue/XenonRecomp + runtime de compatibilidade + backend gráfico específico da plataforma. ProjectTM é o caso mais próximo e mais útil: mesmo jogo-base, mesmo tipo de runtime e **mesmo alvo Switch/NVK**.
- GTA V é um caso pior para static recomp porque o SanRecomp ainda não tem pipeline gráfico; não há evidência pública de que o RAGE de GTA V já tenha sido renderizado corretamente por esse projeto.
- O port de GTA V para Switch, se realmente existe como NSP jogável, é um caminho **diferente** (código PC + assets patcheados). Ele não fornece um “recompiler de RAGE” reutilizável e não reduz o trabalho de um port 360→Switch.

### Gaps

- **[lacuna]** Não há nenhuma captura de frame pública e reproduzível de GTA V rodando no SanRecomp; o README só declara o estágio.
- **[lacuna]** Não há fonte primária sobre a origem do binário do “GTA V Switch port”; o guia não diz se é build do código-fonte PC vazado, do código Android inexistente, ou outra coisa. Não afirmo qual é.
- **[lacuna]** Não encontrei port nativo móvel de GTA V (iOS/Android) comparável ao Theft4; execuções via Winlator/streaming não contam como port nativo.
- **[lacuna]** Não há decompilation pública de GTA IV/V; se existir algum esforço privado, não é verificável.

---

## 2. Que técnicas de render do RAGE são relevantes para um port 360→Switch?

### Takeaway

O RAGE de GTA V é **deferred**, com G-buffer de 5 MRTs, culling/LOD, CSM, SSAO, reflexos (cubemap → dual-paraboloid e planar), pós-processamento pesado e ~4.155 draw calls por frame. O RAGE de GTA IV é mais antigo; a engenharia reversa dos ports mostra lighting em MRT, depth reverso e reflexos, mas o modelo exato (forward vs deferred) não está confirmado por fonte primária — não há um “GTA IV graphics study” público equivalente. Para o GH3-nx, o que transfere **não é o deferred nem o async compute**: é o **frame dependency graph** (ordem de passes, aliases, resolves, depth/color contracts), as técnicas de **redução de overdraw e LOD**, o **batching/instancing**, o **controle de formato/gamma**, e a disciplina de **streaming e cache**. O que é específico de EDRAM/Xenos ou de compute shader de PC/PS4 não deve ser portado cegamente.

### Cited Findings

**GTA V — frame dissecado (PC DX11)**

- “GTA V uses a deferred rendering pipeline, working with many HDR buffers.” O G-buffer usa **MRT com 5 render targets** (diffuse, normal, specular, irradiance e depth-stencil), cerca de **1.900 draw calls** só na geração do G-buffer; a cena é desenhada **front-to-back** para early-Z rejeitar fragmentos ocluídos. — [Adrian Courrèges, GTA V Graphics Study](https://www.adriancourreges.com/blog/2015/11/02/gta-v-graphics-study/) **[verificado]**
- Reflexos: um **cubemap de ambiente 6×128×128** é renderizado por frame em forward (~30 draws por face), convertido para **dual-paraboloid map** para baratear a leitura; GTA IV, segundo o próprio artigo, já usava dual-paraboloid, mas distorcendo os meshes no vertex shader em vez de pós-processar o cubemap. — mesma fonte **[verificado; a parte de GTA IV é nota do autor, não medição dele]**
- Culling/LOD: no PC/PS4 é feito por **compute shader**; no PS3 seria Cell/SPU. GTA V usa **alpha stippling** (dithering 1 em 2 pixels) para transições de LOD, com correção posterior em pós-processo. — mesma fonte **[verificado]**
- Sombras: **CSM com 4 shadow maps** em uma textura 1024×4096, ~1.000 draw calls, blur depth-aware com “early out” em 1/8 de resolução. — mesma fonte **[verificado]**
- Outros passes: planar reflection 240×120 (~650 draws), SSAO em metade da resolução, SSS com máscara de stencil, água com MRT, light shafts/fog em metade da resolução, céu/clouds, transparentes em 11 draw calls com instancing, tone mapping filmic (Uncharted 2), bloom, FXAA, lens distortion, minimapa com scissor e tiles vetoriais. Total: **4.155 draw calls, 1.113 texturas, 88 render targets**. — mesma fonte **[verificado no PC]**
- GTA V roda a **720p no Xbox 360 e no PS3**; GTA IV roda **720p no 360 e 640p no PS3**. — [Wikipedia RAGE, citando Digital Foundry](https://en.wikipedia.org/wiki/Rockstar_Advanced_Game_Engine) **[verificado]**

**GTA IV — o que a engenharia reversa dos ports mostra**

- O renderer do RAGE de GTA IV usa a camada `grc*` (`grcViewport`, `grcWindow`, `grcDevice`), com frustum culling e matrizes de view/projeção; a resolução de endereços é feita por **pattern matching** em runtime, não por tabela estática como no RenderWare. — [DeepWiki sigmaco/plugin-sdk, RAGE GTA IV](https://deepwiki.com/sigmaco/plugin-sdk/6.2-rage-engine-%28gta-iv%29) **[verificado como documentação de SDK de modding, não como fonte da Rockstar]**
- O Theft4 documenta contratos de correção do renderer de GTA IV que são típicos de RAGE/Xenos: **reversed viewport depth**, convenção de profundidade **-1..1** no vertex, **packed lighting aliases** e **float-pair MRT** com channel masks específicos, draws sem attachment, pipelines depth-only, stream 16 por instância, retângulos bufferizados/indexados e **output drawables rotativos**. — [THEFT4_0.3_ARCHITECTURE](https://github.com/KoreanSeats1/Theft4/blob/main/docs/THEFT4_0.3_ARCHITECTURE.md), [RELEASE_0.3](https://github.com/KoreanSeats1/Theft4/blob/main/docs/RELEASE_0.3.md) **[verificado no código/docs do port]**
- O Theft4 também trata **color resolves**, **depth handoffs**, **reflections** e **host effects** como operações ordenadas de um frame, e separa **superfícies GPU mutáveis** de **texturas imutáveis de upload**. — mesma fonte **[verificado]**
- O ProjectTM relata que o 0.2.1 apresentava **black frames** por incompatibilidade de **formato de cor Xenos** no caminho Vulkan/NVK; o 0.2.2 inclui um fix, ainda pendente de validação em hardware. — [README ProjectTM](https://github.com/iy26adchak/ProjectTM) **[verificado como relato do projeto]**

**Xenos / 360**

- O Xenos tem **10 MB de eDRAM** dedicada; graças a ela faz **4× MSAA, z-buffering e alpha blending sem penalidade apreciável**; a banda eDRAM interna é 256 GB/s e a interface eDRAM↔GPU é 32 GB/s. — [Wikipedia Xbox 360 technical specifications](https://en.wikipedia.org/wiki/Xbox_360_technical_specifications) **[verificado]**
- **[inferência]** No 360, render targets e resolves vivem na eDRAM; no Switch/NVK não existe eDRAM. Um renderer “nativo” que ignora isso precisa reproduzir **semântica** (formato, MSAA, resolve, ordem), não o hardware. O caminho de GPU Xenos emulada do GH3-nx faz esse trabalho; o renderer nativo precisa garantir que o resultado final bata com o do caminho emulado.

**Tabela de transferibilidade**

| Técnica RAGE | O que faz | Portable para 360→Switch? | Relevância para GH3-nx |
|---|---|---|---|
| Deferred G-buffer (GTA V) | Adia shading; 5 MRTs | Conceito sim; pipeline não | Baixa para GH3 (forward), alta como **frame graph** |
| CSM (4 cascatas) | Sombras dinâmicas de mundo aberto | Sim, mas caro | Baixa; GH3 tem iluminação mais estática |
| SSAO half-res | Oclusão ambiente | Sim | Baixa/média; só se já existir no jogo |
| Cubemap → dual-paraboloid | Reflexos baratos | Sim | Média, se GH3 usar env maps |
| Planar reflection | Reflexo de água/superfície | Sim, caro | Baixa |
| Culling/LOD por compute | Seleção de geometria | **Não no 360** (Xenos não tem compute); no Switch há compute, mas o código 360 não usa | Média: fazer em CPU, com orçamento |
| Front-to-back / early-Z | Reduz overdraw | Sim | **Alta** (note highway, crowd, partículas) |
| Alpha stippling / LOD dither | Transição de LOD | Sim | Média |
| Instancing de transparentes | Partículas/objetos repetidos | Sim | **Alta** (crowd, notas, efeitos) |
| Tone mapping / gamma / bloom | Look final | Sim | **Alta** para paridade e para diagnosticar flashes |
| FXAA / post | AA barato | Sim | **Alta** (substituir MSAA caro, se a paridade permitir) |
| Streaming de assets | Mundo contínuo | Sim | **Alta** (músicas, venues, personagens) |
| EDRAM resolve/formatos | RT na eDRAM | **Não** literalmente; reproduzir semântica | **Alta** no caminho emulado; crítica para flashes |
| Async compute | Sobreposição de trabalho | **Não no 360**; possível no Switch, mas é rewrite | Baixa; não é “port” |
| Ray tracing / DLSS | Features de PC/next-gen | Não | Nenhuma |

### Inferences

- **Não copie o deferred de GTA V para o GH3.** O GH3 é um jogo de ritmo com cena pequena e câmera controlada; o custo de um G-buffer de 5 MRTs + combine + pós seria desproporcional. O que vale é a **disciplina de passes**: declarar dependências, evitar resolves desnecessários, manter depth/color contracts e separar UI.
- **O maior ganho barato tende a ser overdraw e resolução**, não “mais um passe”. Em GH3, note highway transparente, crowd, partículas e bloom costumam dominar fillrate; front-to-back, scissor, LOD e redução de blending são mais eficazes que reescrever o pipeline.
- **MSAA é uma decisão de custo/qualidade.** O 360 fazia 4× MSAA “de graça” na eDRAM; no Switch/NVK, MSAA ocupa banda e memória. Se o jogo já usa MSAA no caminho emulado, testar 2×/0× + FXAA/SMAA e medir com `gameplay_fps.py` antes de assumir ganho.
- **Formato de cor/gamma é a primeira suspeita de flash full-screen.** O caso ProjectTM (Xenos color format → black frames) mostra que um mismatch de formato pode zerar/pretear o frame inteiro sem crashar.
- **Streaming e primeira compilação de pipeline** explicam hitches e “flash de clear color” no primeiro uso de uma cena/pipeline; o Theft4 trata isso com caches bounded, receipts de GPU e pipelines persistentes.

### Gaps

- **[lacuna]** Não existe estudo público de frame da versão **360** de GTA V; o estudo do Adrian é PC DX11. A arquitetura deferred é confirmada no PC, não medida no 360.
- **[lacuna]** Não há fonte primária da Rockstar sobre o pipeline de GTA IV; a descrição “Forward+ / híbrido” vem de análise comunitária (ex.: thread NeoGAF que referencia um vídeo técnico) e não foi confirmada por captura de frame. Trate como **[não verificado]**.
- **[lacuna]** Não medi o custo de MSAA, resolves ou formatos no NVK/Switch; os números do Theft4 são de iPad M5, não de Tegra X1.
- **[lacuna]** Não há benchmark público de GH3-nx no caminho nativo vs emulado nesta pesquisa; a afirmação “GPU-bound” é contexto do projeto, não medição desta sessão.

---

## 3. O que um port pequeno pode reutilizar de re3/reVC, OpenRW e do trabalho GTA?

### Takeaway

**Reutilizável de verdade:** (a) o **ReXGlue/XenonRecomp** e o pipeline Xenos → SPIR-V, que o GH3-nx já usa; (b) os **padrões de arquitetura** do Theft4 (frame plan imutável, registry de superfícies, conversão de geometria, caches bounded, receipts de GPU, BC→ASTC); (c) os **fixes de Switch/NVK** e o aprendizado do ProjectTM, que é o caso mais próximo; (d) a documentação de correção do GTA IV (depth reverso, MRT, resolves). **Pouco reutilizável:** re3/reVC e OpenRW — são RenderWare/GTA III, não RAGE/360; servem como referência conceitual e como exemplo de decomp/reimplementação, não como código de renderer. **Ainda não reutilizável:** SanRecomp, que não tem pipeline gráfico; e o “GTA V Switch port”, cujo motor não é público.

### Cited Findings

- **ReXGlue SDK** é a base declarada do LibertyRecomp/Theft4 e do GH3-nx; o LibertyRecomp usa um fork com “PowerPC → C++ translation and Xenos shader translation”. — [LibertyRecomp README](https://github.com/OZORDI/LibertyRecomp), [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) **[verificado]**
- **Theft4 0.3** separa **preparação CPU/API-neutral** de **realização no backend**; o frame plan descreve “attachment roles, draws, resolves, copies and host utilities”, preserva continuidade entre submissions internas e distingue views lógicas, versões imutáveis e storage GPU mutável. O documento diz explicitamente que shader hashes, formatos Xbox, regras de alias de render target, LOD addresses e constant banks de GTA IV são **title-specific** e não devem ser copiados como verdade geral. — [THEFT4_0.3_ARCHITECTURE](https://github.com/KoreanSeats1/Theft4/blob/main/docs/THEFT4_0.3_ARCHITECTURE.md) **[verificado]**
- **Theft4** implementa conversão **BC→ASTC** persistente e capability-gated para GPUs sem suporte a BC; isso é diretamente análogo ao problema de texturas comprimidas no Switch/Tegra. O próprio doc diz que o inventário e os layouts são específicos de GTA IV; a lição geral é “preparação persistente de formato por conteúdo/capacidade”, não copiar o inventário. — mesma fonte **[verificado]**
- **Theft4** usa export offline de shaders stock para Metal, com bindings refletidos e identidades explícitas para variantes; o doc alerta que trocar a API (Vulkan → Metal) **não** elimina trabalho de preparação/duplicação e que o ganho veio de corrigir ownership, caches e semântica de frame. — [RELEASE_0.3](https://github.com/KoreanSeats1/Theft4/blob/main/docs/RELEASE_0.3.md) **[verificado]**
- **ProjectTM** é o projeto público mais direto que encontrei combinando GTA IV + Switch + NVK; o README credita Theft4, Theft4-NX, LibertyRecompAndroid, LibertyRecomp, ReXGlue, Xenia, XenonRecomp, `nfsmw-nx`, project8-sw, Mesa/NVK, devkitPro e libnx. O 0.2.2 tem driver de áudio Switch de 4 buffers, 246 imports Xbox e 85 translation units de GTA IV, e o fix de formato de cor Xenos. — [ProjectTM README](https://github.com/iy26adchak/ProjectTM) **[verificado]**
- **re3/reVC** são decompilações de GTA III/VC baseadas em RenderWare; os forks declaram GPL (verifique o `LICENSE` de cada fork) e o upstream foi derrubado (HTTP 451). Não cobrem RAGE, Xenos, 360 ou GTA IV/V. — [re3 mirror](https://github.com/hottabxp/re3), [re3-vita (descrição “GTA III decompilation”)](https://github.com/re3fork/re3-vita) **[verificado]**
- **OpenRW** é uma reimplementação de GTA III, GPLv3, com dependências SDL2/Bullet/FFmpeg; exige cópia PC legítima. Não tem Xenos, RAGE ou Vulkan/NVK. — [OpenRW README](https://github.com/rwengine/openrw) **[verificado]**
- **SanRecomp** usa XenonRecomp e declara GPU/rendering como “in progress”; não há renderer público para reutilizar. — [SanRecomp README](https://github.com/OZORDI/SanRecomp) **[verificado]**
- **GTA V Switch tooling** (RPF editor, DLC manager, ScriptHookV-NX) é útil para **entender o ecossistema de mods de GTA V no Switch**, mas não contém renderer ou recompiler. — [toolbox](https://github.com/niceboat0/GTA5-Switch-Toolbox), [ScriptHookV-NX](https://github.com/Geekmaxxer/ScriptHookV-NX) **[verificado]**

### Inferences

- **O maior ROI para o GH3-nx é o ProjectTM + Theft4, não re3/OpenRW.** O ProjectTM já resolveu coisas específicas de Switch/NVK (formato de cor Xenos, áudio, imports, toolchain) que o GH3-nx vai reencontrar; o Theft4 tem o modelo de frame/ownership que evita retrabalho.
- **Reutilize padrões, não dados.** O próprio Theft4 diz para não copiar hashes de shader, formatos Xbox, regras de alias e layouts de constant bank de GTA IV. Para GH3, o equivalente é copiar a **estrutura** (frame plan, registry, caches, receipts) e redescobrir os contratos do GH3 com oráculo (captura emulada vs nativa).
- **Licença:** ReXGlue, Theft4, ProjectTM, re3 e OpenRW são GPL (ou GPL-compatíveis); o GH3-nx já usa project8-sw (GPL). Antes de copiar código, verifique a licença de cada arquivo e mantenha atribuição. Isto não é parecer jurídico.
- **Não espere que SanRecomp entregue um renderer de GTA V tão cedo.** O README é honesto: sem GPU, áudio, saves ou input. Acompanhar é útil para RAGE reverse engineering, mas não é dependência para GH3.

### Gaps

- **[lacuna]** Não há licença explícita no `GTA5-Switch-Toolbox` (sem LICENSE), e o motor do port de GTA V não é público; não é possível avaliar reutilização.
- **[lacuna]** O ProjectTM tem 0 estrelas e um único autor; não há revisão independente do fix de formato de cor nem teste em múltiplos Switches.
- **[lacuna]** Não encontrei documentação pública do SanRecomp sobre arquitetura de renderer; só o checklist de status.
- **[lacuna]** Não comparei o código do renderer nativo do GH3-nx com o do ProjectTM; a sobreposição real de APIs/formatos precisa de auditoria.

---

## 4. Quais pitfalls de render-correctness podem explicar colour flashes?

### Takeaway

Os suspects mais fortes, em ordem, são: **(1) formato/gamma de cor Xenos**, **(2) resolve/MSAA/eDRAM**, **(3) present/swapchain**, **(4) clear/loadOp e primeiro frame**, **(5) depth convention/reversed Z**, **(6) channel mask/swizzle de MRT**, e **(7) vídeo (Bink) na transição para 3D**. O caso ProjectTM 0.2.1→0.2.2 (formato de cor Xenos causando black frames no NVK) é a evidência mais direta de que um mismatch de formato pode produzir frame inteiro errado sem crash. O GH3-nx já documenta que **present `immediate`/`mailbox` em tela cheia trava no `CAMetalLayer`**; isso é um problema de present, não de cena, e deve ser excluído antes de culpar o renderer.

### Cited Findings

- **Formato de cor Xenos no NVK** — ProjectTM: “a Vulkan/NVK compatibility fix for the Xenos color format that caused 0.2.1 to present black frames”. O fix ainda precisava de hardware run no 0.2.2. — [README ProjectTM](https://github.com/iy26adchak/ProjectTM) **[verificado como relato]**
- **Resolve/ordem de frame** — Theft4 0.3 lista como contratos: “Complete ordered frames now include the game's render passes, color resolves, depth handoffs, reflections and host effects”; “GPU-produced surfaces are distinct from immutable uploaded textures”; “Reflection updates wait for valid written content instead of making an entire otherwise valid scene fail”. — [RELEASE_0.3](https://github.com/KoreanSeats1/Theft4/blob/main/docs/RELEASE_0.3.md) **[verificado]**
- **Depth/clip-space** — Theft4 trata “reversed viewport depth, the negative-one-to-one vertex depth convention” e “depth-only pipelines”. O doc de arquitetura cita “reversed-depth, clip-space, packed-lighting/float-pair MRT, channel-mask and target-storage mismatches” como causas de incorreção no bring-up. — [THEFT4_0.3_ARCHITECTURE](https://github.com/KoreanSeats1/Theft4/blob/main/docs/THEFT4_0.3_ARCHITECTURE.md) **[verificado no port]**
- **MRT/channel mask** — O mesmo doc fala de “packed lighting aliases and the title's float-pair multiple render targets retain their intended channel masks and storage roles”. — [RELEASE_0.3](https://github.com/KoreanSeats1/Theft4/blob/main/docs/RELEASE_0.3.md) **[verificado]**
- **eDRAM/MSAA** — Xenos tem 10 MB eDRAM e faz 4× MSAA/z/alpha blending sem penalidade; os RTs e resolves ficam nela. — [Wikipedia Xbox 360 specs](https://en.wikipedia.org/wiki/Xbox_360_technical_specifications) **[verificado]**
- **Present/swapchain no GH3-nx** — O `AGENTS.md` do repo diz: “tela cheia + modo present `immediate`/`mailbox` trava no `CAMetalLayer` do MoltenVK → janela preta. O `run-gh3.command` já passa as flags certas”. — `AGENTS.md` local **[verificado no projeto]**
- **Vídeo** — RAGE usa Bink Video como middleware (Wikipedia RAGE); Theft4 tem opção “Skip Intro Videos” e o ProjectTM menciona “restoring video after a clean rebuild”. Isso mostra que a transição vídeo→3D é um subsistema separado do renderer de cena. — [Wikipedia RAGE](https://en.wikipedia.org/wiki/Rockstar_Advanced_Game_Engine), [Theft4 README](https://github.com/KoreanSeats1/Theft4), [ProjectTM README](https://github.com/iy26adchak/ProjectTM) **[verificado]**
- **Texturas comprimidas** — Theft4 converte BC→ASTC quando a GPU não suporta BC; o doc diz que formatos não suportados causavam “missing/black textures” e que a preparação persistente evita conversão em runtime. — [THEFT4_0.3_ARCHITECTURE](https://github.com/KoreanSeats1/Theft4/blob/main/docs/THEFT4_0.3_ARCHITECTURE.md) **[verificado]**
- **Pipeline/primeiro uso** — Theft4 separa “preparação” de “realização” e usa caches/pipelines persistentes; o doc alerta que compilação de pipeline no driver ainda pode ocorrer ao encontrar cena nova. — [THEFT4_0.3_ARCHITECTURE](https://github.com/KoreanSeats1/Theft4/blob/main/docs/THEFT4_0.3_ARCHITECTURE.md) **[verificado]**

### Inferences

**Hipóteses ranqueadas para colour flashes no GH3-nx**

1. **Formato de cor/gamma do render target Xenos** (ex.: `k_8_8_8_8` vs `k_8_8_8_8_GAMMA`, sRGB vs linear, 2_10_10_10, float16). Um mismatch pode fazer o clear ou o resolve produzir branco/preto/cores saturadas por 1 frame. **Teste:** forçar clear magenta e ver se o flash fica magenta; comparar captura nativa vs emulada do mesmo frame; logar o formato de cada RT.
2. **Resolve/MSAA/eDRAM.** Se o renderer nativo não reproduz o resolve (ou resolve para o formato/ordem errada), o frame pode mostrar conteúdo velho ou parcial. **Teste:** desligar MSAA e ver se o flash muda; comparar com o caminho emulado; verificar se o flash ocorre só em transições de cena (quando o RT é reutilizado).
3. **Present/swapchain.** `immediate`/`mailbox`, número de imagens, acquire antes de o frame estar completo, ou present de um drawable rotativo. **Teste:** FIFO + janela; uma imagem só; logs `XELOG_GPU PRESENT`; capturar com `gameplay_capture.py`.
4. **Clear/loadOp.** Primeiro frame de uma cena sem clear de cor (ou clear só de depth), ou `loadOp=LOAD` em RT que não foi escrito. **Teste:** forçar `clear` em todos os attachments no início do frame; se o flash desaparece, é isso.
5. **Depth convention/reversed Z.** GH3 pode usar 0..1 ou reverso; se o renderer nativo usa a convenção errada, skybox/UI pode “furar” por um frame em transições de câmera. **Teste:** comparar depth buffer e skybox; testar com depth clamp/clear 0 vs 1.
6. **MRT/channel mask/swizzle.** Menos provável em GH3 (provavelmente poucos MRTs), mas se houver lighting packed ou buffer de bloom, um canal errado pode dar flash verde/magenta. **Teste:** desabilitar passes de post um a um.
7. **Vídeo (Bink) → 3D.** Se o flash ocorre ao sair do vídeo/intro/menu, o problema pode ser o plane de vídeo/apresentação, não a cena. **Teste:** pular vídeos e ver se o flash some; capturar frames na transição.
8. **Textura comprimida/formato não suportado.** Costuma ser persistente (textura preta), mas um fullscreen quad com textura errada pode piscar. **Teste:** forçar fallback RGBA/ASTC e comparar.
9. **Pipeline compilation hitch.** Um stall no primeiro uso pode mostrar o clear color por alguns frames; não é “flash” de conteúdo, é tempo. **Teste:** warm-up de cena/pipelines e medir frame time.

**Plano de diagnóstico barato para o GH3-nx**

- Reproduzir com câmera fixa e com transição de cena; anotar se o flash é **1 frame**, **alguns frames** ou **persistente**.
- Rodar o mesmo trecho no renderer **emulado** e no **nativo**; comparar com `tools/gameplay_capture.py` + `tools/image_stats.py`.
- Trocar o clear color para magenta/verde; se o flash mudar de cor, o problema está no **clear/format/present**, não no shading.
- Forçar FIFO, janela, 1 imagem; se sumir, é **present/swapchain**.
- Desligar MSAA e/ou resolves; se sumir, é **EDRAM/resolve**.
- Desligar post FX (bloom/DOF) um a um; se sumir, é **pass/format de post**.
- Verificar logs de `XELOG_GPU PRESENT` e contagem de frames; comparar `out/perf` antes/depois.

**O que NÃO fazer**

- Não “consertar” o flash reduzindo resolução ou desligando o renderer nativo; isso mascara o sintoma e quebra a paridade.
- Não assumir que o problema é o jogo; o caminho emulado é o oráculo.
- Não copiar o deferred/async compute de GTA V para o GH3 esperando ganho; o custo de fillrate/banda no NVK pode piorar.
- Não usar `immediate`/`mailbox` em tela cheia no Switch/MoltenVK (e, por analogia, testar PresentMode com cuidado no NVK).

### Gaps

- **[lacuna]** Não há issue pública do GH3-nx descrevendo “colour flash”; não sei se o sintoma é no menu, na gameplay, no vídeo ou no present. As hipóteses acima são genéricas e precisam ser confrontadas com captura.
- **[lacuna]** Não sei quais formatos de RT/MSAA o GH3 usa no 360; isso precisa ser extraído do `generated/`/comandos PM4 ou do renderer emulado.
- **[lacuna]** Não há confirmação de que o NVK/Switch suporte BC textures; o caminho BC→ASTC do Theft4 é uma referência, não uma prova para Tegra X1.
- **[lacuna]** Não medi o impacto de MSAA/resolve no NVK; os números do Theft4 são de iPad M5 e não transferem para Switch.

---

### Nota final de confiança

- **Alta confiança:** existência e status dos repositórios (READMEs/API GitHub, 2026-10-19); GTA V deferred no PC (Adrian Courrèges); Xenos 10 MB eDRAM/4× MSAA (Wikipedia); GTA V 720p nos consoles (Wikipedia/DF); ProjectTM/black frame por formato Xenos; Theft4 correctness contracts.
- **Média confiança:** status “jogável” dos ports móveis (descrições de repo, sem teste); port de GTA V para Switch (guia comunitário, sem repo de motor); “GTA IV Forward+” (análise comunitária, não primária).
- **Baixa confiança / especulação:** causa exata de colour flashes no GH3-nx; ganho de qualquer otimização específica no NVK sem medição.
