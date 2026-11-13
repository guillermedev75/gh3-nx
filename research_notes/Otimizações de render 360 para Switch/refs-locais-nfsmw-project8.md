# Otimizações de render 360 para Switch: o que os portes locais nfsmw-nx e project8-sw ainda têm a ensinar ao GH3-nx

Escopo: leitura direta de `/Users/guillermemanhaes/gh3-refs/nfsmw-nx` e
`/Users/guillermemanhaes/gh3-refs/project8-sw`, confrontada com o renderer atual do GH3
(`/Users/guillermemanhaes/gh3-nx/src/native`). Caminhos `src/...`, `cmake/...`, `config/...` e
`run-gh3.command` são relativos a `/Users/guillermemanhaes/gh3-nx`; os demais são absolutos. Números marcados **medidos** vêm dos docs/código; **proposto**
significa que a técnica existe, mas não há número publicado; **descartado** significa que foi medido e perdeu.
O GH3 já herdou do project8-sw o grosso do renderer (targets Vulkan, resolves por copy, depth resolve por
compute, caches de setup/sampler/textura, dedupe de vértices por conteúdo, pipelines assíncronos, alias
MSAA/EDRAM e scene scale); depth24, record thread e MSAA existem como opções, mas estão desligados por
default (`src/native/native_renderer.cpp:68-107`). As lacunas apontadas abaixo são, em sua maioria, técnicas
que o **nfsmw-nx** acrescentou depois e que o project8-sw não tem.

## 1. Quais otimizações de CPU por draw / ring thread existem no nfsmw e quais um primeiro renderer nativo tipicamente não tem?

### Takeaway

O ganho grande do nfsmw não veio de micro-otimizar a gravação do draw, e sim de **parar de repetir trabalho**
(cache exato de shader imediato, cache direta de pipeline, cache de sampler entre frames, dedupe de vértices,
reset seletivo de query pools) e de **tirar o ring thread do caminho de I/O, log e alocação**. Um primeiro
renderer nativo quase sempre nasce com o caminho inverso: grava tudo na hora, realoca por draw, reseta todos
os pools, loga no ring e compila pipeline sob demanda.

### Cited Findings

- Custo inicial de referência: o primeiro renderer nativo do nfsmw gravava a **1,5–1,8 µs/draw no PC**; o
  mesmo draw custava **~16 µs no Switch** (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/performance-history.md:38-40`;
  `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/measuring.md:24-26`).
- Dentro do NVK, o draw custava **~3,4 µs**: `vkCmdDrawIndexed` 1,4–1,6 µs, `vkCmdBindPipeline` 3–3,7 µs e
  o set 4 (UBO dinâmico) 1,3 µs (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:236-240`).
- **Cache exato de `IM_LOAD_IMMEDIATE`** (o VS que o D3D copia no ring com os fetch já patchados): 4.000–8.000
  pacotes/s em trechos de corrida, 1,5–2,8 µs cada; só **82 microcódigos distintos** na sessão inteira. O
  cache reusa cópia byte-swapped, identificação, fingerprint e geração; guarda de auto-verificação
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_sistema.cpp:189-202,2846-2872,2884-2957`).
- **Cache direta de pipelines** (256 slots contíguos, 22 KB) na frente do `unordered_map`: o lookup custava
  0,52–0,76 µs/draw e o miss ~1,3 µs (XXH3 + bucket prime + jumps no heap)
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:978-996`).
- **Pipelines pré-compilados em background**: sem cache, criar pipeline no NVK custava **~59 ms** (82 ms para
  os da primeira corrida); com lista + `VkPipelineCache` salvo, **113 de 113 pipelines prontos em 0,3 s** e
  nenhum criado lentamente na corrida (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/native-renderer.md:121-128`;
  `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:488-500`).
- **Cache de sampler entre frames**: o caminho frio da etapa `texturas` custava 13,5 µs e rodava 266
  vezes/frame = 3,6 ms; com validade entre frames os frios caem para ~45–70/frame = 2,6–3,0 ms. O bug de
  validade `UINT64_MAX` com 3 work slots borrava o leaderboard (smear), porque o CPU rodava 2 frames à
  frente (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:340-365`).
- **Fingerprint de textura** só quando o conteúdo muda, com rechecagem cada vez mais espaçada (até 1 a cada
  32 frames): no PC levou o trabalho de textura de **66 µs para 4,2 µs/draw** no menu
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/native-renderer.md:101-106`). Rechecagem por amostra (primeiro/último bloco de
  4 KB + 1 a cada 8) e orçamento de KB/frame: `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:157-173,1374-1404`.
- **Dedupe de vértices no mesmo frame**: 7,60 MB/frame copiados (204 MB/s) = **3,09 ms/frame só de escrita**
  em memória uncached; 99,0–99,6 % dos bytes eram repetição byte a byte, 41–49 % dentro do mesmo frame. Tabela
  de 16.384 entradas × 40 bytes (640 KB), limpa por geração em `WAIT_REG_MEM`
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_vertices_dedupe.h:3-67,82-95`).
- **Sem `vkCmdBindVertexBuffers` em draw de um único binding**: 0,32–0,46 µs/chamada em 66–98 % dos draws;
  o buffer fica bound em 0 e o draw se desloca com `vertexOffset`/`firstVertex`
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:786-813`).
- **Constantes compartilhadas em buffer com cache de CPU**: escrever sem cache custava **2,0 µs/draw**, com
  cache **0,4 µs/draw** (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:973-977`).
- **Reset seletivo de query pools**: 64 queries de estatística + 2.048 por draw = **2.144 resets/frame**;
  no NVK cada um vira `SET_REPORT_SEMAPHORE`, 42,9 KB de stream, estimados 0,3–1,1 ms reais (limite superior
  medido 2,81 ms). Resetando só o que o frame usa, restam **32 resets (−98,5 %)**
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:4814-4834`).
- **Log fora do ring**: o dump periódico custava **29–32 ms no ring** (14–40 ms de escrita, flush de SD a cada
  2–4 linhas); explicava 33 de 170 stutters (19,4 %) e 1.267 ms de 5.707 ms perdidos. Formatar no ring e
  escrever noutra thread removeu o custo (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_sistema.cpp:125-168`;
  `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.h:336-350`).
