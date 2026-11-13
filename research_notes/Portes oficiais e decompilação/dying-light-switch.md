# Dying Light: Platinum Edition (Nintendo Switch, 2021) — como o port oficial foi feito

Escopo: **somente** a versão Switch (Tegra X1). Fontes consultadas em 2026-10-20. Convenção usada abaixo:
números em "Cited Findings" são **medição ou afirmação de terceiro citada com link**; tudo em "Inferences" é
**inferência minha**, não fato publicado. Quando a única fonte é comunidade/homebrew (GBAtemp), está marcado.

Nota de nomenclatura: o usuário mencionou "C-Engine". Para Dying Light 1 a engine é a **Chrome Engine**,
a engine histórica da Techland; "C-Engine" é o nome usado a partir de Dying Light 2. O
SteamDB lista Dying Light como `ChromeEngine` ([SteamDB](https://steamdb.info/app/239140/patchnotes)) e a
Wikipedia da série lista a tecnologia como "Chrome Engine"
([Wikipedia](https://en.wikipedia.org/wiki/Dying_Light_(series))). Isso importa: o port de Switch é uma
adaptação de uma engine de 2015 (DX11-era), não da C-Engine de 2022.

---

## 1. Quem fez o port e qual foi o ponto de partida?

### Takeaway
O port foi feito por **Virtuos (time de Shanghai) em parceria com a Techland**, sobre a **versão 1.43** do jogo
original, com todos os DLCs. A comunicação pública da Techland e a cobertura da imprensa creditam "Techland",
mas a Virtuos confirma participação e a comunidade encontrou anotações da Virtuos dentro dos arquivos de
configuração da engine.

### Cited Findings
- "Dying Light for Switch is based on **version 1.43** of the original game and brings most of its visual
  features to the table" — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle).
- A Virtuos "contributed to the development of Dying Light: Platinum Edition on the Nintendo Switch.
  Partnering with Techland, our team in **Shanghai** brought various **graphical** and Switch-only features
  such as motion and touch screen controls, HD rumble, and local co-op" —
  [Virtuos](https://www.virtuosgames.com/news/virtuos-partners-techland-to-deliver-nintendo-switch-exclusive-features-to-dying-light-platinum-edition/).
- A ficha de jogo do Gamepressure lista "Developer: Techland / **Virtuos (Switch)**" —
  [Gamepressure](https://www.gamepressure.com/games/dying-light/z43746).
- A Virtuos cita o port como um dos exemplos de adaptação difícil: "The Switch adaptation of *Dying Light*, a
  **resource-intensive PC game**, which was a **major challenge**" —
  [3DVF (entrevista com executivos da Virtuos)](https://3dvf.com/en/redaction/interview-meet-virtuos-a-major-player-in-the-gaming-industry).
- A comunidade que destravou os `.pak` relata "several notes left by **Virtuos**" sobre TAAU dentro dos
  arquivos da engine — [GBAtemp, thread de mod gráfico](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021)
  (fonte de comunidade, não oficial).
- Conteúdo: Platinum Edition inclui as 4 grandes DLCs (*The Following*, *Bozak Horde*, *Hellraid*,
  *Cuisine & Cargo*) e 17 skin bundles — [Nintendo Life](https://www.nintendolife.com/games/nintendo-switch/dying_light_platinum_edition);
  [Virtuos](https://www.virtuosgames.com/news/virtuos-partners-techland-to-deliver-nintendo-switch-exclusive-features-to-dying-light-platinum-edition/).
- O jogo é **content-complete**: "all of the features and gameplay complexities are preserved" —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle).

### Inferences
- A formulação da Virtuos ("contributed", "graphical and Switch-only features") sugere um modelo de
  co-desenvolvimento em que a Techland manteve a direção técnica da engine e a Virtuos executou a adaptação
  de plataforma e features de controle. A atribuição pública a "Techland" na imprensa é, portanto, parcial.
- Como o port parte do código-fonte e da cadeia de build da Chrome Engine, **não houve reescrita de engine**:
  houve porte/condicionamento da engine existente (DX11 → API de console) mais reautoria de conteúdo.

### Gaps
- Não há informação pública sobre **quantas pessoas** nem **quanto tempo** o port levou.
- Não há confirmação oficial de qual API gráfica o port usa (NVN proprietária da Nintendo é o caminho normal;
  nenhuma fonte consultada afirma isso explicitamente).
- Não existe GDC talk, post-mortem ou blog técnico assinado por engenheiros da Techland/Virtuos sobre este
  port. A "matéria-prima" técnica pública é basicamente a análise da Digital Foundry + arquivos do jogo.

---

## 2. O que mudou no renderer? (TAAU, resolução, shaders, LOD)

### Takeaway
A mudança estruturante foi **TAAU (temporal anti-aliasing with upscaling)**: o jogo renderiza interno bem
abaixo da saída e reconstrói 1080p docked / 720p handheld ao longo de vários frames. O config interno lido
pela comunidade aponta **1024×576** como resolução base — ou seja, ~30% dos pixels de 1080p — com o resto das
reduções sendo cortes seletivos (sombras, texturas, LOD, luzes dinâmicas), não uma reescrita do renderer.

### Cited Findings
- "Techland is using what seems to be a **new TAAU feature** — temporal anti-aliasing with upscaling. The idea
  here is to reconstruct the image over several frames to match the target output resolution: either **1080p
  in docked or 720p in portable**" —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle).
- "the actual base resolution is lower, **often counting around or below 720p in docked mode**" — mesma fonte.
- A resolução interna lida no arquivo de configuração é **1024×576**, e não é alterável pelo menu do jogo:
  "resolution in settings is at 576p, is not possible increase resolution, it's probably hardcoded in main
  because Techland TAAU technic" — [GBAtemp](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021)
  (comunidade).
- "there are several notes left by Virtuos about it [TAAU]. And yes, the blurry edges that appears sometimes
  are because TAAU" — [GBAtemp](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021)
  (comunidade).
- O parâmetro de **draw distance padrão é 0.6** (o mod sobe para 1.0/1.5); "Increased draw distance for
  textures and objects to 1.5, **default was 0.6**, seems to apply to **shadow cast distance** too" —
  [GBAtemp](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021) (comunidade).
- **Shadow map** de spot lights padrão **512**, subido para 1024 pelo mod "as it doesn't hit performance
  significantly" — mesma fonte (comunidade).
- O patch 1.0.2 **aumentou a resolução em handheld** e **capou o framerate em 30**: "Handheld resolution
  increased / framerate capped to 30" —
  [Nintendo Everything, patch notes 1.0.2](https://nintendoeverything.com/dying-light-switch-update-out-now-version-1-0-2-patch-notes/).
- O patch 1.0.4 (maio/2022) "optimized performance" novamente —
  [Nintendo Everything, 1.0.4](https://nintendoeverything.com/dying-light-update-out-now-on-switch-version-1-0-4-patch-notes/).
- O jogo mantém **depth of field, motion blur? não** — DF lista: "depth of field and other post-processing
  effects are intact"; mas "**per pixel motion blur** used on prior versions of the game has been eliminated" —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle);
  [resumo da DF feito pelo Nintendo Everything](https://nintendoeverything.com/dying-light-switch-tech-analysis-including-frame-rate/).
- "The engine seems **rebuilt around TAAU**" — [GBAtemp](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021) (comunidade).

### Inferences
- A afirmação "engine rebuilt around TAAU" é forte demais para o que se sabe: o que os arquivos mostram é
  **uso extensivo e hardcoded de TAAU**, com a resolução interna fixada. Isso é compatível com "adaptar o
  pipeline de post-process/upscale", não necessariamente reescrever a engine.
- 1024×576 × 4 ≈ 2,36 MP contra 2,07 MP de 1080p — ou seja, o TAAU entrega ~2× o número de pixels do buffer
  interno. É exatamente a matemática que explica "parece perto de nativo parado, quebra ao mover" descrito
  pela DF.
- Se a resolução base fosse dinâmica (DRS clássico), o menu mostraria valores variáveis; o fato de estar
  fixa em 576p sugere **resolução interna fixa + upscale temporal**, não DRS.

### Gaps
- Não se sabe se o TAAU foi implementado pela Virtuos, pela Techland, ou se veio de um middleware/licença.
- Não há informação publicada sobre **shaders específicos** reescritos (PBR, água, pele, foliage) nem sobre
  tradução de shader DX11/HLSL para o backend do Switch.
- Não há número oficial de contagem de draws, triângulos ou bandwidth.

---

## 3. CPU, memória, streaming de assets e compressão

### Takeaway
Não existe relato técnico público específico de threading/jobs/streaming deste port. O que existe é (a) a
metodologia genérica da Virtuos para Switch — G-buffer compacto, multithreading, resolução adaptativa — e
(b) evidência indireta nos arquivos: assets empacotados em `.pak` que são **ZIP renomeado**, com um
`defaultvideo_nx.scr` centralizando os parâmetros de vídeo. O carregamento ficou **mais rápido que no PS4**,
o que indica que o gargalo não foi I/O.

### Cited Findings
- Metodologia genérica da Virtuos para Switch (não específica de Dying Light): "If a developer's rendering
  pipeline uses deferred rendering, reducing the number of render targets in the G-Buffer can help with
  performance by reducing bandwidth usage. Virtuos achieved good results using **only two render targets**
  for its G-Buffer layout"; "the Switch CPU has a multicore configuration that requires games to be
  multithreaded… Engines that are predominantly single-threaded can pose some optimization challenges";
  "adaptive resolution is adopted as it is very useful for maintaining a constant framerate when the GPU is
  overloaded" — [Virtuos, Expert Talks](https://www.virtuosgames.com/news/expert-talks-nintendos-successful-switch-platform-provides-opportunity-for-developers-and-offers-an-install-base-of-close-to-20-million).
- A Virtuos também recomenda considerar **forward/forward+** em vez de deferred no Switch, pelo custo de
  bandwidth do G-buffer — mesma fonte.
- Sobre memória em outro port da Virtuos (XCOM 2): "we spent **half a year just to optimise the memory**…
  including using more efficient formats for files, removing needless memory usage" —
  [GameSpot](https://www.gamespot.com/articles/xcom-2-switch-dev-thinks-even-more-ps4-xbox-one-po/1100-6477686/).
  (Contexto de estúdio, **não** deste jogo.)
- Os `.pak` do Dying Light de Switch "are simply **renamed .zip** files"; `Data0.pak` contém "all the other
  settings" e `Data3.pak` contém `data/settings/defaultvideo_nx.scr` —
  [GBAtemp](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021) (comunidade).
- "elements like **loading times are actually comparable**. In fact, in my tests, the Switch version had
  **faster loading than the PS4 game**" —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle).
- Tamanho digital: listagens de eShop/ROM reportam **~14 GB** ([ROMsNSm](https://romsnsm.com/game/dying-light-platinum-edition))
  e um usuário cita **15,7 GB** na loja Nintendo ([Reddit](https://www.reddit.com/r/dyinglight/comments/qdhvli/switch_game_cartridge_size/)).
  Uma resposta de fórum afirma que a versão física traz o jogo completo no cartucho, com update de ~1,3 GB e
  ~1 GB por pacote de idioma ([GameFAQs](https://gamefaqs.gamespot.com/pc/211520-dying-light-content-drop-0/answers/600641-is-all-content-on-the-cartrdige-or-does-it-require-an-additional-download-of)).

### Inferences
- O uso de `.pak` = ZIP renomeado é típico da Chrome Engine e **não** indica compressão pensada para Switch:
  ZIP/deflate é barato de descomprimir mas ruim de taxa. Se houve recompressão de texturas, foi dentro dos
  arquivos, não no empacotamento.
- Carregamento mais rápido que PS4 num cartão SD/cartucho sugere streaming agressivo e/ou assets menores,
  não uma reengenharia de I/O.
- O fato de os parâmetros estarem num único `.scr` por plataforma (`_nx`) é consistente com um **sistema de
  configuração por plataforma** já existente na engine, explorado pelo port — e é o que permitiu à comunidade
  moddar draw distance e shadow map sem tocar em código.

### Gaps
- **Nenhum** dado público sobre número de threads, job system, orçamento de memória, taxa de compressão de
  textura (BC1/BC3/ASTC?), resolução de texturas, ou tamanho do streaming pool.
- Não há confirmação de que o port use ASTC (formato que a Virtuos costuma adotar em Switch); a única pista
  é o tamanho total.
- Não há informação sobre culling (oclusão, portals, cell loading) além do corte de luzes dinâmicas.

---

## 4. Quais compromissos de qualidade foram aceitos?

### Takeaway
O port manteve a **identidade visual e todo o conteúdo**, cortando orçamento de imagem de forma seletiva:
sombras menores e sem sombra de vegetação, texturas piores, motion blur removido, LOD/draw distance curto com
pop-in visível, luzes dinâmicas e reflexos reduzidos, animação de zumbis distantes em taxa menor, água mais
simples e **sem som surround**. O framerate ficou **destravado entre 30–36 fps** no lançamento (frame pacing
ruim) e só depois foi capado em 30.

### Cited Findings
- "Shadow resolution is **significantly lower**, shadows on vegetation are **gone**, texture quality takes a
  hit on both characters and world textures, while **motion blur is absent**" —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle).
- "distant detail and **dynamic lights are pared back**, producing a **pop-in effect much closer to the
  player**. However, depth of field and other post-processing effects are intact" — mesma fonte.
- Lista complementar da análise: AO "still on, but has been changed"; "**All distance detail has been toned
  down**"; "Some dynamic lights are **culled from view** and only appear as you approach them"; "**Water
  rendering reduced** in overall fidelity"; "**Animation rate can sometimes be reduced for zombies far
  enough** from the camera"; "**Reflections pared back** in certain instances"; "**Grass shadows removed**";
  "object pop-in"; "**No surround sound support** for audio" —
  [Nintendo Everything, resumo da análise da DF](https://nintendoeverything.com/dying-light-switch-tech-analysis-including-frame-rate/).
- Performance no lançamento: "uncapped… often running at circa **30-36fps**"; "inconsistent frame-times of
  **16ms, 33ms and 50ms**"; quedas reais abaixo de 30 "in the city at night" —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle);
  [Nintendo Life](https://www.nintendolife.com/news/2021/10/video-digital-foundrys-technical-analysis-of-dying-light-on-switch).
- O cap de 30 fps veio no **patch 1.0.2** (29/10/2021), junto com aumento da resolução handheld —
  [Nintendo Everything](https://nintendoeverything.com/dying-light-switch-update-out-now-version-1-0-2-patch-notes/).
- A DF recomendou explicitamente o cap: "if Techland can implement a decent 30fps cap, this should make the
  game look and feel smoother" — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle).
- Review de terceiro descreve o resultado como "fuzzy and blurry at times", rodando a 1080p/30 docked e
  720p/30 handheld — [Console Creatures](https://www.consolecreatures.com/review-dying-light).
- O patch 1.0.4 (2022) trouxe nova otimização de performance —
  [Nintendo Everything](https://nintendoeverything.com/dying-light-update-out-now-on-switch-version-1-0-4-patch-notes/).

### Inferences
- Ordem de prioridade do port, lida a partir dos cortes: **resolver a imagem (TAAU) > manter conteúdo e
  loading > manter iluminação/post-process "de assinatura" (DOF, AO) > sombras e view distance**. Cortar
  motion blur e sombra de grama rende muito por pouco custo perceptual — decisão clássica de port barato.
- O framerate destravado no lançamento parece **intencional** (margem acima de 30 em cena leve) e não um
  bug; o cap posterior troca fluidez máxima por consistência — exatamente o trade-off que a DF apontou.
- "Sem surround" é provavelmente corte de custo de mixing/CPU de áudio, não de memória.

### Gaps
- Sem números de resolução de textura, contagem de polígonos, draw distance em metros, nem resolução de
  shadow map oficial (o 512 vem de config modável, não de documentação).
- Não se sabe a resolução interna **após** o patch 1.0.2 (só que "aumentou" em handheld).
- Não há comparação oficial de duração de carregamento com números.

---

## 5. Houve entrevista, talk ou análise técnica?

### Takeaway
**Análise técnica: sim, e é a melhor fonte** — Digital Foundry, outubro de 2021, com artigo + vídeo. **Talk
de engenharia: não localizada** (nem GDC, nem post-mortem). **Entrevista técnica dedicada: não**. Existe um
vídeo de bastidores da Techland e uma nota curta da Virtuos, ambos de marketing, sem detalhe de engenharia.

### Cited Findings
- Artigo/vídeo: "Dying Light on Switch — another mobile miracle?" (John Linneman, 17/10/2021) —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle).
- Cobertura/quotização da análise — [Nintendo Life](https://www.nintendolife.com/news/2021/10/video-digital-foundrys-technical-analysis-of-dying-light-on-switch).
- A Techland publicou um vídeo de bastidores do port, coberto pela Nintendo Life ("Check Out This
  Behind-The-Scenes Look Into How Dying Light Was Ported To Switch", 25/10/2021) —
  [índice da Nintendo Life](https://www.nintendolife.com/companies/techland). O conteúdo é promocional; não
  foi possível recuperar a página original (404) para extrair detalhes técnicos.
- A Virtuos publicou uma nota de projeto citando "graphical and Switch-only features" —
  [Virtuos](https://www.virtuosgames.com/news/virtuos-partners-techland-to-deliver-nintendo-switch-exclusive-features-to-dying-light-platinum-edition/).
- Entrevista de 2025 com executivos da Virtuos classifica o port como "major challenge", mas sem detalhes —
  [3DVF](https://3dvf.com/en/redaction/interview-meet-virtuos-a-major-player-in-the-gaming-industry).
- Análise da comunidade com acesso aos arquivos (config, `.pak`, valores default) —
  [GBAtemp](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021).

### Inferences
- A ausência de talk é típica de port contratado: a Virtuos quase nunca publica post-mortem de trabalho
  cliente, e a Techland comunica o jogo, não a engenharia.
- O vídeo de bastidores provavelmente é a fonte das poucas declarações públicas da Techland sobre "quanto
  trabalho deu"; sem acesso ao conteúdo, não dá para citá-lo.

### Gaps
- Vídeo de bastidores não recuperado/transcrito.
- Nenhum GDC/CEDEC/devcom encontrado sobre o port.
- Nenhum dado oficial de Techland sobre TAAU, resolução interna ou orçamento.

---

## 6. Como isso difere de um port por recompressão estática (sem código-fonte)?

### Takeaway
A diferença não é "qualidade de execução", é **o que se pode mudar**. Com código-fonte e assets originais, a
Techland/Virtuos pôde (1) **mexer no pipeline de render** para introduzir TAAU e fixar resolução interna,
(2) **reautorar assets** (texturas, LODs, sombras de vegetação), (3) **mudar a lógica de simulação/streaming**
(taxa de animação por distância, culling de luzes), e (4) **reescrever o backend gráfico** para a API do
Switch. Uma recompressão estática (ReXGlue: PPC→C++→ARM64) herda o renderer e o comportamento do binário
original: você pode trocar o backend e o upscaler, mas não "pedir" ao jogo para usar menos sombras ou LODs
diferentes sem reescrever a camada que os consome.

### Cited Findings
- O port manteve o jogo "content-complete" e com "all of the features and gameplay complexities preserved",
  com loading comparável ou melhor que o PS4 —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle).
- As reduções foram feitas **no conteúdo e nos parâmetros da engine** (shadow resolution, texturas, motion
  blur, draw distance 0.6, shadow map 512, luzes dinâmicas), não apenas no output —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle);
  [GBAtemp](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021).
- A resolução interna está **hardcoded** e amarrada ao TAAU, o que só é possível com acesso ao código/build —
  [GBAtemp](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021).
- A Virtuos descreve o trabalho como adaptação de plataforma incluindo "performance optimizations" e
  "platform-specific features" — [3DVF](https://3dvf.com/en/redaction/interview-meet-virtuos-a-major-player-in-the-gaming-industry).
- Sobre o caso oposto (sem fonte): a própria Virtuos nota que engines "predominantemente single-threaded"
  exigem mudança de modelo de threading — algo inviável sem código —
  [Virtuos](https://www.virtuosgames.com/news/expert-talks-nintendos-successful-switch-platform-provides-opportunity-for-developers-and-offers-an-install-base-of-close-to-20-million).

### Inferences
- Um port estático pode, na melhor das hipóteses, reproduzir a **camada de apresentação** do truque
  (TAAU/FSR/DLSS-like no renderer nativo) e **podar trabalho no backend** (descartar draws, reduzir shadow
  map se o renderer for seu, mudar precisão). O que ele **não** consegue barato é o que a Techland fez no
  **conteúdo** (LODs, texturas, remoção de sombra de vegetação) e na **lógica** (animação por distância,
  culling de luzes).
- Ainda assim, parte dos cortes é observável e patchável por hooks: desligar motion blur, reduzir shadow map,
  cortar luzes dinâmicas por distância e limitar tick de animação são intervenções de renderer/engine que um
  port estático com renderer nativo próprio pode fazer — desde que exista um ponto de interceptação.
- O paralelo mais forte com o gh3-nx: o ganho da Techland veio de **desacoplar resolução de render da
  resolução de saída**. Um port estático que só sobe resolução paga o custo inteiro; com upscale temporal, o
  orçamento de pixel cai ~2×.

### Gaps
- Não há nenhum relato público de port estático vs. port com fonte comparando os mesmos cortes.
- Não se sabe quanto do TAAU veio pronto de middleware e quanto foi escrito à mão.

---

## 7. O que deste port é transferível para o gh3-nx (recompressão estática + Vulkan/NVK)?

### Takeaway
Transferível: **TAAU como multiplicador de orçamento de pixel**, a **disciplina de cortes baratos e
perceptualmente invisíveis** (motion blur, sombra de vegetação, animação distante, luzes dinâmicas), e a
**centralização de parâmetros por plataforma** num arquivo de config. Não transferível de forma barata:
reautoria de assets e mudanças na lógica de jogo/simulação. O maior risco é o mesmo do Dying Light: TAAU
troca nitidez estática por quebra de imagem em movimento — e no GH3, jogo de ritmo, **artefato temporal em
câmera rápida é pior** que em um FPS de parkour.

### Cited Findings
- O ganho de imagem do port veio de TAAU reconstruindo 1080p/720p a partir de base "around or below 720p" —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle).
- O custo do TAAU é explícito: "looks reasonably close to native when standing still but **as you move,
  you'll notice some image break-up**" — mesma fonte.
- Cortes de alto retorno e baixo custo perceptual usados: motion blur removido, sombra de vegetação removida,
  animação de zumbis distantes reduzida, luzes dinâmicas culled, draw distance 0.6 —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle);
  [Nintendo Everything](https://nintendoeverything.com/dying-light-switch-tech-analysis-including-frame-rate/);
  [GBAtemp](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021).
- Parâmetros centralizados em `defaultvideo_nx.scr` dentro de `Data3.pak`, com defaults ajustáveis sem
  recompilar — [GBAtemp](https://gbatemp.net/threads/dying-light-platinum-edition-graphics-mod.605021)
  (comunidade).
- O cap de 30 fps só veio depois, e foi a mudança que a DF considerou necessária para consistência —
  [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2021-dying-light-on-switch-another-mobile-miracle);
  [Nintendo Everything](https://nintendoeverything.com/dying-light-switch-update-out-now-version-1-0-2-patch-notes/).

### Inferences
- Para o gh3-nx, o análogo direto é implementar **upscaling temporal no renderer nativo Vulkan** (jitter de
  projeção + histórico + clamp de vizinhança) e reduzir a resolução de render interna, em vez de perseguir
  1080p nativo no NVK.
- O análogo do `defaultvideo_nx.scr` é expor um **arquivo de tuning por renderer/plataforma** (shadow map,
  draw distance, motion blur on/off, tick de animação) para iterar sem rebuild de 6–11 min.
- O que **não** copiar: o port aceita "image break-up" em movimento; em GH3 a leitura de notas e a nitidez de
  HUD/texto importam mais, então o alvo deve ser TAAU com componente espacial forte (ou DLSS-like/FSR2) e
  teste específico de ghosting em scroll de pista.
- Onde o port oficial teve vantagem injusta: pôde **remover sombra de grama, trocar LOD e reautorar
  texturas**. Num port estático isso vira trabalho manual de asset (ou fica de fora) — logo, o teto de
  performance do gh3-nx deve ser comparado com o teto do Dying Light **menos** esses ganhos de conteúdo.

### Gaps
- Não há medição pública de quanto do ganho do Dying Light veio de TAAU vs. cortes de conteúdo; sem isso, não
  é possível estimar quanto do delta é replicável.
- Não há dados de custo (ms de GPU) do TAAU no Tegra X1 neste jogo.
