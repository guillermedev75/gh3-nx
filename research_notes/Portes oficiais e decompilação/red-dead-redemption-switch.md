# Port oficial de Red Dead Redemption (Switch, 2023) — engenharia do port

Escopo: apenas o port de **RDR1 (2010) para Nintendo Switch**, lançado em 17/08/2023, e não RDR2 nem as versões PC/PS4/PS5 — exceto quando a comparação ajuda a explicar uma decisão do Switch. Base de evidências: análise frame-a-frame da Digital Foundry (Thomas Morgan, 20/08/2023), a análise da DF da versão de PS4, o blog do Ryujinx (set/2023), o vídeo do Modern Vintage Gamer (21/08/2023) e material oficial (Rockstar/Take-Two). **Não existe** talk de GDC, entrevista técnica profunda nem post-mortem público do port; quase tudo que se sabe sobre renderer vem de inspeção de imagem e de comportamento observável. Onde não há fonte, está marcado como inferência ou gap.

Convenção usada abaixo: **[medido]** = medição de terceiro (DF, MVG, Ryujinx); **[citado]** = declaração oficial/imprensa; **[inferência]** = dedução minha a partir dos dados.

---

## 1. O que foi preciso no renderer e quem fez o port

### Takeaway
O port foi feito pela **Double Eleven** (Middlesbrough, Reino Unido), com apoio da **Rebellion North**, como uma **conversão nativa** do RAGE para ARM64/NVN — não emulação nem wrapper. A estratégia de renderer foi conservadora: manter o jogo de 2010, subir a resolução para 1080p docked / 720p handheld, trocar 2xMSAA por FXAA, reduzir sombras e filtragem anisotrópica e **não** usar resolução dinâmica nem upscaling temporal. O ganho veio de rodar o mesmo conteúdo em resolução maior, não de refazer o renderer.