- **Contadores não atômicos**: ~83.000 pacotes/frame com `fetch_add` atômico no Cortex-A57 custavam
  **1,1 ms/frame** (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/native-renderer.md:116-119`).
- **Busy-wait do guest**: dois loops do D3D mantinham duas threads perto de 100 % “sem fazer nada”; com sleep
  foram a **25 % e 17 %** no PC (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/platform-notes.md:142-148`).
- **Prioridades do ring**: ring 0x2D, apresentação 0x2C, guest 0x3B (o único time-sliced); um present thread
  na mesma prioridade do ring gastava 998 ms de tempo real para entregar 300 ms de CPU
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/platform-notes.md:111-131`).
- **Texturas novas**: `vkBindImageMemory` custa ~0,6 ms de wall time por textura no Horizon; entrar numa zona
  traz 33–45 de uma vez = **19–27 ms de ring parado**. O bind foi para uma thread 0x2D
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:416-438`). Já o thread de hash/untile foi **descartado**: o guard disparou no
  startup e a criação na verdade custava só 56 µs/textura no ring
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:440-460`).
- **O que NÃO pagou**: forçar o push buffer do NVK a usar memória com cache. A mudança foi aplicada
  (`armDCacheClean` 2,51 → 14,89 MB/s), mas `grabar` ficou 3,871 → 3,820 µs/draw (0,78 σ) e o custo de
  apresentação subiu 1,328 → 1,457 ms/Swap. A teoria de “45 ns por store uncached” estava errada no A57
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_entorno_mesa.cpp:52-99`).
- **Extensões de dynamic state (EDS 1/2/3)**: 13 % menos binds, mas cada bind foi de **2,95 para 4,36 µs**;
  perda líquida de ~1 ms de ring/frame (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:1035-1063`;
  `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/mesa.md:127-147`).

**O que o GH3 já tem** (para não recomendar de novo): caches de setup por hash de estado
(`src/native/native_renderer.cpp:688-733`), cache de descriptor set por conteúdo
(`src/native/native_renderer.cpp:566-580`), hash de conteúdo de vértices uma vez por frame
(`src/native/native_renderer.cpp:580-607`), 4 submission slots e stream de 160 MB (`src/native/native_renderer.cpp:125-140`),
pipelines assíncronos (`src/native/native_renderer.cpp:105-107`) e record thread opcional (`src/native/native_renderer.cpp:74-76`).

### Inferences

- **P0 — cache exato de `IM_LOAD_IMMEDIATE`**: o GH3 usa o tradutor de shader em runtime e recebe o VS
  patchado a cada `IM_LOAD` (`src/native/native_renderer.h:100-113`); é exatamente o caso que o nfsmw mediu em
  1,5–2,8 µs/pacote e 82 microcódigos distintos. Baixo risco, guarda por `memcmp`.
- **P0 — pipeline prewarm list**: o GH3 já carrega e salva o `VkPipelineCache` do driver
  (`src/native/native_renderer.cpp:4767-4769,4828-4868`), mas não há lista de pipelines para recriar em
  background no boot (não há `prewarm`/`precalent` no código). O nfsmw mediu 59 ms/pipeline e 0,3 s para 113
  pipelines.
- **P1 — reset seletivo de query pools**: verificar se o GH3 reseta pools de timestamp/perfil por frame; o
  padrão do nfsmw sugere remover resets de pools que o frame não usa.
- **P1 — dedupe de vértices**: o GH3 já tem hash de conteúdo por frame (`src/native/native_renderer.cpp:580-607`), então
  o item já está coberto; resta confirmar se ele cobre **índices** e constantes, não só vértices.
- **P1 — cache de sampler entre frames com horizonte explícito**: o GH3 tem caches por frame/submission
  (`src/native/native_renderer.cpp:688-733`); o nfsmw mostra que estender a validade entre frames vale ~0,6–1,0 ms/frame,
  mas exige `valido_hasta` curto para alvos reescritos todo frame.
- **P2 — tirar log/relatório do ring**: o GH3 imprime relatórios periódicos no sistema nativo
  (`src/native/gh3_native_system.cpp:588-650`); se o log for síncrono, adotar o padrão de fila + thread 0x3B.
- **P2 — `vertexOffset` sem bind**: depende de o shader do GH3 não ler `SV_VertexID`; no nfsmw os shaders não
  leem, por isso foi exato. Verificar antes.

### Gaps

- Não há medição publicada de quanto cada item acima vale **no GH3**; os números são do nfsmw/project8.
- O project8-sw não publica breakdown de custo por draw no Switch; seus docs só afirmam 30 fps em Free Skate e
  que o gargalo restante é o trabalho de CPU do jogo + lookup de texturas (`/Users/guillermemanhaes/gh3-refs/project8-sw/docs/known-issues.md:17-26`).
- Não foi possível confirmar se o GH3 usa query pools de timestamp por frame e se os reseta todos.

## 2. Quais técnicas de GPU são descritas — e o que não pagou?

### Takeaway

As que mais pagaram foram **eliminar pixels sombreados** (ZCULL, early-Z, céu adiado, blur radial removido) e
**eliminar cópias** (resolve por troca de imagem, sombra por mínimo, depth/front buffer lazy). As que não
pagaram foram as que atacam área/resolução quando o custo é **geometria ou draws**: encolher o shadow map,
ZCULL no shadow pass, baixar a resolução interna além de 1024×576, trocar imagem de cor e dividir o frame em
duas submissions.

### Cited Findings

- Orçamento inicial medido no Switch a stock: **60,2 ms** de GPU por frame — cena 27,2, shadow maps 13,6,
  cópias 7,4, cubemap/blur 7,0, pós-processamento 3,9 (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/performance-history.md:42-53`).
