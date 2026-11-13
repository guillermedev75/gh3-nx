# No Man's Sky no Nintendo Switch (Hello Games, 2022) — anatomia técnica do port oficial

**Escopo:** somente o port de Switch 1 (Tegra X1). Switch 2 e PC/PS/Xbox aparecem apenas como contraste.
**Data da pesquisa:** 2026-10-20.

**Legenda de confiança**
- **[medido]** — número ou observação de análise técnica com captura (Digital Foundry, IGN).
- **[citado]** — declaração de funcionário da Hello Games, nota de patch oficial ou imprensa.
- **[inferência]** — dedução minha a partir dos dados; não é afirmação da Hello Games.
- **[conflito]** — fontes discordam; os dois valores ficam registrados.

**Resumo em uma frase:** a Hello Games não reescreveu o jogo para o Switch; portou o mesmo engine e o mesmo universo procedural, cortou agressivamente custo de banda (resolução, alpha, sombras, LOD, texturas, animação) e, um ano depois, recuperou boa parte da qualidade de imagem integrando **FSR 2 customizado + DRS dentro do próprio pipeline** — algo que só é possível com acesso ao código-fonte do renderer.

---

## 1. O que a Hello Games teve de fazer no renderer?

### Takeaway
Não houve reescrita: o renderer multiplataforma foi adaptado, com enormes reduções de custo por pixel e, depois, um upscaler temporal proprietário embutido no pipeline. O ganho grande veio de **substituir os filtros pesados do FSR 2 por versões leves** (o gargalo era banda, não ALU) e **fundir passes do FSR 2 com passes do próprio engine**.

### Achados citados