### Cited Findings
- Port feito por **Double Eleven Studios**, com suporte da **Rebellion North**; instalação de **11,4 GB** com campanha + Undead Nightmare e **sem multiplayer** — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port)
- Rockstar descreve o produto como **"new conversion by Double Eleven Studios"**, "crafted specifically for the PlayStation 4 and Nintendo Switch", **30 FPS**, **até 1080p no Switch**, até 4K no PS4 Pro e 4K no PS5 via retrocompatibilidade — [Take-Two/Rockstar, press release 17/08/2023](https://ir.take2games.com/node/29806/pdf)
- Double Eleven foi fundada em 2009/2010 por ex-desenvolvedores da Rockstar; o port é listado no site da própria Double Eleven como projeto para **Switch, PS4 e PC** — [The Verge](https://www.theverge.com/2023/8/7/23811044/red-dead-redemption-remake-rockstar-games-switch-ps4); [Double Eleven](https://double11.com/games/red-dead-redemption/); [GamesRadar+](https://www.gamesradar.com/why-has-rockstar-brought-red-dead-redemption-to-the-switch-and-ps4-in-2023/)
- Créditos de portabilidade: **Double Eleven Limited + Rebellion North** — [MobyGames, releases de Switch](https://www.mobygames.com/game/207074/red-dead-redemption/releases/)
- **Resolução nativa fixa**: 1920×1080 docked, 1280×720 handheld; "não há resolução dinâmica" segundo o MVG — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port); [Modern Vintage Gamer, 21/08/2023](https://www.youtube.com/watch?v=TCJrC9KRnrQ)
- **Sem upscaling temporal no Switch**: o PS4 tem opção de **FSR2**; o Switch usa **FXAA**, que a DF considera pior que o 2xMSAA do Xbox 360 original — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port)
- **Sombras**: melhores que no 360, porém **piores que no PS4** (resolução menor, mais flicker e ruído em sombras dinâmicas do sol) — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port)
- **Filtragem de textura**: qualidade "low sample anisotropic", pior que o próprio Xbox 360 e que o PS4; texturas em ângulo raso ficam borradas — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port)
- **Assets e LOD**: texturas idênticas às de 2010 ("right down to character faces"), LOD de geometria e vegetação **idêntico ao do 360** (mesma distância de pop-in); densidade de folhagem igual à do PS4; draw distance igual à do PS4, com leve vantagem do PS4 em alguns casos — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port)
- A DF de 2025 resume: "a versão de 2023 do Switch 1 **largamente usou a versão de Xbox 360 como base de settings visuais**, mas a resolução maior em 1080p docked veio ao custo de AA pior: FXAA em vez do 2x MSAA do 360" — [Digital Foundry, análise do Switch 2, 10/12/2025](https://www.digitalfoundry.net/news/2025/12/red-dead-redemption-on-switch-2-a-great-dlss60fps-combo)
- O jogo é um **app nativo** que chama funções do SDK da Nintendo: a ferramenta ReverseNX precisou ser corrigida por causa de "como Red Dead Redemption usa o SDK para detectar modo docked/handheld" — [GBAtemp, thread oficial do ReverseNX](https://gbatemp.net/threads/reversenx-flag-your-console-as-handheld-or-docked.543625/page-49)
- O mod de 60 FPS é um **patch de instruções ARM64** no executável (ex.: `0017F530 28008052`), o que confirma código nativo compilado para ARMv8 — [GameBanana, 60FPS Mod](https://gamebanana.com/mods/464053)
- Evidência de renderer no nível do backend: o Ryujinx precisou corrigir um bug "Vulkan-specific com **masked stencil clears**" que fazia a folhagem não renderizar (efeito "psicodélico") — [Ryujinx, Progress Report jul/ago 2023](https://blog.ryujinx.org/progress-report-july-august-2023/)
- O lead programmer do port (Matt Phillips, Double Eleven) lista em suas especialidades **"graphics programming (DirectX, Gnm, Agc, Nvn)"** e descreve seu cargo como "Lead Programmer on Red Dead Redemption for PC/Switch/PS4" — [LinkedIn](https://linkedin.com/in/mattphillips1); [Bluesky](https://bsky.app/profile/did:plc:wqsrhzwlo53ezhgt5bdtmbkb)
- Double Eleven também mexeu no **áudio de baixo nível**: um audio programmer credita no RDR "Audio Code support, low-level audio engine (DSP/Effects/Filters/**New-gen SDKs**)" — [LinkedIn, Felype Fernandes](https://linkedin.com/in/felypefernandes)

### Inferences
- **[inferência, alta confiança]** O port é uma recompilação do RAGE/RDR1 para ARM64 usando **NVN** (API gráfica de baixo nível da Nintendo), e não uma camada de tradução. Os indícios são: patch de instruções ARM64 no executável, chamadas ao SDK para detectar docked/handheld, correção específica de "masked stencil clears" no emulador (recurso de NVN) e o histórico do lead programmer com NVN.
- **[inferência]** A troca de **2xMSAA → FXAA** é uma decisão clássica de economia de **banda de memória** em GPU móvel/tile-based: MSAA multiplica tráfego de cor/depth por amostra; FXAA é um passe de pós-processamento barato em banda. Faz sentido num chip com 25,6 GB/s compartilhados.
- **[inferência]** O renderer do RDR1 é pesado em **alpha/folhagem** (o analysis da época documenta uso de alpha-to-coverage e dependência de banda), o que explica por que a filtragem anisotrópica foi cortada e por que a resolução de sombra caiu: são os dois lugares onde cortar rende mais banda.
- **[inferência]** Não houve "remaster" de renderer: sem PBR, sem AO novo, sem novas texturas, sem DRS, sem TAA/FSR. O trabalho foi **portar código e pipeline**, não atualizar o look. A DF chama o resultado de "surpreendentemente reduzido ao essencial".
- **[inferência]** O corte do multiplayer removeu do escopo toda a pilha de rede do RAGE (sessões, matchmaking, sincronização, serviços online da era 360), o que reduz risco de certificação e superfície de bugs — mas não há fonte oficial dizendo isso.
- **[inferência]** O fato de a versão de Switch 2 (2025) ser um "app nativo separado" com DLSS, 16x AF, LOD maior e CHSS mostra que o port de 2023 foi deliberadamente **mínimo**, não um limite técnico do RAGE no Switch.

### Gaps
- Não há informação pública sobre o **renderer interno** do RDR1 no Switch: deferred vs. forward, formato de G-buffer, uso de compute, número de render targets, técnica de sombra (cascaded? PSSM?), se há oclusão/portal culling.
- Não se sabe se o port manteve o **pipeline de assets do RAGE** (RPF) ou converteu para outro formato; nem o formato de compressão de textura usado no Switch (BCn? ASTC?).
- Não há confirmação oficial de que o jogo usa NVN (é inferência forte, não declaração).
- Não há dados públicos de **threading** (quantas threads, job system, uso dos 3 cores A57 disponíveis), nem de orçamento de streaming.
- Não há post-mortem, GDC talk ou entrevista da Double Eleven especificamente sobre RDR1 Switch.

---

## 2. Técnicas de CPU, memória e engine

### Takeaway
Não há engenharia de CPU/memória documentada publicamente. O que os testes mostram é um port **limitado por banda de memória/GPU**, não por CPU: sem overclock, com o cap removido, o jogo já faz 30–45 FPS em handheld a 720p, e só chega perto de 60 com memória a ~1,8 GHz. O RAGE continuou sendo RAGE — streaming de mundo aberto, Euphoria/Bullet, ticks de 30 Hz — com orçamentos reajustados para o Tegra X1.

### Cited Findings
- Especificação do Switch: **4× ARM Cortex-A57 a 1.020 MHz** (um core reservado ao sistema), GPU Maxwell de **256 CUDA cores** a **307,2/384 MHz handheld** e **768 MHz docked**, **4 GB LPDDR4** compartilhados, **25,6 GB/s** de banda — [Digital Foundry, review do Switch](https://www.digitalfoundry.net/articles/digitalfoundry-2017-nintendo-switch-review); [Digital Foundry, análise de specs](https://www.digitalfoundry.net/articles/digitalfoundry-2016-nintendo-switch-spec-analysis)
- O RAGE é descrito como engine com força em **"large streaming worlds, complex A.I. arrangements, weather effects, fast network code"**, com middleware **Euphoria** (animação) e **Bullet** (física); RDR1 é listado como jogo RAGE da Rockstar San Diego — [Wikipedia, RAGE](https://en.wikipedia.org/wiki/Rage_engine)
- O Switch original renderiza RDR1 a **720p/1080p** e, segundo a DF, depende de forma crítica da **banda de memória**; a DF documenta que, no Switch, subir CPU/GPU sem mexer na memória rende pouco em jogos limitados por banda — [Digital Foundry, análise de overclock do Switch](https://www.digitalfoundry.net/articles/digitalfoundry-2019-switch-overclocking-analysis)
- **[medido]** Com o cap de FPS removido e **sem overclock**, em handheld 720p, RDR1 roda entre **30 e 45 FPS** — [Modern Vintage Gamer](https://www.youtube.com/watch?v=TCJrC9KRnrQ); corroborado por [Nintendo Life](https://www.nintendolife.com/news/2023/08/video-heres-a-look-at-red-dead-redemption-running-at-60fps-on-switch)
- **[medido]** Para chegar perto de 60 FPS foi preciso: CPU a **1.785 MHz**, GPU a **614–921 MHz** e **memória a ~1,8 GHz** (Erista); o MVG aponta a **memória** como o gargalo principal — [Modern Vintage Gamer](https://www.youtube.com/watch?v=TCJrC9KRnrQ); [OC3D](https://overclock3d.net/news/software/rdr_at_60_fps_switch_gamers_are_playing_red_dead_redemption_at_60_fps_with_overclocking/)
- **[medido]** No Switch 2 (2025), o mesmo jogo vai a **60 FPS** com **DLSS de 720p→1440p docked** e **540p→1080p handheld**, com 16x AF, LOD maior e contact-hardening shadows — ou seja, o conteúdo escala quando há mais GPU/banda — [Digital Foundry](https://www.digitalfoundry.net/news/2025/12/red-dead-redemption-on-switch-2-a-great-dlss60fps-combo)
- Tamanho: **11,4 GB** no Switch contra **6,7 GB** (disco do 360) e **7,67 GB** (PS3) do original — [Digital Foundry, Switch](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port); [Digital Foundry, Face-Off de 2010](https://www.digitalfoundry.net/articles/digitalfoundry-red-dead-redemption-face-off)
- A Double Eleven descreve seu método de port como reengenharia do código existente, não reescrita: em Rust console eles "tiveram que reengenheirar como o jogo inteiro funcionava" partindo de um ponto do codebase — [PlayStation Universe, entrevista com Matt Phillips](https://www.psu.com/news/rust-console-edition-interview-what-did-it-take-to-get-rust-running-on-ps4/)
- A Double Eleven declara usar **C++** e ter como prática "otimizar código e assets, enxugar uso de memória e implementar técnicas de renderização mais eficientes" em ports — [TechTRP, entrevista com Mau Iida](https://techtrp.com/gaming/2024/08/06/behind-the-mind-of-double-elevens-game-programmer/)

### Inferences
- **[inferência, alta confiança]** O port é **bandwidth-bound** em 1080p: o ganho de 30→45 FPS só removendo o cap, e o salto para ~60 com overclock de memória, indicam que a GPU não está saturada de ALU, e sim de tráfego (cor, depth, texturas, alpha).
- **[inferência]** A CPU do Switch (3× A57 fora do SO, OoO, 64-bit) é provavelmente **mais capaz** que a do Xenon do 360 para o gameplay do RDR1 (lógica, IA, streaming); o problema não é o tick de 30 Hz, é alimentar a GPU. Isso é consistente com os drops em cenas de muitos NPCs (custo de draw call/skinning/IA) serem pontuais, não generalizados.
- **[inferência]** O delta de tamanho 6,7 → 11,4 GB com **as mesmas texturas** sugere que o crescimento vem de **áudio** (múltiplos idiomas novos: chinês, coreano, polonês, português-BR, russo, espanhol LATAM), vídeos/cutscenes e/ou menor compressão para melhorar tempo de load no eMMC/cartucho. Não há confirmação.
- **[inferência]** O streaming do RAGE (RPF, células do mundo, LOD por distância) foi mantido com os **mesmos raios de LOD do 360**; o port não tentou "esticar" draw distance. Isso é uma decisão de orçamento: manter o custo de streaming e gastar o ganho de hardware em resolução.
- **[inferência]** Sem DRS e com cap fixo de 30, o port evita oscilação de resolução e mantém frametime previsível em 33,3 ms — mais fácil de certificar e de manter estável num hardware com banda apertada. A contrapartida são drops para "high 20s" em cenas pesadas em vez de queda de resolução.
- **[inferência]** O uso de **FXAA** em vez de MSAA também reduz pressão no **tile memory**/framebuffer do Maxwell, não só banda; é a escolha típica de port Switch de 7ª geração.

### Gaps
- Nenhum dado público sobre **número de threads**, job system, distribuição CPU/GPU, ou uso de async compute.
- Nenhum dado sobre **streaming**: tamanho de célula, buffers, prioridade, tempo de load, uso de I/O assíncrono, tamanho do cache.
- Nenhum dado sobre **compressão de textura/áudio** no Switch, nem se houve reencode de assets.
- Não se sabe o consumo de memória do jogo no Switch (quanto dos ~3,2 GB disponíveis é usado), nem se houve redução de pools.
- Não há medição de **frametime** publicada além da DF (que cita picos de 16 ms — número estranho para um alvo de 33,3 ms, possivelmente erro de transcrição).

---

## 3. Compromissos de qualidade aceitos

### Takeaway
O alvo foi "360 com resolução maior": 1080p fixo docked, 720p fixo handheld, 30 FPS travado, assets de 2010, LOD de 2010, sem multiplayer. As concessões ficaram concentradas em **AA, sombras e filtragem anisotrópica** — exatamente os três itens que mais gastam banda. Nos pontos pesados (saloon à noite, batalha de fazenda) cai para a casa dos 20 FPS; Undead Nightmare, apesar de mais pesado, ficou estável.

### Cited Findings
- **[medido]** Alvo de **30 FPS** com V-Sync; "grande parte do jogo" fica na linha de 30; exceções: chegada ao **saloon de Armadillo à noite** (abaixo de 30) e uma **batalha de fazenda** com muitos NPCs (high 20s); pacing irregular ocasional com "frame-time spikes to 16ms" — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port)
- **[medido]** Em **Undead Nightmare**, o cemitério com muitos zumbis roda "absolutamente sólido a 30" no Switch, enquanto no 360 original caía perto de 20 FPS com tearing — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port)
- **[medido]** Em handheld, 720p/30 "perfeitamente", com a mesma exceção da batalha de fazenda (high 20s) — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port); [Handheld Performance](https://handheld-performance.com/games/red-dead-redemption)
- **HUD/mapa permanecem em 720p**, como no 360/PS3 originais — [Digital Foundry, vídeo](https://www.youtube.com/watch?v=RhhkHHsEzL4)
- **Sem multiplayer** no pacote (só campanha + Undead Nightmare) — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port); [TechSpot](https://www.techspot.com/news/99694-red-dead-redemption-coming-playstation-4-nintendo-switch.html)
- **Novos idiomas** (chinês simplificado/tradicional, coreano, polonês, português-BR, russo, espanhol LATAM) e **achievements in-game** no Switch, além de conteúdo da GOTY Edition — [Take-Two/Rockstar, press release](https://ir.take2games.com/node/29806/pdf)
- Comparação de baseline: 360 = **1280×720 com 2xMSAA**, sem cap, com tearing; PS3 = **1152×640 com QAA**, 30 FPS com V-Sync; PS4 = 1080p com FSR2; Switch = 1080p com FXAA — [Digital Foundry, Face-Off 2010](https://www.digitalfoundry.net/articles/digitalfoundry-red-dead-redemption-face-off); [Digital Foundry, PS4](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-playstation-has-one-impressive-upgrade); [Digital Foundry, Switch](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port)
- O original dependia de **alpha-to-coverage** para folhagem e de **~4x AF** (estimativa da época), com forte custo de banda em transparências — [IQGamer, análise técnica de 2010](http://imagequalitymatters.blogspot.com/2010/05/tech-analysis-red-dead-redemption-360.html)
- Patches do Switch: **1.02** (04/10/2023, "general bug fixes and improvements") e **1.04** (12/03/2024, tela de ToS + estabilidade); nunca houve patch de 60 FPS — [Rockstar Support](https://support.rockstargames.com/articles/4Kzr5DrodN1JYu4mixkMxE/red-dead-redemption-title-update-notes-ps5-ps4-xbox-series-x-or-s-switch-pc); [Perfectly Nintendo](https://www.perfectly-nintendo.com/red-dead-redemption-switch-all-the-updates/)
- PS4/PS5 ganharam opção de 60 FPS em 1.03 (out/2023); o Switch não — [Rockstar Support](https://support.rockstargames.com/articles/4Kzr5DrodN1JYu4mixkMxE/red-dead-redemption-title-update-notes-ps5-ps4-xbox-series-x-or-s-switch-pc)

### Inferences
- **[inferência]** Os drops em cenas com **muitos NPCs** (fazenda, saloon) apontam para custo de **CPU/draw calls/IA/animação**, não para fill rate — é o único tipo de carga em que o port não segura 30.
- **[inferência]** O saloon à noite combina muitas luzes dinâmicas + sombras + interiores, o que volta a bater em banda e em resolução de shadow map; é consistente com a redução de qualidade de sombra documentada pela DF.
- **[inferência]** Manter o **HUD em 720p** é sintoma de port mínimo: a UI foi deixada como estava, sem re-render em resolução de tela. Para o usuário (GH3), é um alerta: UI/HUD precisa ser reescalada explicitamente, senão fica borrada em 1080p.
- **[inferência]** O cap de 30 é escolha de **orçamento de frametime**, não de impossibilidade: sem cap e sem OC o jogo já faz 30–45. Um cap de 40 ou um modo 40 FPS seria possível, mas a Nintendo não tem modo 40 Hz e o custo de QA/stability não compensava.

### Gaps
- Não há medição pública de **frametime completo** (só observações pontuais da DF), nem de draw distance em metros, nem de contagem de NPCs/draw calls.
- Não se sabe a **resolução interna das sombras** nem o número de cascatas.
- Não há comparação de **tempo de load** Switch vs. 360 vs. PS4.
- Não há informação sobre qualidade de áudio (codec, taxa) — só se sabe que há novos idiomas.
- Não há dados sobre o **modo handheld** além de 720p/30: clocks escolhidos, se usa 384 MHz ou 307 MHz.

---

## 4. Análise técnica e material oficial disponíveis

### Takeaway
A fonte primária de engenharia é a **Digital Foundry** (texto + vídeo), complementada pelo **Ryujinx** (comportamento do renderer) e pelo **MVG** (teste de headroom). O material oficial (Rockstar/Take-Two) só confirma plataformas, resolução, framerate, conteúdo e patches — não descreve processo. Não existe entrevista técnica/deep dive oficial sobre o port de Switch.

### Cited Findings
- **Digital Foundry, "Red Dead Redemption on Switch: an impressive, creditable port"**, Thomas Morgan, 20/08/2023 — a análise de referência: resolução, AA, sombras, texturas, LOD, performance e comparação com 360/PS4 — [texto](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port); [vídeo](https://www.youtube.com/watch?v=RhhkHHsEzL4)
- **Digital Foundry, análise do PS4** (Oliver Mackenzie, 17/08/2023) — estabelece o baseline de settings do port (FSR2, sombras, SSR) e antecipa que o Switch roda 1080p docked — [texto](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-playstation-has-one-impressive-upgrade)
- **Digital Foundry, análise do PC** (Alex Battaglia, 07/11/2024) — mostra o que a Double Eleven fez quando teve orçamento de PC: DX12, DRS, DLSS/FSR, frame gen, draw distance maior, sombras com contact hardening, shader precompilation — [texto](https://www.digitalfoundry.net/articles/digitalfoundry-2024-red-dead-redemption-pc-tech-review)
- **Digital Foundry, análise do Switch 2** (Tom Morgan/Sam Machkovech, 10/12/2025) — confirma que o Switch 1 usou o 360 como base visual e mostra o contraste com um app nativo novo (DLSS, 60 FPS, 16x AF, CHSS) — [texto](https://www.digitalfoundry.net/news/2025/12/red-dead-redemption-on-switch-2-a-great-dlss60fps-combo)
- **Ryujinx, Progress Report jul/ago 2023** — relata o bug de masked stencil clears em Vulkan que impedia a folhagem de renderizar e diz que, depois do fix, o jogo ficou "tão perto de flawless quanto ousamos chamar" — [blog](https://blog.ryujinx.org/progress-report-july-august-2023/)
- **Modern Vintage Gamer, "Red Dead Redemption can hit 60fps on Nintendo Switch"** (21/08/2023) — teste com FPSLocker/ReverseNX/sys-clk; mede 30–45 FPS sem OC e ~60 com OC; aponta memória como gargalo — [vídeo](https://www.youtube.com/watch?v=TCJrC9KRnrQ); cobertura: [Kotaku](https://kotaku.com/red-dead-redemption-60fps-switch-price-ps5-remaster-1850757788); [Nintendo Life](https://www.nintendolife.com/news/2023/08/video-heres-a-look-at-red-dead-redemption-running-at-60fps-on-switch)
- **Rockstar/Take-Two** — press release de lançamento com 30 FPS, 1080p no Switch, 4K no PS4 Pro/PS5, conteúdo GOTY e idiomas — [Take-Two IR](https://ir.take2games.com/node/29806/pdf)
- **Rockstar Support** — histórico de title updates (1.02 Switch, 1.04 PS4/Switch, 1.03 PS5 com 60 FPS) — [Rockstar Support](https://support.rockstargames.com/articles/4Kzr5DrodN1JYu4mixkMxE/red-dead-redemption-title-update-notes-ps5-ps4-xbox-series-x-or-s-switch-pc)
- **Ficha técnica/estúdio** — Double Eleven + Rebellion North nos créditos de port; Double Eleven lista Switch/PS4/PC — [MobyGames](https://www.mobygames.com/game/207074/red-dead-redemption/releases/); [Double Eleven](https://double11.com/games/red-dead-redemption/)
- **Contexto de estúdio** — fundada por ex-Rockstar, histórico de ports (LittleBigPlanet Vita, Limbo, Prison Architect, Rust, Minecraft Dungeons), ~400 funcionários — [The Verge](https://www.theverge.com/2023/8/7/23811044/red-dead-redemption-remake-rockstar-games-switch-ps4); [GamesRadar+](https://www.gamesradar.com/why-has-rockstar-brought-red-dead-redemption-to-the-switch-and-ps4-in-2023/); [Built In, vaga de graphics programmer](https://builtin.com/job/senior-graphics-programmer/9181180)
- **Entrevistas do estúdio (não específicas de RDR)** — Matt Phillips sobre reengenharia de código em ports (Rust) e Mau Iida sobre otimização de memória/renderização — [PSU](https://www.psu.com/news/rust-console-edition-interview-what-did-it-take-to-get-rust-running-on-ps4/); [TechTRP](https://techtrp.com/gaming/2024/08/06/behind-the-mind-of-double-elevens-game-programmer/)

### Inferences
- **[inferência]** A DF não teve acesso a build de debug, settings nem código; todas as conclusões dela vêm de captura e contagem de pixels. Portanto "sem DRS" e "FXAA" são observações robustas, mas "LOD idêntico ao 360" é comparação visual, não leitura de configuração.
- **[inferência]** A ausência de material oficial sobre o processo é típica de port contratado: Rockstar tratou como "conversion", sem campanha de engenharia. O único rastro técnico público é o perfil dos funcionários e a própria análise da DF.
- **[inferência]** O contraste com o PC (2024) e o Switch 2 (2025) é a melhor evidência de **quais decisões eram orçamento e quais eram limite**: no PC a Double Eleven adicionou DRS/DLSS/FSR/sombras melhores; no Switch 2, DLSS/60/16xAF/CHSS. Nada disso era impossível no RAGE — era caro demais para o escopo de 2023.

### Gaps
- **Nenhum** GDC/CEDEC/talk técnico, entrevista longa ou artigo de engenharia da Double Eleven/Rockstar sobre o port de Switch.
- Não há números oficiais de performance além de "30 FPS, até 1080p".
- Não há informação de credits detalhada (quantas pessoas, duração, custo).
- Não há análise da DF sobre o **input latency** do port de Switch.
- Não há dados de certificação Nintendo nem de uso de ferramentas first-party (NVN, NX SDK).

---

## 5. Como isso difere de uma recompilação estática (ReXGlue) e o que é transferível

### Takeaway
A diferença central é **acesso ao código-fonte e ao pipeline de assets**. A Double Eleven recompilou o RAGE para ARM64/NVN e pôde escolher AA, sombras, LOD e o que cortar; um port por recompilação estática (PPC→C++→ARM64, Xenos→Vulkan) não tem essa alavanca: o código de jogo transpilado mantém as decisões de renderer do original, e o ganho vem de substituir a GPU emulada por um renderer nativo e de usar o hardware folgado. Ainda assim, o **pacote de decisões** do port oficial é quase todo replicável num recomp: resolução fixa, 30 FPS, assets originais, LOD original, AA barato e cortar modos não usados.

### Cited Findings
- O port oficial é descrito como **"new conversion"** feito "especificamente" para PS4 e Switch, com 30 FPS e até 1080p no Switch — [Take-Two/Rockstar](https://ir.take2games.com/node/29806/pdf)
- A versão de Switch 1 usou o **Xbox 360 como base de settings visuais**, trocando 2xMSAA por FXAA e mantendo texturas/LOD de 2010 — [Digital Foundry](https://www.digitalfoundry.net/articles/digitalfoundry-2023-red-dead-redemption-on-switch-an-impressive-creditable-port); [Digital Foundry, Switch 2](https://www.digitalfoundry.net/news/2025/12/red-dead-redemption-on-switch-2-a-great-dlss60fps-combo)
- O executável do Switch é **ARM64 nativo** (patch de instruções) e chama o **SDK da Nintendo** para detectar docked/handheld — [GameBanana](https://gamebanana.com/mods/464053); [GBAtemp/ReverseNX](https://gbatemp.net/threads/reversenx-flag-your-console-as-handheld-or-docked.543625/page-49)
- A Double Eleven teve liberdade para **mexer no áudio de baixo nível e nos "new-gen SDKs"** no RDR — [LinkedIn](https://linkedin.com/in/felypefernandes)
- O port de PC (2024) feito pelo mesmo estúdio adicionou **DRS, DLSS/FSR, frame gen, draw distance maior, sombras com contact hardening e DX12** — mudanças que não existem no Switch de 2023 — [Digital Foundry, PC](https://www.digitalfoundry.net/articles/digitalfoundry-2024-red-dead-redemption-pc-tech-review)
- O Switch 2 (2025) é um **app nativo separado**, com DLSS, 60 FPS, 16x AF, LOD maior e CHSS — [Digital Foundry](https://www.digitalfoundry.net/news/2025/12/red-dead-redemption-on-switch-2-a-great-dlss60fps-combo)
- Contexto do projeto do usuário: o GH3 usa **ReXGlue (PPC→C++→ARM64)** e um **renderer Vulkan nativo (project8-sw/NVK)**, escolhido em runtime por `--gh3_renderer=native`; o caminho emulado (Xenos) fica em ~5 FPS — [AGENTS.md do repositório](/Users/guillermemanhaes/gh3-nx/AGENTS.md)

### Inferences
**Quadro comparativo [inferência, com base nos itens citados acima]:**

| Dimensão | Port oficial (RDR1 Switch) | Recompilação estática (GH3/ReXGlue) |
|---|---|---|
| Código de jogo | C++ original do RAGE, recompilado para ARM64 | PPC do `default.xex` transpilado para C++/ARM64 |
| Renderer | Backend NVN escrito/portado pela Double Eleven; pode mudar AA, sombras, LOD | Renderer do jogo é código de guest; o Vulkan nativo substitui o **Xenos**, não o renderer do jogo |
| Assets | Pipeline RAGE original; pode reempacotar, recomprimir, trocar formato | Assets do disco do usuário, formato original; reempacotar é possível, mas sem tools oficiais |
| LOD/draw distance | Ajustável no código; foi mantido igual ao 360 | Fixo no código de guest; mudar exige patch/RE |
| Threading/jobs | Pode reescrever para 3× A57 | Mantém o escalonamento do Xenon (3 cores/6 threads); ganho vem do hardware OoO |
| AA | Escolha livre (FXAA, sem MSAA) | Depende do que o renderer nativo expõe; MSAA do original pode ser emulado ou substituído |
| Resolução | 1080p/720p fixos | Pode ser maior que o original (upscale interno/FSR), mas HUD/UI podem ficar presos na resolução antiga |
| Escopo | Cortou multiplayer, adicionou idiomas/achievements | Corte de modos é possível, mas é trabalho de RE; UI nova é cara |
| Verificação | QA + certificação Nintendo | Oráculo próprio (boot test, captura/paridade, FPS) |

**O que é transferível para o GH3 [inferência]:**
1. **Orçamento de banda primeiro**: escolher AA barato (FXAA ou nenhum) e não gastar em MSAA; cortar AF alto; reduzir resolução de sombra antes de cortar framerate. O port oficial fez exatamente isso.
2. **Metas fixas e simples**: 1080p docked / 720p handheld, 30 FPS travado, sem DRS. Para um jogo de 2007, 1080p/60 pode ser viável — mas o padrão de "resolução fixa + frametime estável" é o que a DF elogiou.
3. **Manter assets e LOD originais**: o port oficial não tentou remasterizar; vendeu resolução e portabilidade. Um recomp não deve gastar esforço em "melhorar" LOD se o original já é aceitável.
4. **Cortar o que não é usado**: multiplayer fora do escopo remove rede/serviços e uma classe inteira de bugs. No GH3, modos/online equivalentes podem ser avaliados.
5. **HUD/UI na resolução da tela**: o erro do port oficial (HUD 720p) é uma lição negativa; num recomp, a UI é um dos poucos lugares onde dá para intervir sem tocar no gameplay.
6. **Diagnóstico por headroom**: o teste do MVG (remover cap + overclock) é um método barato para descobrir se o limite é CPU, GPU ou banda — replicável no Switch real para calibrar o renderer nativo.
7. **Áudio e localização**: o port oficial retargetou o motor de áudio para os SDKs novos; num recomp, áudio costuma ser um ponto subestimado (mixer, DSP, latência).
8. **Validação estilo DF**: contar pixels, medir frametime, comparar cenas específicas (cidade à noite, muitos NPCs) — é o que separa "roda" de "port".

**O que NÃO é transferível [inferência]:**
- Reescrita de backend gráfico no nível do jogo (NVN), recompilação de shaders, mudança de formato de assets, ajuste de LOD/streaming no código, adição de achievements/localização no engine, certificação oficial. Tudo isso exige fonte.
- A "qualidade" do port oficial não veio de truque de renderer: veio de **ter o código** e poder fazer um port conservador. Num recomp, o teto é o que o renderer nativo consegue em cima do código de guest.

### Gaps
- Não há estudo público comparando **frametime/latência** de um recomp com o port oficial do mesmo jogo (não existe RDR1 em ReXGlue).
- Não há dados sobre **quanto do orçamento de 30 FPS** do RDR1 Switch é CPU vs. GPU; sem isso, a transferibilidade dos itens 1–2 para o GH3 é qualitativa.
- Não se sabe se o GH3 (2007, 720p/60 no 360) tem folga de banda suficiente para 1080p/60 no Switch; isso precisa ser medido no projeto, não inferido do RDR1.
- A afirmação "o port oficial não usou DRS" é consenso DF/MVG, mas não há confirmação da Rockstar; se existir DRS oculto, a conclusão muda.