- **Um passe em vez do tiling do Xbox 360**; **resolve sem cópia** economizou **2,36 ms** dos 7,41 ms de
  cópias; **ZCULL** ~2 ms; shaders simplificados; ao fim, 33,35 ms e 26,0 FPS
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/performance-history.md:60-81`).
- Cada resolve “de verdade” custava **3–4 ms de GPU** no renderer emulado
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/native-renderer.md:26-29`).
- **Resolve sem cópia (depth)**: trocar as imagens em vez de copiar; −2,36 ms medidos. Para cor **não
  funcionou**: 16.807 swaps contra 17.350 restores (quase 1:1) e, com clear + target de 720 linhas, a cena
  virou um borrão amarelo (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:70-128`).
- **Segundo resolve do shadow map sem clear**: 2,56 Mpixels, **2,4 ms reais**, 44 % de todas as cópias do
  frame; resolvido com swap + restauração (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:75-90`).
- **Sombra por mínimo**: em vez de restaurar 1600×1600, limpa o target (0,19 ms) e o shader do mundo amostra
  `min(mundo, carros)`; economiza a cópia de **~1,54 ms** (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:318-353`).
- **ZCULL**: −2,12 ms na cena; exige remover `TRANSFER_DST` das imagens de depth. No shadow pass o teto é
  **0,1–0,25 ms** porque o passe é geometria (custo 0,556 ms/10k triângulos), então o balanço é perda 10:1
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/mesa.md:49-74`; `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recorte_sombras.cpp:120-132`).
- **Shadow pass**: 11,3 ms reais, ~660 draws, **91 % do custo é por draw** (não por pixel); 0,556 ms reais por
  10k triângulos, r²=0,982; o mapa desenha 90k triângulos contra 67k da cena visível
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recorte_sombras.cpp:45-46,100-123,152-200`).
- **Encolher o shadow map não pagou**: −44 % de área = −9 % de tempo; a área vale 0,037 ms
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recorte_sombras.cpp:106,174-175`).
- **Vegetação no shadow map**: 55 % dos draws do passe, só 12 % dos triângulos; remover = **−1,2 ms GPU** e
  **−3,1 ms CPU** (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:115-132,175-179`).
- **Composição final**: um quad full-screen com 12 samples custa **2,8–3,2 ms**, 72–80 % do pós-processamento;
  7 dos 12 samples são o blur radial, que removido vale **1,5–2,0 ms** e deixa a imagem mais nítida
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:110-113,181-193`).
- **Resolução interna**: 1280×720 → 1024×576 economiza **2,14 ms reais**; abaixo disso sobra pouco e o scaling
  aparece. Importante: 640×360 **não** muda a resolução em que o jogo desenha, só o tamanho do resolve
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_ajustes_graficos.cpp:16-27`).
- **Early-Z declarado (`EarlyFragmentTests`)**: só para draws que testam Z mas **não escrevem** Z/stencil e
  não estão dentro de query de oclusão do jogo; a imagem é idêntica porque não há escrita para adiantar
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:823-857`).
- **Céu adiado**: o céu era 1 draw/480 índices mas invocava 0,83 M fragmentos = 90 % da tela, **~4,8 ms de
  uma cena de 19,2 ms (25 %)**. Adiar até depois dos opacos é exato se for um único dome opaco que testa Z e
  não escreve; a primeira versão quebrou a imagem porque **7 draws compartilhavam o fingerprint do shader**
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:858-916`).
- **Draws que não pintam nada**: blend `0×src + 1×dst` em todos os canais, ou alpha test `NEVER`, e sem
  escrita de Z/stencil → pular é exato (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:918-934`).
- **Dividir o frame em duas submissions**: a ideia era a GPU começar nas sombras enquanto o CPU grava a cena;
  medido, o gap entre submissions **subiu 3,61 ms** (duas latências de start + segunda peça depende do CPU).
  Ficou desligado (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:707-727`; `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/platform-notes.md:178-183`).
- **Quarto work slot quebrou a imagem**: work slots e output slots são coisas diferentes; `kRanurasSalida`
  ficou fixo em 3, então o CPU 3 frames à frente reusava um output ainda na tela — “flickering and odd
  colors” (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:520-555`).
- **MSAA**: no nfsmw, MSAA “hangs NVK” e todo target é desenhado com 1 sample
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:9`). O GH3 adotou “MSAA-as-samples” (4x em pitch P = 1x em pitch 2P), que é
  o que o jogo usa para limpar o alvo 1x (`src/native/native_edram_samples.h:1-13`;
  `src/native/native_renderer.cpp:77-87`).
- **Depth 24**: o project8 propõe guardar depth em 32 bits/pixel (D24S8 para depth unorm, D32F sem stencil
  para float), em vez de D32F+S8 que a NVIDIA mantém em 64 bits (`/Users/guillermemanhaes/gh3-refs/project8-sw/docs/native-renderer.md:77`;
  `/Users/guillermemanhaes/gh3-refs/project8-sw/src/game/src/native/native_renderer.cpp:65-68,153-160`). **Proposto**: não há número
  publicado de ganho. O GH3 já tem a opção, default off (`src/native/native_renderer.cpp:68-71`).

### Inferences

- **P0 — resolve sem cópia no GH3**: o `Resolve()` atual sempre copia para `ResolveDestination`
  (`src/native/native_renderer.cpp:4126-4230`; `src/native/native_textures.cpp:1165-1255`). O nfsmw mediu
  −2,36 ms de 7,41 ms; o GH3 foi descrito no pedido com 300–400 draws e vários resolves por frame. Precondição: só trocar quando o
  target inteiro for resolvido para textura de mesmo tamanho/formato; manter restauração antes de novo resolve
  e antes de desenhar sem clear. O caso de cor do nfsmw falhou e deve ficar de fora.
- **P1 — sombra por mínimo**: só vale se o modo performance não desligar shadows. Exige shader com
  `tfetch2DSombraMin` e PCF barato; o GH3 traduz shaders em runtime, então a especialização teria de ser
  adicionada ao tradutor, não a uma biblioteca offline.
- **P1 — early-Z e “draws que não pintam nada”**: não mudam a imagem e independem da cena; são os itens de
  menor risco do catálogo. O GH3 já tem `skip_reason` por draw (`src/native/native_renderer.cpp:3403-3430`), então o
  ponto de inserção existe.
- **P2 — céu adiado**: alto ganho potencial (25 % da cena no nfsmw), mas exige identificar o dome por
  assinatura e um guard de “um único dome por frame”; o nfsmw quebrou a imagem na primeira tentativa.
- **P2 — depth24**: pode reduzir banda de depth, mas no GH3 o gargalo descrito é fragment shading; medir A/B
  antes de ligar.

### Gaps

- Não há, nos dois portes, um número de ganho do depth24 no Switch.
- O nfsmw não separa “custo de sombra” por pixel vs geometria no GH3; a proporção pode ser outra.
- Não há medição de resolve sem cópia no GH3 nem contagem de resolves/frame publicada.

## 3. Quais técnicas de culling/redução de draws são usadas, com quais precondições e riscos?

### Takeaway

O nfsmw usa **pouca culling geométrica própria**; o que ele faz é (a) devolver ao jogo bits de culling que o
próprio jogo removeu, (b) limitar views caras (cubemap/reflexo) por **demanda de leitura/visibilidade** e
(c) pular draws exact-free. A culling mais agressiva é condicionada por queries do próprio jogo, porque o jogo
lê o resultado. As reduções por LOD/distância só pagam na sombra, não na cena visível.

### Cited Findings

- **Occlusion queries do jogo (sol)**: o jogo mede quanto do sol está visível e desliga o flare; com contagem
  falsa de 1000 samples o céu estourava para branco e criava halos azul/roxo nas árvores. É preciso medir na
  GPU, não fingir (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_sistema.cpp:381-389`; `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:842-844`).