**Estado no lançamento (out/2022)**
- Docked: **1152×648**; handheld: **896×504**; alvo de **30 fps**. TAA presente, mas com cobertura ruim; imagem "cintila o tempo todo", com serrilhado em bordas geométricas. — [Digital Foundry, 18/10/2022](https://www.digitalfoundry.net/articles/digitalfoundry-2022-no-mans-sky-for-nintendo-switch-a-valiant-effort-but-the-compromises-cut-too-deep), resumido por [Nintendo Everything](https://nintendoeverything.com/no-mans-sky-switch-tech-analysis-including-frame-rate-and-resolution)
- IGN mediu **~1120×630 docked** e **896×504 handheld**, descrevendo "sem AA, mas com um passe de sharpening" e DRS possivelmente presente, porém aparentando resolução fixa. Diz que o docked tem **56% mais resolução** que o handheld (handheld ~36% menor). — [IGN, 21/10/2022](https://www.ign.com/articles/no-mans-sky-switch-performance-review) **[conflito]** com a medição de 1152×648 da DF; e **[conflito]** com "TAA usado" da DF.
- DF: "draw-in agressivo em praticamente todos os elementos de tela"; blocos grandes de geometria aparecem a curta distância; sombras entram "de forma crua" conforme o jogador se aproxima; folhagem grande vai longe, mas arbustos só aparecem muito perto; assets de baixa qualidade, texturas "sopudas"; nuvens volumétricas simples e de baixa resolução; animação de personagem em taxa reduzida perto do jogador; shadow maps de média distância cintilam. — [Nintendo Everything / DF](https://nintendoeverything.com/no-mans-sky-switch-tech-analysis-including-frame-rate-and-resolution)
- Base building derruba o framerate e "essencialmente trava" em bases complexas. — [Nintendo Everything / DF](https://nintendoeverything.com/no-mans-sky-switch-tech-analysis-including-frame-rate-and-resolution)

**Update 4.40 / "Echoes" (ago/2023): o salto**
- Nota oficial: "Uma versão altamente customizada do AMD FidelityFX Super Resolution 2 (FSR 2), feita sob medida para No Man's Sky no Switch", combinada com **Dynamic Resolution Scaling**, para mais qualidade de imagem e framerate mais suave. — [Hello Games, Echoes 4.4](https://www.nomanssky.com/echoes-update/)
- A Hello Games respondeu à DF com o detalhamento técnico (tradução):
  - "Ao rodar FSR 2 (na implementação de referência) num sistema de especificação mais baixa como o Switch, **banda é de longe o principal gargalo**. Isso se deve principalmente ao número considerável de amostras feitas no passe final de upsampling temporal."
  - "Por isso, a maior parte do esforço foi **substituir alguns dos filtros de amostragem pesados do FSR 2 por filtros customizados mais leves**, mas ainda capazes de alta qualidade. **Essa é de longe a mudança mais importante que fizemos em performance.**"
  - "Além disso, o FSR 2 **não está implementado no nosso engine como biblioteca externa**, e sim integrado ao pipeline. Isso permitiu **fundir alguns passes do FSR 2 com os nossos** e amortizar o custo."
  - "As últimas semanas foram gastas simplificando cálculos para **reduzir custo de ALU e aumentar ocupação** (a essa altura banda já não era o gargalo principal). Muito tempo também foi gasto ajustando heurísticas para equilibrar nitidez, estabilidade e ghosting. Com o update 4.40, **DRS também passou a ser habilitado no Switch**."
  — [Digital Foundry, 29/09/2023](https://www.digitalfoundry.net/articles/digitalfoundry-2023-fixed-on-switch-no-mans-skys-custom-fsr-2-support-dramatically-improves-the-game)
- Resultado medido pela DF depois do patch: docked com **DRS ~468p–648p** e saída FSR 2 em torno de **648p**; handheld **~504p dinâmico**, saída próxima disso. Imagem final "muito limpa, ainda que macia"; o jogo vira "um dos mais estáveis visualmente do Switch". — [Digital Foundry, 29/09/2023](https://www.digitalfoundry.net/articles/digitalfoundry-2023-fixed-on-switch-no-mans-skys-custom-fsr-2-support-dramatically-improves-the-game)
- A DF observa que a Hello **não mira uma saída alta**, provavelmente por causa do custo de frame-time do FSR 2; mesmo assim, o resultado é limpo. — mesma fonte.
- A DF chama o caso de **primeiro exemplo de FSR 2 no Switch** que ela viu. — mesma fonte. **[conflito leve]** com a TweakTown, que diz que *Switch Sports* também usou FSR; a afirmação da DF é sobre o que ela própria testemunhou, não um censo.

**O que o patch 4.4 lista como otimização de renderer/memória no Switch**
- "Implementação bespoke de FSR 2.0" e DRS para Switch.
- "Número significativo de otimizações de uso de memória para Nintendo Switch."
- "Otimização significativa de renderização de céu no Switch" e melhora de qualidade do céu em movimento.
- Otimizações específicas para naves IA, hangares de freighter e sistemas de mira de torres.
- Correção de crashes de gerenciamento de memória.
— [Hello Games, Echoes 4.4 patch notes](https://www.nomanssky.com/echoes-update/)

### Inferences
- **[inferência]** O port não é um renderer novo: é o mesmo pipeline com backend/ajustes de Switch, já que a Hello fala em "fundir passes do FSR 2 com os nossos" e em "heurísticas" do próprio engine.
- **[inferência]** O gargalo dominante no Switch é **banda de memória e fill-rate** (amostragem do upscaler, alpha, sombras, volumétricos), não geometria bruta. A ordem das otimizações descritas (banda → ALU/ocupação) confirma isso.
- **[inferência]** O FSR 2 no Switch só foi viável porque o engine já tinha **motion vectors e depth** utilizáveis (havia TAA antes). Sem esses buffers, o upscaler temporal não funciona.
- **[inferência]** A resolução de saída ficar em ~648p/504p, abaixo de 720p, indica que a Hello preferiu **estabilidade temporal e custo previsível** a picos de nitidez.

### Gaps
- Não há informação pública sobre **shaders específicos** reescritos, nem sobre a API gráfica do Switch (NVN proprietária vs. Vulkan). O engine de PC usa Vulkan/OpenGL; no Switch, o caminho mais provável é NVN, mas isso **não foi confirmado**.
- Não há detalhe público de como o DRS escolhe o alvo (histograma de GPU? frame-time? histerese?), nem os limites exatos de escala.
- Não há informação sobre uso de compute shaders no FSR 2 customizado além da menção a "ALU/ocupação".

---

## 2. CPU, memória, engine e streaming: o que foi feito?

### Takeaway
A grande vantagem estrutural é o engine procedural: o jogo **gera** o mundo em vez de carregar assets gigantes, então "streaming" é, sobretudo, geração contínua. Ainda assim, memória foi o limite real: a Hello reduziu animação, áudio, densidade, texturas e efeitos, e admite que cada update custa 2–3× mais trabalho nas plataformas móveis por causa de "restrições de memória impossíveis".

### Achados citados

**Arquitetura do engine (base para o port)**
- O engine da Hello gera mundos continuamente: **geração voxel → poligonização → texturização → povoamento e simulação**, permitindo transição seamless de espaço para superfície. — [GDC 2017, "Continuous World Generation in No Man's Sky", Innes McKendrick](https://www.gdcvault.com/play/1024265); resumo em [Game Developer](https://www.gamedeveloper.com/programming/video-how-continuous-world-generation-works-in-i-no-man-s-sky-i-)
- O jogo é "construído em torno de geração procedural, o que significa que o console gera tudo o que você vê", o que torna o port "muito mais difícil" — [Sean Murray, comunicado de anúncio, via Kotaku](https://kotaku.com/no-mans-sky-nintendo-switch-hello-games-sean-murray-ps-1849096661)
- A Hello esperava cortar código do engine, "particularmente relacionado a geração procedural", mas **"no fim, fizeram funcionar sem mudanças"**. — [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review)
- O universo é o mesmo entre plataformas: "você pode achar uma árvore no PS5 e ir lá no Switch e achar a mesma árvore". — [Sean Murray, The Verge](https://www.theverge.com/2022/10/3/23380392/no-mans-sky-switch-launch-interview-sean-murray-4-0-update) **[citado]**

**Memória**
- "As plataformas móveis, como Switch 1 e 2, junto com Steam Deck, tomam uma quantidade desproporcional de tempo de engenharia a cada update. Vários de nós na Hello Games provavelmente gastam **2–3× mais tempo** para fazer esses updates funcionarem perfeitamente, exatamente como nos outros consoles e PC/Mac." — **Martin Griffiths, engenheiro de engine da Hello Games**, no X, abril/2026 — [GamesRadar](https://www.gamesradar.com/games/survival/no-mans-sky-updates-like-xeno-arena-take-2-3x-longer-on-switch-2-and-steam-deck-because-hello-games-has-to-beat-impossible-memory-constraints-engineer-says/); [GoNintendo](https://gonintendo.com/contents/59844-no-mans-sky-dev-says-switch-switch-2-steam-deck-updates-take-triple-the-dev-time)
- "É um prazer constante vencer **restrições de memória impossíveis** com um jogo que cresce." — mesma fonte.
- Nota 4.4: "número significativo de otimizações de uso de memória para Nintendo Switch". — [Hello Games](https://www.nomanssky.com/echoes-update/)

**CPU / animação / áudio**
- Reduções de CPU fazem **animação rodar em taxa pela metade** a menos que o personagem esteja a poucos metros. — [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review) **[medido/observado]**
- Áudio: qualidade "boa", mas aparentemente com **menos amostras, menos processamento e menor variedade** de sons de objeto. — [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review)
- IGN levanta a hipótese de o engine ser **double-buffered** e atribui engasgos de 16/33/50 ms a pressão de memória; isso é hipótese do analista, não confirmação da Hello. — [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review) **[inferência da fonte, não da Hello]**

**Assets / disco**
- Tamanho atual no Switch: **6,9 GB**; no Switch 2 Edition: **30,2 GB** (com texturas de alta resolução). — [Nintendo](https://www.nintendo.com/au/games/nintendo-switch/no-mans-sky/)
- O Switch 2 Edition adiciona **DLSS, DRS, texturas de alta resolução**, roda a **1080p/40 fps com VRR no handheld** e **1440p/30 fps no docked**, e o arquivo passa de "pouco mais de 6 GB" para "mais de 25 GB". — [Vooks](https://www.vooks.net/no-mans-sky-gets-free-nintendo-switch-2-edition-update); [Nintendo Insider](https://www.nintendo-insider.com/no-mans-sky-nintendo-switch-2-edition-review)
- No PC, o install girava em torno de 14 GB em 2023 — [Steam Community](https://steamcommunity.com/app/275850/discussions/0/3815166994154663103?l=brazilian). Serve só para mostrar que 6,9 GB no Switch é um pacote ainda mais enxuto. **[contexto]**

**Carga**
- Tempo de loading inicial no Switch: **~1min10s**, contra **~1min29s** no PS4 (~22% mais rápido). — [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review) **[medido]**

### Inferences
- **[inferência]** O "streaming" do NMS no Switch é principalmente **geração procedural em runtime + pooling**, não streaming de um mundo pré-cozido. Por isso o custo de I/O é baixo e o de CPU/memória é alto.
- **[inferência]** O corte de 6,9 GB → 30,2 GB no Switch 2 mostra que a maior parte da diferença é **payload de textura/resolução**, não geometria: o Switch 1 usa mips/texturas muito mais baratas e provavelmente compressão de textura adequada ao hardware (ASTC/ETC2), mas **não há confirmação pública do formato**.
- **[inferência]** A redução de animação para metade da taxa a distância é um corte de **CPU**, típico de engine com orçamento de skinning/atualização; é exatamente o tipo de corte que um port de recompilação estática não consegue fazer sem reescrever lógica do jogo.
- **[inferência]** "Sem mudanças na geração procedural" + "mesmo universo" indica que o custo do port não estava no algoritmo, e sim no **renderer, memória e orçamento de assets**.

### Gaps
- Não há informação pública sobre **número de threads/jobs**, uso de fibers, task graph, ou mudanças de escalonamento no Switch.
- Não há detalhe público sobre **formato de compressão de textura** no Switch 1.
- Não há detalhe público sobre **streaming de áudio** ou codec usado no Switch.
- Não há confirmação de que o port tenha usado NVN; a API gráfica permanece não documentada publicamente.

---

## 3. Quais compromissos de qualidade foram aceitos?

### Takeaway
Cortes em **densidade de mundo, LOD, alpha, sombras, iluminação, post-process, texturas, animação e áudio**, além de duas features inteiras (multiplayer e settlements). O que sobrou: o universo é o mesmo, o ciclo dia/noite, tempestades, nuvens volumétricas, fauna e flora procedurais continuam existindo — só que mais simples e mais perto.

### Achados citados

| Dimensão | O que foi cortado/limitado | Fonte |
|---|---|---|
| Densidade do mundo | Densidade reduzida vs. PS4 | [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review) |
| Geometria/folhagem | Menos detalhe em grama, flora, fauna; polycount menor; "billboards" usados muito perto da câmera; grama alpha-tested com alcance curto | [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review) |
| Texturas | Menor variedade e menor resolução; assets "de baixa qualidade", texturas "sopudas" | [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review); [DF](https://www.digitalfoundry.net/articles/digitalfoundry-2022-no-mans-sky-for-nintendo-switch-a-valiant-effort-but-the-compromises-cut-too-deep) |
| Alpha/partículas | Menor resolução e quantidade de efeitos alpha (grama, árvores) e partículas; volumétricos e alpha em metade da resolução do buffer ou menos | [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review) |
| LOD | LOD reduzido em todos os testes de profundidade; impostores usados mais perto; pop-in visível; geometria grande "entra" a curta distância | [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review); [DF via NE](https://nintendoeverything.com/no-mans-sky-switch-tech-analysis-including-frame-rate-and-resolution) |
| Sombras | Shadow maps de média distância cintilam; cobertura de sombra entra "de forma crua" ao se aproximar | [DF via NE](https://nintendoeverything.com/no-mans-sky-switch-tech-analysis-including-frame-rate-and-resolution) |
| Iluminação/post | Menos efeitos de luz e post vs. PS4; **motion blur por objeto removido**; **depth of field bokeh baseado em sprite** (mais barato); menos camadas de material e decals | [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review) |
| Reflexos | **Screen-space reflections removidos** | [Nintendo World Report](http://www.nintendoworldreport.com/review/61812/no-mans-sky-switch-review) |
| Draw distance | Alcance de folhagem reduzido; terreno ainda desenhado "bastante longe" | [Nintendo World Report](http://www.nintendoworldreport.com/review/61812/no-mans-sky-switch-review) |
| Nuvens/volumétricos | Nuvens volumétricas mantidas, porém simples e de baixa resolução | [DF via NE](https://nintendoeverything.com/no-mans-sky-switch-tech-analysis-including-frame-rate-and-resolution) |
| Animação | Taxa reduzida/meia taxa fora de curta distância | [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review) |
| Áudio | Menos amostras, menos processamento, menos variedade | [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review) |
| Base building | Limite de complexidade por padrão no Switch, desativável com aviso | [Nintendo World Report](http://www.nintendoworldreport.com/review/61812/no-mans-sky-switch-review) |
| Features | **Sem multiplayer**; **sem settlements** (update Frontiers) | [The Verge](https://www.theverge.com/2022/10/3/23380392/no-mans-sky-switch-launch-interview-sean-murray-4-0-update); [GoNintendo/Eurogamer](https://gonintendo.com/contents/10265-hello-games-on-no-mans-sky-s-impossible-switch-port-lack-of-multiplayer-and) |
| Framerate | Alvo 30 fps; quedas frequentes; ferramenta de mineração chega "logo acima dos teens"; base building derruba | [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review); [DF via NE](https://nintendoeverything.com/no-mans-sky-switch-tech-analysis-including-frame-rate-and-resolution) |

- Justificativa de Murray para os cortes: "O foco foi jogabilidade e performance, e garantir que entregássemos algo o mais próximo humanamente possível dos irmãos de console." — [The Verge](https://www.theverge.com/2022/10/3/23380392/no-mans-sky-switch-launch-interview-sean-murray-4-0-update)
- Murray não acreditava no port: "Quando algumas pessoas do time disseram 'acho que dá para fazer uma versão de Switch', eu honestamente disse que não acreditava." — [GamingBible](https://www.gamingbible.com/features/no-mans-sky-interview-the-impossible-made-possible-20221003)
- Ele esperava não conseguir suportar o conjunto completo de features nem "as seis séries de updates" e não achava que seria o mesmo universo. — mesma fonte.
- O que permaneceu: nuvens volumétricas na atmosfera, tempestades, ciclo dia/noite completo, animais procedurais animados. — [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review)

### Inferences
- **[inferência]** O port foi tratado como **"o mesmo jogo, com orçamento menor por pixel"**, não como uma versão reduzida de conteúdo: o que caiu foram features que exigem estado persistente/rede (multiplayer) e simulação pesada (settlements), além de efeitos caros.
- **[inferência]** A ordem dos cortes (alpha, sombras, volumétricos, motion blur, SSR, DOF) é consistente com um gargalo de **bandwidth/fill-rate** no Tegra X1, não de vértices.
- **[inferência]** O limite de base building é um **teto de memória/estado**, não de GPU: bases crescem sem limite e precisam ser mantidas/simuladas.

### Gaps
- Não há números públicos de **orçamento de memória** (MB de texturas, render targets, etc.).
- Não há confirmação de resolução interna **mínima** do DRS além da faixa medida pela DF (~468p docked).
- Não há informação sobre o impacto exato de cada corte em frame-time; a Hello não publicou profiling.

---

## 4. Houve entrevista, talk de GDC, blog ou análise técnica do processo?

### Takeaway
**Não existe (até esta pesquisa) uma talk de GDC específica sobre o port de Switch.** O material técnico real são: (a) duas análises da Digital Foundry (lançamento e pós-4.40); (b) uma resposta técnica escrita da Hello Games à DF sobre o FSR 2; (c) a talk de GDC 2017 sobre geração procedural contínua (arquitetura, não port); (d) entrevistas de Sean Murray sobre produto/decisões; (e) um post de 2026 do engenheiro Martin Griffiths sobre custo de updates. Nada de blog/postmortem oficial detalhado.

### Achados citados
- **Digital Foundry, 18/10/2022** — "No Man's Sky for Nintendo Switch: a valiant effort – but the compromises cut too deep": análise de resolução, LOD, sombras, TAA, framerate e base building. — [link](https://www.digitalfoundry.net/articles/digitalfoundry-2022-no-mans-sky-for-nintendo-switch-a-valiant-effort-but-the-compromises-cut-too-deep)
- **Digital Foundry, 29/09/2023** — "Fixed on Switch: No Man's Sky's custom FSR 2 support dramatically improves the game": inclui a resposta técnica da Hello Games sobre banda, filtros customizados, integração no pipeline, ALU/ocupação, heurísticas e DRS. — [link](https://www.digitalfoundry.net/articles/digitalfoundry-2023-fixed-on-switch-no-mans-s-skys-custom-fsr-2-support-dramatically-improves-the-game)
- **GDC 2017 — "Continuous World Generation in 'No Man's Sky'" (Innes McKendrick, Hello Games)**: pipeline voxel → poligonização → texturização → povoamento/simulação, com geração contínua em tempo real. — [GDC Vault](https://www.gdcvault.com/play/1024265); [Game Developer](https://www.gamedeveloper.com/programming/video-how-continuous-world-generation-works-in-i-no-man-s-sky-i-)
- **GDC 2018 — "Beyond Procedural Horizons" (Innes McKendrick)**: continuação da talk de arquitetura procedural; não é sobre Switch. — [referência em r/NoMansSkyTheGame](https://www.reddit.com/r/NoMansSkyTheGame/comments/8udw69/beyond_procedural_horizons_was_a_gdc_2018_talk_by)
- **The Verge, 03/10/2022** — entrevista com Sean Murray: mesmo universo, sem multiplayer/settlements, foco em performance. — [link](https://www.theverge.com/2022/10/3/23380392/no-mans-sky-switch-launch-interview-sean-murray-4-0-update)
- **Eurogamer via GoNintendo, 03/10/2022** — Murray sobre ceticismo inicial, multiplayer e modelo de negócio. — [link](https://gonintendo.com/contents/10265-hello-games-on-no-mans-sky-s-impossible-switch-port-lack-of-multiplayer-and)
- **GamingBible, 19/12/2022** — "the impossible made possible": Murray sobre não acreditar no port e expectativa de cortes. — [link](https://www.gamingbible.com/features/no-mans-sky-interview-the-impossible-made-possible-20221003)
- **Martin Griffiths (engine programmer), X, abril/2026** — custo de 2–3× por update em Switch 1/2 e Steam Deck por restrições de memória. — [GamesRadar](https://www.gamesradar.com/games/survival/no-mans-sky-updates-like-xeno-arena-take-2-3x-longer-on-switch-2-and-steam-deck-because-hello-games-has-to-beat-impossible-memory-constraints-engineer-says/)
- **Nota oficial do update 4.4 (Echoes)** — lista FSR 2 bespoke, DRS, otimizações de memória e de céu no Switch. — [nomanssky.com](https://www.nomanssky.com/echoes-update/)

### Inferences
- **[inferência]** A Hello Games preferiu **não fazer talk pública** do port; o detalhamento técnico veio por resposta direta à DF. Isso é comum quando o port é tratado como parte do ciclo de updates, não como projeto separado.
- **[inferência]** As entrevistas de Murray são sobre **produto e escopo**, não sobre implementação; qualquer relato de "como foi feito" a partir delas é limitado.

### Gaps
- **Não foi encontrada** talk de GDC/Develop/EGX especificamente sobre o port de Switch.
- **Não foi encontrado** blog/postmortem oficial da Hello Games sobre o port.
- **Não foi encontrado** material público de profiling (frame-time, bandwidth, occupancy) além do que a DF mediu e do que a Hello resumiu.

---

## 5. Como isso difere de um port de recompressão estática (sem código-fonte)?

### Takeaway
A diferença central não é "qualidade do port", é **onde se pode mexer**. A Hello tinha o código do engine e do renderer: pôde reescrever filtros do FSR 2, fundir passes, mudar heurísticas, reconstruir assets e cortar features no código de gameplay. Num port de recompilação estática (ReXGlue: PPC → C++ → ARM64), o código de convidado é fixo; o que se controla é o **renderer nativo substituto** e alguns pontos de interceptação. As lições do NMS que sobrevivem são as de **orçamento de banda e apresentação**, não as de reescrita de engine.

### Achados citados (base para a comparação)
- A Hello descreve o FSR 2 como **integrado ao pipeline**, com passes fundidos e filtros substituídos — ou seja, mudanças **dentro do código do renderer**. — [DF, 2023](https://www.digitalfoundry.net/articles/digitalfoundry-2023-fixed-on-switch-no-mans-skys-custom-fsr-2-support-dramatically-improves-the-game)
- A Hello rodou o **mesmo universo** e a **mesma geração procedural** no Switch, sem mudanças no algoritmo. — [The Verge](https://www.theverge.com/2022/10/3/23380392/no-mans-sky-switch-launch-interview-sean-murray-4-0-update); [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review)
- O port cortou **animação em distância, áudio, densidade, LOD, alpha, sombras, post** — decisões que exigem acesso ao código de simulação/renderização e ao build de assets. — [IGN](https://www.ign.com/articles/no-mans-sky-switch-performance-review)
- O Switch 2 Edition, com o mesmo engine, sobe para **1080p/40 handheld e 1440p/30 docked com DLSS** e texturas de alta resolução, mas o arquivo vai a **30 GB**. — [Vooks](https://www.vooks.net/no-mans-sky-gets-free-nintendo-switch-2-edition-update); [Nintendo](https://www.nintendo.com/au/games/nintendo-switch/no-mans-sky/)
- O custo de manter o port é **2–3× maior por update** nas plataformas móveis, por memória. — [GamesRadar / Martin Griffiths](https://www.gamesradar.com/games/survival/no-mans-sky-updates-like-xeno-arena-take-2-3x-longer-on-switch-2-and-steam-deck-because-hello-games-has-to-beat-impossible-memory-constraints-engineer-says/)

### Inferences — o que é transferível para o gh3-nx

**Transferível (técnica, não código)**
1. **Upscaling temporal só vale se o custo couber no orçamento de banda.** O maior ganho da Hello não foi "usar FSR 2", foi **enxugar os filtros de amostragem** e integrar o upscaler ao pipeline. Num renderer nativo Vulkan/NVK, isso significa: medir o custo do passe de upscale, reduzir taps/amostras, e não tratar o upscaler como caixa-preta.
2. **DRS com histerese e alvo por frame-time** é mais valioso que resolução fixa em cenas variáveis. O NMS só habilitou DRS no 4.40; antes, a resolução fixa deixava picos de custo virarem quedas. No GH3, o análogo é escala de resolução por cena/música/efeitos.
3. **Atacar primeiro alpha, sombras, volumétricos e pós-processamento.** São os maiores consumidores de banda/fill-rate no Tegra X1. No GH3, isso mapeia para partículas de palco, transparências, reflexos e o bloom/DOF do renderer nativo.
4. **LOD/impostores agressivos e "draw-in" aceitável** compram muito frame-time. O NMS aceitou pop-in visível para manter 30 fps; no GH3, a meta de 60 fps provavelmente exige o mesmo tipo de corte, mas o orçamento original de 360 é muito menor.
5. **Textura é o maior item de memória.** A diferença 6,9 GB → 30 GB no Switch 2 mostra que resolução de textura domina o pacote. Para o GH3, reduzir mips/resolução de textura e usar o formato de compressão correto do Switch é provavelmente o maior ganho de memória.
6. **Cortar features que exigem estado/rede** (multiplayer, settlements) foi decisivo no NMS. No GH3, o análogo é desligar/limitar modos ou subsystems que não sobrevivem ao port (online, DLC, etc.), em vez de tentar emular tudo.

**Não transferível diretamente (ou muito caro)**
1. **FSR 2 "de verdade"** exige **motion vectors + depth + jitter** consistentes. O NMS já tinha TAA e, portanto, motion vectors. Um jogo de 360 como GH3 normalmente **não tem motion vectors**; sem sintetizá-los (caro e propenso a erro), FSR 2 não é plug-and-play. Alternativas realistas: **FSR 1 / spatial upscale + sharpening**, ou um temporal próprio com reprojeção baseada em depth.
2. **Fundir passes do upscaler com passes do engine** exige acesso ao grafo de renderização do jogo original. No ReXGlue, o grafo do convidado é traduzido; você só funde o que o **seu renderer nativo** controla.
3. **Meia taxa de animação por distância** exige hooks no código de simulação/animação do convidado. É possível por patch/reverse engineering, mas é frágil e específico por jogo.
4. **Mudar o build de assets** (resolução de textura, compressão, áudio) exige pipeline de assets; num port sem fonte, isso depende de ferramentas próprias de repack.
5. **"Sem mudanças na geração procedural"** não tem análogo: o GH3 não gera mundo; ele carrega assets fixos. O gargalo do GH3 é provavelmente **draw calls/estado de renderer e banda**, não geração.

**Mapa mental para o gh3-nx**
- O que a Hello teve de melhor foi **propriedade do renderer**. O gh3-nx já tem isso no renderer nativo Vulkan/NVK — é exatamente o pedaço onde as lições 1–5 se aplicam.
- O que o gh3-nx **não** tem é propriedade do jogo (lógica, animação, assets). É aí que as lições do NMS param.
- O caso NMS também mostra que **um ano de updates** foi necessário para chegar ao FSR 2 + DRS. Um port de recompilação estática deve planejar o mesmo ciclo: primeiro estabilidade e cortes baratos, depois upscaler.

### Gaps
- Não sei, sem inspecionar o renderer do gh3-nx, se o jogo expõe **depth buffer e vetores de movimento** utilizáveis; isso decide se FSR 2 é viável ou se o caminho é FSR 1/spatial.
- Não há benchmark público de **bandwidth do GH3 360 vs. NMS** que permita prever o orçamento; a comparação acima é qualitativa.
- Não há confirmação de que o NMS no Switch use NVN; a comparação de stack gráfica com NVK é, portanto, **inferência**.

---

## Apêndice — linha do tempo e números-chave

| Data | Evento | Número/fato |
|---|---|---|
| 09/02/2022 | Anúncio do port no Nintendo Direct | "mesmo universo", updates até Prisms |
| 07/10/2022 | Lançamento (update 4.0 "Waypoint") | 1152×648 docked / 896×504 handheld; 30 fps; sem multiplayer/settlements |
| 18/10/2022 | Análise DF de lançamento | draw-in agressivo, TAA fraco, quedas frequentes |
| 21/10/2022 | Análise IGN | ~1120×630 / 896×504; loading 70 s vs. 89 s no PS4 |
| 24/08/2023 | Update 4.4 "Echoes" | FSR 2 bespoke + DRS no Switch; otimizações de memória e céu |
| 29/09/2023 | Reanálise DF | DRS 468–648p docked, ~504p handheld; imagem muito mais estável |
| 05/06/2025 | Switch 2 Edition | 1080p/40 handheld VRR, 1440p/30 docked, DLSS, 30 GB |
| 14/04/2026 | Post de Martin Griffiths | updates em plataformas móveis custam 2–3× mais tempo |

**Números para citar com cuidado**
- 1152×648 e 896×504: **medição DF** (lançamento). IGN mediu 1120×630; usar a faixa e dizer que há divergência.
- 468–648p e ~504p: **medição DF** pós-4.40.
- 6,9 GB e 30,2 GB: **ficha oficial da Nintendo**.
- 2–3×: **declaração** de engenheiro da Hello, não medição independente.
- "Primeiro FSR 2 no Switch": **afirmação da DF** no escopo do que ela testou; não é um censo.