- **Visibilidade do reflexo por query própria**: cada draw que amostra o reflexo é envolvido numa occlusion
  query; 1–2 frames depois o ring lê se sobrou amostra. Se nenhum draw do water deixou amostra em 20 frames,
  o reflexo é renovado 1 a cada N. O que não dá para medir conta como visível. Guard: a composição final
  full-screen serve de testemunha e precisa dar amostras; se der 0, a decisão volta para “por leitura”
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recortes_carrera.cpp:173-196,240-260`).
- **Reflexo sob demanda**: 42 % dos draws do reflexo eram cópias que ninguém lia; ~2 ms de CPU/frame em
  trechos. Sem leitura recente, renova 1 a cada 4 frames; o water que reaparece vê reflexo de até ~3 frames
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recortes_carrera.cpp:136-170`).
- **Cubemap do carro**: limitar a 1 face/frame + fixar a face do retrovisor; a face do retrovisor desenhava
  **415 objetos = 19,6 % dos draws do frame**, porque o jogo removia dela dois bits de culling
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recortes_carrera.cpp:264-331`). Devolver os bits `0x40 | 0x08000000` à vista 20 foi verificado
  no console (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recortes_carrera.cpp:273-296,886-911`).
- **PixelMinSize nas faces que rotam** (não nas fixas): objetos abaixo do threshold não entram na view; a
  primeira rodada **nunca** é cortada, senão o cubemap fica preto e o carro reflete preto
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recortes_carrera.cpp:305-331,990-1007`).
- **Corte por distância na sombra**: `nfsmw_sombras_corte=150` encurta a distância de sombra para 2/3; custa
  0,556 ms/10k triângulos e 99 % do passe é geometria. O corte composto (multiplicar o valor já escrito) foi
  um bug que saturava em 4096 e esvaziava o mapa; a correção guarda o valor original do jogo
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recorte_sombras.cpp:152-200,210-276`).
- **LOD na cena visível não paga GPU**: subir `PixelMinSize` de 4 para 6 remove no máximo 11k de 6,5 M
  fragmentos = 0,17 %, 0,04 ms; ganha ~3 ms de CPU, mas só em trechos CPU-bound, e causa popping
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recorte_sombras.cpp:442-480`).
- **LOD na sombra**: o mapa não reduz detalhe com distância; o lever é usar a mesma malha reduzida da cena, o
  que **não é decidido no renderer** (ele só vê registradores; sem object id/mesh level). Filtrar por
  contagem de triângulos do draw remove justamente os objetos simples, o oposto do desejado
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recorte_sombras.cpp:152-176`).
- **Atualizar sombra 1 a cada N frames** foi tentado e **descartado**: com o guard no topo 99 % do tempo, o
  skip virou estado normal e as sombras piscaram; a escada final usa “1 mapa” (congela o segundo) e “sem
  sombra”, nunca saltar frames (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_recorte_sombras.cpp:58-98`).
- **Draws que não mudam pixel**: blend que copia o destino ou alpha `NEVER`, sem Z/stencil, podem ser pulados
  por definição (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:918-934`).
- **Céu adiado** também é redução de fragmentos, mas depende de um único dome e de ordem segura
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:858-916`).

### Inferences

- **P1 — queries de visibilidade para passes caros temporais**: o GH3 foi descrito no pedido com 300–400 draws e um modo performance
  que já corta shadows+bloom; uma query de oclusão em volta de um passe (ou de um draw full-screen) pode
  decidir se ele é necessário naquele frame. Precondição: o jogo não pode ler essa query; usar testemunha
  full-screen como o nfsmw e cair para “visível” quando não der para medir.
- **P1 — devolver bits de culling que o jogo remove**: requer decompilação/identificação da função que monta a
  máscara da view e um guard de layout; no GH3 isso ainda não foi feito. Alto risco de popping/seleção errada.
- **P2 — limite temporal de passes caros**: reflexo/cubemap não existem no GH3 da mesma forma, mas o padrão
  “renova 1 a cada N se ninguém leu” serve para qualquer passe cujo resultado não é lido todo frame.
- **Evitar**: skip de shadow por frame (flicker), filtro por contagem de triângulos (remove o barato), LOD da
  cena visível como ganho de GPU (não é).

### Gaps

- Não há no GH3 uma identificação publicada de quais passes são shadows/bloom/reflexo além dos cvars
  experimentais (`src/native/native_renderer.cpp:92-97`); sem isso, culling por passe é frágil.
- O nfsmw não mede o custo de uma query de oclusão por draw no Switch; só o custo do mecanismo de visibilidade
  do reflexo.
- Não há dados de quantos draws do GH3 são “invisíveis” ou redundantes.

## 4. Quais técnicas de frame pacing/present são descritas e quais efeitos medidos?

### Takeaway

O nfsmw separa três coisas: **quem manda no ritmo** (o jogo, o vblank ou o compositor), **quanto custa
apresentar no ring** e **quantos frames o CPU pode adiantar**. As maiores vitórias foram parar de descartar
metade dos frames (bug de UI drawer), fechar o gap de 6,3–7,8 ms com present `IMMEDIATE` e rotacionar 3 slots
de saída para não esperar 17 ms por Swap. Thread de apresentação própria ganhou FPS médio e piorou a
distribuição — foi rejeitada.

### Cited Findings

- **Metade dos frames era jogada fora**: um diálogo de conquistas invisível se registrou como UI drawer; com
  um drawer, o presenter pintava da UI thread e fundia frames, então **cada segundo frame renderizado nunca
  chegava à tela**. Corrigido, todo frame renderizado é apresentado
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/performance-history.md:85-91`).
- **Thread de apresentação própria**: +1,5 FPS na média, mas **3× mais variação** no frame time e **2,4× mais
  frames >50 ms**; ficou desligada porque frame time estável importa mais
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/performance-history.md:92-94`; `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/measuring.md:143-146`).
- **FIFO quantiza em 16,67 ms**: um frame de 34 ms é mostrado 50 ms. Mesmo com média de 33,3 ms, 50,9 % dos
  frames trocam de cadência; para zerar a variação são necessários **mediana ~28 ms (90 % dos frames ≤31 ms)
  + swap interval 2**. Qualquer um sozinho deixa ~9 ms de desvio
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/measuring.md:136-164`).
- **`IMMEDIATE` fecha o gap**: no Horizon, `nwindowSetSwapInterval(nw, 0)` não bloqueia o produtor e elimina
  os **6,3–7,8 ms** em que a GPU ficava idle enquanto o ring estava dentro da apresentação. O default do cvar
  é 0 justamente para não sobrescrever o modo da swapchain
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_ajustes_graficos.cpp:101-135`).
- **Custo de apresentar no ring**: **6,4–7,5 ms/frame** dentro da apresentação; a espera pelo output anterior
  custava **17 ms/Swap** (quase um vsync). Com 3 slots de saída, só espera se aquele slot ainda estiver
  pending (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_ajustes_graficos.cpp:89-93`; `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:591-593,5559-5571`).
- **Uma submission por frame**: o nfsmw mediu **um único `vkQueueSubmit` no fim, em `Presentar`**; dividir em
  duas peças aumentou o gap em 3,61 ms (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:707-727`;
  `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/platform-notes.md:178-183`).
- **Pacing hold (project8)**: o jogo começa o próximo frame assim que o interrupt de swap chega; com o
  renderer nativo o interrupt voltava imediatamente e os frames saíam 16,7/33/50 ms em sequência. Segurar o
  interrupt até N vblanks dá cadência estável (2 = 30 fps). O hold **antes** do present, depois de submeter os
  draws, evita o padrão “frame cedo na tela, próximo 50 ms depois”
  (`/Users/guillermemanhaes/gh3-refs/project8-sw/docs/native-renderer.md:55-65`;
  `/Users/guillermemanhaes/gh3-refs/project8-sw/src/game/src/native/thps_native_system.cpp:1167-1195`).
- **Late frame não espera o próximo vblank (project8)**: sem isso, todo frame atrasado era arredondado para
  múltiplo de 16,7 ms — **um frame de 36 ms virava 50 ms**. O hook libera só o frame atrasado; frames no prazo
  continuam esperando (`/Users/guillermemanhaes/gh3-refs/project8-sw/src/game/src/guest_swap_wait.h:124-153`;
  `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/native-renderer.md:80-81`).
- **Espera de vblank do guest**: 29 % das amostras da render thread, um terço de um core, só girando; o hook
  dorme em passos curtos e devolve o mesmo contador
  (`/Users/guillermemanhaes/gh3-refs/project8-sw/src/game/src/guest_swap_wait.h:112-123`).
- **Job wait do main thread**: 16 % das amostras num loop de job slot e 12 % no task scheduler; backoff
  spin→yield→sleep de 50 µs (`/Users/guillermemanhaes/gh3-refs/project8-sw/src/game/src/guest_job_wait.h:4-29,44-57`).
- **`sched_yield` não resolve**: no Steam Deck, yield deu 12,44 → 12,18 µs/draw, dentro do ruído; dormir
  ~100 µs cedeu o core de verdade (`/Users/guillermemanhaes/gh3-refs/project8-sw/src/game/src/guest_spin_yield.h:92-109`).
- O GH3 já tem `gh3_pace_before_present`, `gh3_min_frame_vblanks`, espera de vblank com sleep/yield e
  “late no wait” (`src/native/gh3_native_system.cpp:58-70,1167-1195`).

### Inferences

- **P0 — rotacionar slots de saída**: se o GH3 apresenta direto do ring e espera o output anterior, o padrão do
  nfsmw (3 slots, espera só se aquele slot estiver pending) vale ~17 ms/Swap. Verificar antes se o presenter do
  SDK já faz isso.
- **P1 — `IMMEDIATE` no Switch**: no Mac o `run-gh3.command` usa FIFO de propósito porque `immediate` em tela
  cheia trava o MoltenVK (`run-gh3.command:2-4`); no Switch, o nfsmw mediu que `IMMEDIATE` não rasga e fecha o
  gap. Não transportar a conclusão do Mac para o Switch.
- **P1 — não adotar thread de apresentação**: o único ganho medido foi +1,5 FPS médio com 3× mais variação;
  o GH3 é sensível a stutter de ritmo (jogo de ritmo), então a distribuição importa mais que a média.
- **P2 — medir a distribuição, não a média**: usar os buckets de 15/18/25/30/36/50/60/75/100/150 ms do nfsmw
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:1912-1927`) e reportar % >50 ms.

### Gaps

- Não há medição do custo de apresentação do GH3 no Switch; o número de 6,4–7,5 ms é do nfsmw.
- Não se sabe se o presenter do GH3 no Switch usa `IMMEDIATE`, FIFO ou mailbox; o `run-gh3.command` é só Mac.
- Não há dados de quantos frames o GH3 perde por quantização de vblank.

## 5. Quais correções de render-correctness estão documentadas — especialmente o que pode causar flashes breves de cor entre telas?

### Takeaway

As correções documentadas se agrupam em: **transformada/formato** (NDC para 2D/vídeo, normais, R/B swap,
gamma, 1/size de texturas resolvidas), **conteúdo de target** (resolve por swap e restauração, clear repetido,
alias de EDRAM, MSAA), **profundidade** (depth-only sem PS, formato de resolve, depth24) e **vídeo** (WMV3 por
FFmpeg, B frames, primeiro frame). “Flash de cor entre telas” não é nomeado como sintoma nos docs; os
mecanismos que o produzem estão documentados e são listados abaixo como inferência.

### Cited Findings

**Transformada, formato e swizzle**

- Vídeos e elementos 2D são desenhados em pixels (`PA_CL_VTE_CNTL = 0x400`); o tradutor aplica
  `oPos.xy = oPos.xy * g_NdcScale + g_NdcOffset * oPos.w`, com escala/offset por draw. Sem isso, um quad
  full-screen de vídeo/HUD amostra a área errada
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/shaders.md:45-51`).
- Normais/tangentes/binormais lidas como inteiros davam espelho de cor chapada e carro sem vinil; passaram a
  ser `float4` com formato SNORM/UNORM/float (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/shaders.md:52-55`).
- `PRED_SET_INV` sem o caso `src == 1` pulava blocos do shader de grama e deixava **uma faixa magenta na
  estrada**; a lição é comparar cada operação com Xenia (`ucode.h`) antes de depurar o renderer
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/shaders.md:56-60`).
- Alpha test com as 8 funções; draws depth-only com pixel shader 0 não podem ser pulados
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/shaders.md:61-63`).
- **R/B swap**: quando o resolve usa `copy_dest_swap`, o guest vê R e B trocados; o renderer aplica
  `kSwizzleBGRA` na view. Sem isso, “Mia saiu azul” (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:8453-8460`). O project8
  faz o mesmo no Swap, aplicando o swizzle do fetch constant 0
  (`/Users/guillermemanhaes/gh3-refs/project8-sw/src/game/src/native/native_renderer.cpp:4147-4155`).
- **Textura resolvida precisa reportar tamanho**: com `1/size` vindo das constantes, o caminho de textura
  resolvida retornava sem escrever o tamanho; 0 fazia todos os taps do PCF caírem no mesmo texel, e a borda
  cintilava com a câmera. O mesmo vale para o cubemap resolvido
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:8415-8440,8610-8620`).
- **Gamma ramp**: a saída do nfsmw aplica a rampa de 256 entradas do jogo, como o display do Xbox 360
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:12-15,616-617`).

**Clears, resolves e conteúdo de target**

- **Resolve por swap com conteúdo inválido**: se o target é resolvido por troca e o jogo resolve de novo sem
  desenhar no meio, o segundo resolve lê a imagem antiga. No menu, isso fazia o cenário amostrar o shadow map
  do frame anterior e **uma parede escurecer por um frame a cada ~4 frames**
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:129-158`). Correção: restaurar o conteúdo antes de resolver.
- **Swap de cor sem clear**: 16.807 swaps e 17.350 restores; o jogo redesenha por cima sem limpar, então a
  cópia é paga do mesmo jeito, depois e com duas operações. Com clear e target de 720 linhas, a tela encheu
  de “yellow smear” (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:91-128`).
- **Clear repetido**: 9 clears/frame (4 cor, 5 depth) custavam **0,67 ms reais**; pular clear com o mesmo
  valor quando nada foi desenhado desde o último é exato
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:361-382`).
- **Clear parcial**: cortar a área útil economizou só **0,09 ms** e foi desligado; o tracking no ring custa
  mais do que o ganho (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:382-414`).
- **Depth/front buffer lazy**: depth que ninguém amostra deixa de ser copiada (~0,35 ms); o front buffer é
  desenhado direto do render target (~0,35 ms) com variante exata de output
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:252-316`).
- **Cache de framebuffer com view destruída**: ao destruir uma imagem, framebuffers antigos ficavam no cache;
  se o Vulkan reusasse o handle, um framebuffer criado na view morta era devolvido — artefatos de cor
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:143-155`).
- **Fingerprint de shader × draw**: o céu adiado quebrou porque 7 draws compartilhavam o fingerprint do
  shader; o fingerprint identifica o shader, não o draw (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:885-911`).

**Profundidade, EDRAM e MSAA**

- **Depth resolve**: o project8 resolve depth por compute para uma imagem float; se o jogo amostra o resolve
  como formato não-depth, entrega as palavras que o resolve teria deixado na memória,
  `(depth24 << 8) | stencil` (`/Users/guillermemanhaes/gh3-refs/project8-sw/docs/native-renderer.md:37-39`).
- **Resolve no tamanho exato amostrado**: copiar para uma textura maior que a amostrada fazia o skatista
  sair dobrado com halo; o resolve passou a copiar exatamente o tamanho amostrado
  (`/Users/guillermemanhaes/gh3-refs/project8-sw/docs/native-renderer.md:33-36`; `/Users/guillermemanhaes/gh3-refs/project8-sw/docs/known-issues.md:46-47`).
- **MSAA e depth resolve**: com multisampling ligado, depth resolve de targets multisample **não é feito**;
  com `thps_native_msaa=false`, é (`/Users/guillermemanhaes/gh3-refs/project8-sw/docs/native-renderer.md:96-97`).
- **Alias de EDRAM no GH3**: um clear full-screen 4x em pitch 640 tinha ido para uma imagem própria e o alvo
  1x real nunca era limpo; o depth guardava todos os frames e o palco desenhava só luzes aditivas, “piled up
  to white” (`src/native/native_edram_samples.h:1-13`). O `gh3_native_alias_cap` limita o scissor de 8192
  para metade das linhas do EDRAM (`src/native/native_renderer.cpp:101-104,3184-3200`).
- **ZCULL e loadOp**: o GH3 só liga ZCULL quando o depth é limpo no primeiro passe (`loadOp=CLEAR`), porque o
  NVK exige isso; o nfsmw obtém ZCULL removendo `TRANSFER_DST` das imagens de depth
  (`src/native/native_renderer.cpp:98-100,2816-2817`; `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:574-590`,
  `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/mesa.md:49-74`).

**Vídeo, intro e frames**

- WMV3 do Xbox 360 rodava a **98 % de um core** e só ~22 fps de vídeo 720p30; passou a decodificar com FFmpeg,
  **7–15 ms/frame** (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/audio-and-video.md:128-138`).
- Vídeos com B frames/loop filter (tradução fan) saíam **verdes** no decoder recompilado e levavam até
  **330 ms/frame**; o FFmpeg decodifica, e se falhar repete o último quadro bom até o próximo I frame
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/audio-and-video.md:148-158`).
- Durante vídeos o jogo apresenta **30 fps**, então contadores de frame leem 30, não 60
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/audio-and-video.md:162`).
- Bink (project8): o primeiro frame aparece parcialmente decodificado, com **blocos verdes e magenta**, e
  congela; o renderer recebe os planos (1280×720 luma + 2×640×360 cor) todo frame. A causa provável é o
  áudio do Bink nunca avançar (`/Users/guillermemanhaes/gh3-refs/project8-sw/docs/known-issues.md:5-15`).

### Inferences

**Catálogo de causas prováveis de “flash breve de cor entre telas” no GH3**, todas com mecanismo documentado
nos portes (não há um bug do GH3 nomeado nos docs):

1. **Frame apresentado antes do resolve/conteúdo válido**: o nfsmw pinta “cor de teste” quando o Swap não tem
   textura resolvida (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:2119`); o GH3 tem o mesmo fallback de cor de teste quando
   o renderer não está pronto (`src/native/gh3_native_system.cpp:14-17,1220-1322`). Se uma transição cair
   nesse caminho, aparece um frame chapado.
2. **Resolve por swap lendo conteúdo antigo**: o caso do menu (parede escura por 1 frame) é o padrão exato de
   “flash” entre telas (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:129-158`). Se o GH3 adotar resolve-sem-cópia, precisa
   restaurar antes de qualquer resolve/desenho sem clear.
3. **Descriptor/view antigo com CPU 2 frames à frente**: o smear do leaderboard com 3 work slots
   (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:340-365`) pode aparecer como 1 frame de imagem velha. A correção é horizonte
   de validade por frame para alvos reescritos.
4. **Framebuffer de view destruída**: handle reusado devolve framebuffer velho e cores trocadas
   (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:143-155`).
5. **Work slot a mais que output slot**: “flickering and odd colors” por reusar output ainda na tela
   (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:520-555`).
6. **Vídeo/primeiro frame**: blocos verdes/magenta do Bink e vídeo B-frame sem I frame; manter o último quadro
   bom e nunca apresentar plano não decodificado (`/Users/guillermemanhaes/gh3-refs/project8-sw/docs/known-issues.md:5-15`;
   `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/audio-and-video.md:148-158`).
7. **Transformada 2D/vídeo errada**: quad full-screen em pixels sem `g_NdcScale/g_NdcOffset` amostra a região
   errada e pode dar um frame de cor sólida (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/shaders.md:45-51`).
8. **R/B ou gamma errados**: não é flash, mas um frame de cor trocada na transição de formato/resolve
   (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_dibujos.cpp:8453-8460`; `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/src/nfsmw_nativo_destinos.cpp:12-15`).

- **P0 — instrumentar “Swap sem textura resolvida”**: contar e logar (uma vez por endereço) no GH3; é o
  sintoma mais direto de frame chapado.
- **P0 — teste de transição com captura**: alternar menu→música→menu e comparar frames consecutivos; procurar
  1 frame de cor sólida, imagem velha ou parede escura. O nfsmw usou captura em pausa e A/B a cada 2 s
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/measuring.md:45-57`).
- **P1 — guard de conteúdo válido antes de resolve**: se adotar resolve-sem-cópia, implementar
  `RestaurarContenido` antes de resolver e antes de desenhar sem clear, com contador de restaurações.
- **P1 — não deixar o fallback de cor de teste aparecer em runtime**: no GH3 ele existe para “renderer não
  iniciou”; em runtime, preferir repetir o último frame bom a pintar cor sólida.

### Gaps

- Nenhum doc descreve explicitamente “flash de cor entre telas” como sintoma; a lista acima é inferência a
  partir de mecanismos documentados. Não foi possível reproduzir o GH3 no Switch para confirmar qual deles
  ocorre.
- Não há captura/log do GH3 que mostre um Swap sem textura resolvida ou restauração de conteúdo.
- O GH3 usa tradutor de shader em runtime; não se sabe se ele reproduz os bugs de `PRED_SET_INV`/normais do
  XenosRecomp ou se o tradutor do SDK já os trata.

## 6. Quais técnicas de build (PGO, ordenação de funções, LTO, O2 vs O3, seções) e ganhos medidos?

### Takeaway

O build foi o passo que mais rendeu depois do renderer: **chamadas diretas + LTO + PGO + ordenação de
funções** levaram a corrida de 30,3 para ~34 FPS, e as funções nativas do jogo para 35,4. O GH3 já tem
chamadas diretas, LTO, PGO e `-fno-tree-vectorize`; **não tem ordenação de funções** e mantém
`non_volatile_as_local = false`, que no nfsmw valia 23.857 chamadas de save/restore e 14 % do main thread. O
O2 vs O3 foi proposto, mas não há número publicado. O maior risco do build é LTO/PGO expor UB latente (o caso
xxHash).

### Cited Findings

- **Chamadas diretas**: `DEFINE_REX_FUNC` gera alias fraco, e o GCC não inlina através de weak alias nem com
  LTO. O script reescreveu **79.612 de 83.077 chamadas** para `__imp__sub_X`
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/toolchain.md:88-105`).
- **Registradores como locais**: `xer_as_local` removeu 172.403 usos de `ctx.xer`; `ctr_as_local` 32.637;
  `non_volatile_as_local` removeu **23.857 chamadas** a `__savegprlr_*`/`__restgprlr_*`. Só dois helpers
  (`__restgprlr_29`, `__savegprlr_29`) consumiam **~14 % do main thread**
  (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/toolchain.md:52-62`). O GH3 tem `non_volatile_as_local = false`
  (`config/gh3_functions.toml:2-5`).
- **LTO**: `-flto -fno-fat-lto-objects` no jogo e no app, link `-flto=2 -flto-partition=balanced`; o link
  precisa de muita memória (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/toolchain.md:107-111`; `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/CMakeLists.txt:206-222`). O GH3
  usa `-flto=6` (`cmake/switch.cmake:38-43,64-66`).
- **PGO**: build de gravação levou **66 min**, NRO de 82,7 MB, 16 MB de contadores; dump a cada 3 min via
  `__gcov_dump`/`__gcov_reset`. Armadilhas: `-fno-profile-values` obrigatório (o profiler de chamada indireta
  lê `tpidr_el0`, que é 0 no Horizon e derrubava antes do `main`) e `-fprofile-correction` obrigatório
  (contadores multi-thread ficam negativos) (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/toolchain.md:113-152`). O GH3 já replica as duas
  (`cmake/switch.cmake:67-98`).
- **PGO + LTO expuseram strict aliasing no xxHash**: `XXH3_64bits` foi inlineado no key builder e o GCC leu a
  chave antes do último write; as chaves levavam 4 bytes de lixo e as texturas subiam **10× mais**
  (**17.075 uploads contra 1.743**). Correção: `XXH_FORCE_MEMORY_ACCESS 0` + checagem em runtime + script que
  verifica o código compilado (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/toolchain.md:168-185`).
- **Ordenação de funções**: 53,5 MB de código, i-cache de 48 KB e TLB pequeno; código quente espalhado força
  page-table walk em cada jump longe. `--section-ordering-file` com lista de stack sampling. Armadilha: o
  linker coloca a lista **antes** do `KEEP(*(.crt0))`; 4 builds não bootaram com `memcpy` no endereço 0 e
  `_start` deslocado. A primeira linha de `.text` tem de ser `KEEP (*(.crt0))`, e todo release confere
  `_start == 0` (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/toolchain.md:154-166`; `/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/CMakeLists.txt:206-213,271-275`). O GH3
  não tem `--section-ordering-file` no `cmake/switch.cmake`.
- **Ganho combinado medido**: passo 5 (CPU por draw + 460,8 MHz) = 30,3 FPS; passo 6 (diretas + LTO + PGO +
  ordenação) = **~34 FPS**; passo 7 (funções nativas) = **35,4 FPS**. Não há ganho isolado de LTO, PGO ou
  ordenação publicado (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/performance-history.md:99-147`).
- **O2 vs O3**: `-O3` incha o código (53 MB) contra i-cache de 48 KB; existe a opção `NFSMW_RECOMP_O2`, mas
  **não há resultado medido** publicado (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/app/CMakeLists.txt:263-269`). **Proposto, não medido.**
- **Vetorização e correção**: o project8 desliga `-fno-tree-slp-vectorize -fno-tree-vectorize` no código
  recompilado porque o GCC fundia pares de cópias de 64 bits do contexto em q-register e uma page fault no
  meio voltava com **q31 zerado**, causando crash na loading screen. O GH3 já adotou
  (`/Users/guillermemanhaes/gh3-refs/project8-sw/src/game/cmake/switch.cmake:31-37`; `cmake/switch.cmake:34-37`).
- **Funções nativas do jogo**: material 2–4 % e efeito ~5 % do game thread; visibilidade 82.000 chamadas/s;
  matrizes 595→394 instruções e 265→78 acessos; resultado 35,4 FPS, pior trecho 27,6→30,0 e frames ≥60 ms
  caíram de ~24 para 7 (−70 %) (`/Users/guillermemanhaes/gh3-refs/nfsmw-nx/docs/performance-history.md:132-147`).

### Inferences

- **P0 — ordenação de funções no GH3**: o custo é quase zero (só posicionamento), o potencial é grande num
  binário recompilado grande, e o GH3 hoje não a usa. Fazer com lista de stack sampling no console, e incluir
  a checagem `_start == 0` no pipeline. **Ganho no GH3: proposto, não medido.**
- **P1 — `non_volatile_as_local = true`**: o nfsmw mediu 23.857 chamadas e 14 % do main thread; o GH3 mantém
  false. Exige marcar `share_registers` nas funções divididas e rodar `lee_antes.py`; risco de crash se
  faltar marca. Tratar como mudança de correctness, com teste de boot e de vídeo.
- **P1 — O2 no código recompilado**: testar A/B; a hipótese (código menor para i-cache de 48 KB) é plausível,
  mas não há número. Não ligar por fé.
- **P2 — perfil PGO representativo**: o perfil do nfsmw veio de um teste pequeno, não de uma gameplay
  completa; o GH3 deve gravar menus, músicas, vídeos e várias faixas antes de usar.
- **P2 — LTO jobs**: `-flto=6` no GH3 contra `-flto=2` no nfsmw; o limite é memória do linker, não CPU.
  Monitorar o pico e reduzir se o link começar a swapar.

### Gaps

- Não há ganho isolado de LTO, PGO ou ordenação de funções nos docs; o número de ~34 FPS é o pacote completo.
- Não há medição de `-O2` vs `-O3` no Switch.
- Não há medição do `non_volatile_as_local` no GH3; o número de 14 % é do main thread do NFSMW.
- Não há tamanho de código nem perfil de i-cache do GH3 publicados; a ordem de grandeza pode ser diferente.
