---
created: 2026-10-20
updated: 2026-10-20
tags:
  - decompilacao
  - recompilacao-estatica
  - xbox-360
  - nintendo-switch
  - guitar-hero-3
---

# Decompilação, reimplementação e reescrita: o que se aplica a um port de recompilação estática de Xbox 360

**Escopo e método.** Pesquisa feita em 2026-10-20 com fontes públicas (repositórios, documentação, artigos e decisões judiciais) e com medições locais do `gh3-nx`. O foco é o que é acionável para um port de Guitar Hero III (Xbox 360) para Switch por recompilação estática (ReXGlue: PPC → C++ → ARM64) com renderer Vulkan nativo sobre NVK.

**Legenda usada neste documento.**

- **Fato citado** — afirmação com fonte inline (URL ou arquivo/linha do repo) ou medição local reproduzível.
- **Inferência** — raciocínio meu a partir dos fatos; não é afirmação de fonte.
- **Lacuna** — o que não consegui confirmar com fonte confiável.
- **Estimativa** — número aproximado, marcado como tal; não é medição.

**Resposta curta.** Recompilar não é decompilar. O ReXGlue produz C++ literal, função por função, a partir do binário — no GH3, 59.575 endereços de função e ~5,0 milhões de linhas de C++ gerado. Isso não é “código-fonte do jogo” no sentido de código legível com tipos, nomes e estruturas. “Reescrever boa parte do fonte” só é viável de duas formas: (a) decompilação matching completa, que é um projeto de multi-pessoa-ano e não tem precedente público para o GH3; ou (b) substituição cirúrgica de funções por hooks em C++, que é viável, já foi feita neste repo e é o caminho realista. Para desempenho, o GH3 no Switch é **GPU-bound**; reescrever lógica de jogo não ataca o gargalo. O ganho medido veio de cortar passes de GPU (+32% de FPS), não de mexer no código do jogo.

**Resposta curta sobre legalidade.** Não existe “recompilar é legal” universal. Compilar e jogar localmente, com o seu dump, é uma coisa; distribuir o C++ gerado, o NRO ou o source decompilado é outra, com risco alto — foi o que aconteceu com re3/reVC. O modelo com menor risco aparente é reimplementação independente + assets do usuário, ainda assim com revisão jurídica.

---

## 1. As abordagens e o que cada uma produz

### Takeaway

São quatro famílias distintas — recompilação estática, decompilação matching, engenharia reversa para source e reimplementação independente — mais um híbrido (recompilação + patches de decompilação). Elas produzem artefatos diferentes, com custos e riscos jurídicos diferentes. Para o GH3, o híbrido é o único caminho realista; a recompilação estática já está feita, e a decompilação total não é pré-requisito para portar nem para otimizar.

### Comparativo

| Abordagem | Entrada | O que produz | Fidelidade | Custo típico | Exemplos |
|---|---|---|---|---|---|
| **Recompilação estática** | Binário da console (XEX/ROM) + metadados de símbolos | C/C++ traduzido instrução por instrução, uma função por símbolo; precisa de runtime | Muito alta (semântica de instrução), mas sem semântica de fonte | Dias a semanas por jogo, se o recompilador e o runtime já existem; meses/anos para escrever o recompilador | N64Recomp, ReXGlue, XenonRecomp, gh3-nx |
| **Decompilação matching** | Binário + compilador/flags originais + símbolos + ROM | C/C++ que recompila para o **mesmo binário** (byte-perfect) | Total, verificável por hash | Meses a anos, com equipe; “escala de anos” para jogos de tamanho moderado | zeldaret/oot, decomp.me, projetos de N64 |
| **Engenharia reversa para source** | Binário + decompiler + símbolos de debug, quando existem | C++ legível, comportamento próximo, **não** byte-perfect | Alta, mas divergente em detalhes | Anos; re3 ~2 anos, reVC ~7 meses reaproveitando re3 | re3/reVC |
| **Reimplementação independente** | Comportamento/documentação/engenharia reversa | Engine nova, código independente (idealmente clean-room), normalmente GPL | Aproximada; não é o binário original | Anos; OpenRW começou em 2013 e ainda tinha “Version 1.0” como meta | OpenRW, OpenMW, OpenRA |
| **Híbrido: recompilação + patches** | Recompilação estática + funções decompiladas/escritas à mão | Núcleo recompilado + funções substituídas por link/hook | Núcleo exato; patches variam | Dias a semanas por patch; anos para cobrir muito código | Zelda64Recomp, Unleashed Recompiled, gh3-nx |

> Os custos dessa tabela são **ordens de grandeza** baseadas nos casos citados (relatados) e em estimativa; os números medidos neste repo estão na seção 6, com o rótulo de confiança.

### Fatos citados

- **Recompilação estática é tradução antecipada, não emulação.** O N64Recomp “aceita uma lista de símbolos e metadados ao lado do binário” e divide o binário em funções, cada uma recompilada para uma função C; a tradução é “muito literal para manter a complexidade baixa”, e `addiu $r4, $r4, 0x20` vira `ctx->r4 = ADD32(ctx->r4, 0x20)`. Chamadas `jal` viram chamadas de função; `jalr` vira `LOOKUP_FUNC(ctx->r25)` para o runtime resolver ponteiros. — [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- **O N64Recomp exige metadados de símbolos; hoje, na prática, um ELF.** O README diz que “a única forma de fornecer os metadados necessários é passar um arquivo ELF” e que o formato customizado para dispensar o ELF ainda é planejado. — [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- **O ReXGlue é um recompilador estático de Xbox 360.** “Converte código PowerPC do Xbox 360 em C++ portátil que roda nativamente em plataformas modernas”; é “fortemente enraizado no Xenia” e inspirado no XenonRecomp e no recompilador do rexdex. — [ReXGlue README](https://github.com/rexglue/rexglue-sdk)
- **O XenonRecomp também é estático, mas hoje só gera x86** por usar intrinsics x86; ele foi a base do Unleashed Recompiled (Sonic Unleashed, Xbox 360), lançado em 2025. — [XenonRecomp README](https://github.com/hedge-dev/XenonRecomp); [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp)
- **Decompilação matching exige o mesmo compilador, assembler e flags do original.** O objetivo é escrever C/C++ que compile para assembly idêntico; a verificação é binária (“ou o assembly bate, ou não bate”), e o decomp.me roda o m2c para gerar uma aproximação inicial em C que quase sempre precisa de refinamento manual. — [decomp.me FAQ](https://www.decomp.me/faq)
- **Matching decompilation é caro em escala.** O decomp.dev descreve a tarefa como “brutalmente difícil e demorada, na escala de anos para qualquer jogo de tamanho moderado com vários contribuidores ativos”. — [decomp.dev](https://decomp.dev/)
- **O caso Ocarina of Time: 21 meses de decompilação, concluída em novembro de 2021.** Em junho de 2020 o projeto estava em 17%; a decompilação foi feita pelo ZRET e não produzia um port, apenas o source que recompila para a ROM. — [Wikipedia: Ship of Harkinian](https://en.wikipedia.org/wiki/Ship_of_Harkinian); [Ars Technica](https://arstechnica.com/gaming/2021/11/reverse-engineering-team-completes-its-ocarina-of-time-decompilation/); [zeldaret/oot](https://github.com/zeldaret/oot)
- **O re3/reVC é engenharia reversa para source, não matching.** O README descreve o código como “completamente reverso dos binários originais”, e o histórico conta: início na primavera de 2018, ~10 mil linhas em maio de 2019, executável standalone em abril de 2020 com ~180 mil linhas, e reVC de maio a dezembro de 2020 reaproveitando o re3. — [re3 README (fork)](https://github.com/jp2411/re3/blob/miami/README.md)
- **O re3 começou substituindo funções individuais do binário original por versões reversas, via DLL.** Isso é exatamente o modelo “híbrido” de reescrita incremental, antes de virar executável standalone. — [re3 README (fork)](https://github.com/jp2411/re3/blob/miami/README.md)
- **O re3 se beneficiou de símbolos de debug das versões PS2/Android.** O Eurogamer cita o lead `aap`: “a código do GTA 3 no PS2 e Android inclui símbolos de debug”; o fluxo era pegar a saída do decompiler e “massagear de volta para C++ legível”. — [Eurogamer](https://www.eurogamer.net/how-a-small-group-of-gta-fanatics-reverse-engineered-gta-3-and-vice-city-without-so-far-getting-shut-down-by-take-two)
- **O Ship of Harkinian é um source port sobre a decompilação matching do OoT.** O README diz que o SoH depende do “source code fornecido pelo projeto de decompilação do OOT” e do `libultraship`, uma biblioteca que imita o `libultra` do N64 em hardware moderno; nenhum asset é distribuído, e o usuário precisa fornecer a ROM. — [Shipwright README](https://github.com/HarbourMasters/Shipwright)
- **O SoH saiu ~4 meses depois da decompilação.** Desenvolvimento começou em novembro de 2021, junto com a conclusão da decomp; em janeiro de 2022 o time estimava ~90% e o lançamento no Windows foi em março de 2022. — [Wikipedia: Ship of Harkinian](https://en.wikipedia.org/wiki/Ship_of_Harkinian)
- **O OpenRW é reimplementação, não decompilação.** O site o descreve como “re-implementação da engine” de GTA III, iniciada em 2013, com “Version 1.0” ainda como meta; exige uma cópia legítima do jogo e não distribui assets. — [OpenRW](https://www.openrw.org/); [README](https://github.com/rwengine/openrw)
- **O Zelda64Recomp é o exemplo mais claro de híbrido.** Os símbolos vêm da decompilação de Majora's Mask (`zeldaret/mm`): `syms.toml` acha as funções no binário e `datasyms` permitem que patches “sejam escritos copiando funções já matchadas da decompilação e ajustando”. — [Zelda64RecompSyms README](https://github.com/Zelda64Recomp/Zelda64RecompSyms)
- **O N64Recomp tem modo de patch por arquivo único.** Ele recompila só as funções patchadas em um único arquivo; se esse arquivo for passado ao linker antes do output original, os símbolos do patch têm prioridade. É o mecanismo de substituição de função do projeto — substituição em link-time, não hook em runtime. — [N64Recomp README](https://github.com/N64Recomp/N64Recomp)

### Inferências

- **“Conseguimos recompilar” não implica “temos o fonte”.** O artefato do ReXGlue é C++ de registro (`ctx.r3.u32`, `REX_STORE_U32`, offsets crus), não C++ com classes, structs, enums e nomes. No GH3, são 59.575 endereços registrados e ~5,0 milhões de linhas geradas; a média é ~85 linhas por função, mas quase todas são operações de registrador. — medido em `generated/`; `docs/porting-playbook.md:20`.
- **Reescrever “boa parte” do GH3 por decompilação total é inviável para um projeto solo.** Não há decompilação pública do GH3, não há símbolos no XEX e o jogo é de 2007, com engine proprietária Neversoft. Pela escala (OoT levou 21 meses com equipe; re3 ~2 anos com símbolos de debug), qualquer estimativa séria é multi-pessoa-ano. — **Estimativa**, não medição.
- **O caminho realista é o híbrido:** manter o núcleo recompilado, substituir funções específicas por C++ e investir no renderer. É o que o re3 fez no começo (DLL com funções reversas), o que o Zelda64Recomp faz com patches e o que o gh3-nx já faz com `guest_waits.h`.
- **O maior valor da decompilação, para o GH3, não é performance:** é modding, portabilidade e capacidade de entender/mudar sistemas. Se o objetivo é FPS no Switch, o retorno está em outro lugar (seção 4).

### Lacunas

- Não há decompilação pública do Guitar Hero III nem da engine Neversoft; não há fonte, símbolos ou mapa de tipos público.
- Não há número público de “quantas linhas de fonte” o GH3 teria; o número de 59.575 endereços de função não equivale a 59.575 funções de fonte (inclui thunks, tails e funções minúsculas).
- Não há dado público de custo por função de uma decompilação matching de Xbox 360; os números de OoT/re3 são de arquiteturas e épocas diferentes.

---

## 2. Como re3/reVC e Ship of Harkinian reconstruíram o fonte

### Takeaway

O SoH veio de uma decompilação matching já concluída (OoT, 21 meses) e de uma reimplementação da SDK (`libultraship`); o re3 veio de engenharia reversa com decompiler e símbolos de debug, em ~2 anos. Nenhum dos dois “recompilou e editou o C++ gerado”: são rotas diferentes, com custos diferentes. O Zelda64Recomp mostra o atalho: usar a decompilação como fonte de símbolos e patches, não como fonte completa.

### 2.1 re3/reVC: decompiler + símbolos + substituição incremental

**Fatos citados**

- O projeto formal começou na primavera de 2018; antes disso, o lead `aap` já mexia com engenharia reversa desde 2016, casualmente. — [Eurogamer](https://www.eurogamer.net/how-a-small-group-of-gta-fanatics-reverse-engineered-gta-3-and-vice-city-without-so-far-getting-shut-down-by-take-two); [re3 README](https://github.com/jp2411/re3/blob/miami/README.md)
- O método era: saída de decompiler → C++ legível, com erros e idas e voltas; os símbolos de debug do PS2/Android ajudaram a nomear funções e variáveis. — [Eurogamer](https://www.eurogamer.net/how-a-small-group-of-gta-fanatics-reverse-engineered-gta-3-and-vice-city-without-so-far-getting-shut-down-by-take-two)
- O começo foi substituir funções individuais do binário original por versões reversas, via DLL; só depois veio o executável standalone. — [re3 README](https://github.com/jp2411/re3/blob/miami/README.md)
- Escala: ~10 mil linhas em maio de 2019; ~180 mil em abril de 2020; estimativa de 200–250 mil linhas para o jogo final; reVC de maio a dezembro de 2020. — [re3 README](https://github.com/jp2411/re3/blob/miami/README.md)
- O re3 não tem licença: o README diz que o time “não se sente em posição de dar uma licença” e que o código deve ser usado apenas para fins educacionais, documentação e modding. — [re3 README](https://github.com/jp2411/re3/blob/miami/README.md)
- A Take-Two enviou DMCA em 19/02/2021; o repositório foi removido e depois restaurado após contra-notificação em junho de 2021; a ação judicial foi protocolada em 02/09/2021; as partes chegaram a um acordo em princípio em fevereiro de 2023. — [complaint](https://storage.courtlistener.com/recap/gov.uscourts.cand.384429/gov.uscourts.cand.384429.1.0.pdf); [NME](https://www.nme.com/news/take-two-and-grand-theft-auto-modders-will-settle-legal-battle-3394677)
- O re3 distribuía **código**, não assets; ainda assim, foi alvo de DMCA e processo. — [complaint](https://storage.courtlistener.com/recap/gov.uscourts.cand.384429/gov.uscourts.cand.384429.1.0.pdf)

**Inferências**

- Os símbolos de debug foram um multiplicador de produtividade enorme. Sem eles, o custo de nomear e tipar ~180 mil linhas teria sido muito maior.
- O GH3/XEX não tem equivalente público de símbolos; o ReXGlue batiza tudo de `sub_XXXXXXXX` e ainda precisa de heurística para achar fronteiras de função. Isso encarece qualquer tentativa de “source legível”.
- O modelo incremental do re3 (uma função de cada vez, via DLL) é o ancestral direto do modelo de hooks do ReXGlue. A diferença é que o re3 tinha um binário PC com símbolos e um ponto de injeção; o GH3 tem um runtime de recompilação com tabela de funções.

### 2.2 Ship of Harkinian: matching decomp + libultraship

**Fatos citados**

- A decompilação do OoT levou 21 meses e terminou em novembro de 2021; em junho de 2020 estava em 17%. — [Wikipedia](https://en.wikipedia.org/wiki/Ship_of_Harkinian)
- O projeto de decompilação `zeldaret/oot` tem como alvo padrão a ROM de debug `gc-eu-mq-dbg` e produz uma ROM com hash idêntico ao original; o README avisa que “não está produzindo um port de PC”. — [zeldaret/oot](https://github.com/zeldaret/oot)
- O SoH começou em novembro de 2021, junto com a conclusão da decomp; em janeiro de 2022 estava ~90%; lançou em março de 2022. — [Wikipedia](https://en.wikipedia.org/wiki/Ship_of_Harkinian)
- O SoH usa `libultraship`, que imita o `libultra` do N64 em plataformas modernas, e extrai os assets da ROM do usuário para um arquivo `.o2r`; não distribui assets. — [Shipwright README](https://github.com/HarbourMasters/Shipwright)
- A decompilação matching exige o mesmo compilador/flags e verificação byte a byte; o decomp.me existe para scratches de funções individuais nesse fluxo. — [decomp.me FAQ](https://www.decomp.me/faq)

**Inferências**

- A velocidade do SoH (4 meses até o primeiro release) não veio de recompilação estática: veio de uma decompilação matching **já pronta**, de uma ROM de debug e de uma comunidade grande. O trabalho pesado foi feito antes, no `zeldaret/oot`.
- Para o GH3, o equivalente não existe. Começar do zero seria refazer o “zeldaret/oot” do jogo, não o “SoH”.
- A ROM de debug é uma vantagem enorme: ela dá símbolos e nomes. O GH3 não tem uma versão de debug pública equivalente.

### 2.3 OpenRW: reimplementação independente

**Fatos citados**

- O OpenRW é uma reimplementação da engine de GTA III, iniciada em 2013, GPL-3.0, que exige uma cópia legítima do jogo e não distribui assets. — [OpenRW](https://www.openrw.org/); [README](https://github.com/rwengine/openrw)
- O site ainda define “Version 1.0” como meta: implementar todo o gameplay original, todos os formatos de dados, saves, mods de dados e rodar nativamente em Windows/Linux/macOS. — [OpenRW](https://www.openrw.org/)

**Inferências**

- Reimplementar a engine do zero é ainda mais caro do que decompilar: o OpenRW tem mais de uma década e, pelo próprio critério do site, ainda não chegou a 1.0. É a rota menos indicada para um port solo de GH3.
- A vantagem buscada é jurídica (código independente), não de prazo; para sustentar isso, o processo de clean-room precisa ser documentado.

### 2.4 Zelda64Recomp: o híbrido que funciona

**Fatos citados**

- O Zelda64Recomp usa o N64Recomp para recompilar o binário e usa os símbolos da decompilação de Majora's Mask para nomear funções e variáveis; os patches podem copiar funções matchadas da decomp e ajustar. — [Zelda64RecompSyms README](https://github.com/Zelda64Recomp/Zelda64RecompSyms)
- O N64Recomp permite recompilar só as funções patchadas em um arquivo único e usar o linker para dar prioridade a elas; é substituição em link-time, não hook em runtime. — [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- O ReXGlue oferece hooks de função em C++ (`REX_HOOK`, `REX_HOOK_RAW`, `REX_STUB`), com acesso ao `PPCContext` e ao `base` da memória do guest. — [hook.h](https://github.com/rexglue/rexglue-sdk/blob/main/include/rex/hook.h)

**Inferências**

- O híbrido é o melhor dos dois mundos: o núcleo recompilado garante fidelidade e cobre 99% do jogo; os patches cobrem o que você entende e quer mudar.
- O pré-requisito do híbrido “de verdade” é ter símbolos/nomes de algum lugar (decomp, debug build, engenharia reversa). No GH3, esse “de onde” não existe; os patches serão manuais, função por função.

### De onde vieram os símbolos e tipos

| Projeto | Fonte de símbolos/tipos | Consequência |
|---|---|---|
| **re3/reVC** | Símbolos de debug das versões PS2/Android + decompiler + trabalho manual | Nomes e tipos parciais; ainda assim, ~2 anos para ~180 mil linhas |
| **OoT/SoH** | Decompilação matching + ROM de debug (`gc-eu-mq-dbg`) + nomes da comunidade | Source legível e byte-perfect; base para o SoH |
| **Zelda64Recomp** | `syms.toml`/`datasyms` gerados da decompilação de Majora's Mask | Recompilação com nomes e patches que copiam funções matchadas |
| **GH3/ReXGlue** | Nenhum símbolo público; nomes `sub_XXXXXXXX`; heurística de fronteira + varredura de ponteiros | C++ literal, sem tipos; 13 `Unresolved` no codegen, 91 funções adicionadas à mão no GH3 |

### Lacunas

- Não encontrei documentação pública sobre símbolos internos do `default.xex` do GH3 (nem debug build, nem PDB, nem mapa de símbolos).
- Não há número público de “linhas de fonte” do GH3; a comparação com re3/OoT é de ordem de grandeza, não de escala exata.
- Não há fonte pública que descreva o custo de uma decompilação matching de um jogo de 2007 com engine proprietária e sem símbolos.

---

## 3. Reescrever partes de um jogo recompilado: hooks, limites e o que é viável

### Takeaway

Sim, dá para substituir funções de um jogo recompilado por C++ nativo. O ReXGlue expõe isso como API de primeira classe: o corpo gerado fica num símbolo fraco (`sub_X`), o hook forte substitui, e o original continua acessível via `__imp__sub_X`. O que **não** dá é “reescrever o jogo” ignorando o ABI, a memória do guest, o dispatch por ponteiro e as funções impuras. A unidade viável é a função pequena, com contrato claro; a unidade inviável é o sistema grande e cheio de estado inline.

### Como o ReXGlue faz

**Fatos citados**

- `REX_HOOK(subroutine, function)` faz hook de uma função recompilada com tradução automática de tipos; `REX_HOOK_RAW(name)` dá acesso direto a `PPCContext& ctx` e `uint8_t* base`; `REX_STUB` e `REX_STUB_RETURN` permitem stubar. — [hook.h](https://github.com/rexglue/rexglue-sdk/blob/main/include/rex/hook.h)
- O `CallFrame` isola um contexto de chamada e copia só `r1`, `r13` e `fpscr` do contexto pai; a chamada “auto-isolating” do `ImportFunction` cria um `PPCContext` com frame de `0x70` e devolve `r3`/`f1`. — [hook.h](https://github.com/rexglue/rexglue-sdk/blob/main/include/rex/hook.h)
- No gh3-nx, `src/guest_waits.h` substitui dois accessors de contador (`sub_82172718` e `sub_82172758`) por hooks que chamam o original e, após N leituras iguais no mesmo thread, dormem ~100 µs em vez de girar. — `src/guest_waits.h:126-151`
- O comentário do arquivo explica o porquê: no Switch, o processo tem 3 núcleos e o perfil mostrava 282% de 300%; um thread sozinho queimava 90% de um núcleo girando no laço. O contrato é preservado: o guest sempre recebe o valor real do contador; nada é falseado, pulado ou reordenado. — `src/guest_waits.h:20-32`
- O `Install()` usa `runtime->function_dispatcher()->GetFunction(kSwapCounter)` para conferir que os endereços existem no dump carregado. — `src/guest_waits.h:112-121`
- O codegen emite chamadas diretas como `sub_X(ctx, base)`; `tools/llamadas_diretas.py` troca para `__imp__sub_X` para liberar inline/LTO — **exceto** para funções com hook, que precisam continuar em `sub_X` para passar pelo símbolo forte. Chamadas indiretas (`REX_CALL_INDIRECT_FUNC`) não são tocadas. — `tools/llamadas_diretas.py:1-13`
- O teste `tests/test_guest_waits.py` confere que a lista `HOOKED` do script é a mesma dos endereços do header e que os acessores/laços no gerado ainda são os esperados. — `tests/test_guest_waits.py:1-10`

### Como outros recompiladores fazem

**Fatos citados**

- O XenonRecomp define funções PPC com alias para uma “implementation function” e símbolo fraco; o usuário pode sobrescrever com implementação custom e ainda chamar o original via `__imp__`. Além disso, permite **mid-asm hooks** em endereços de instrução, com lista de registradores e opções de `return`, `return_on_true/false` e salto para endereço dentro da mesma função. — [XenonRecomp README](https://github.com/hedge-dev/XenonRecomp)
- O N64Recomp substitui funções em **link-time**: recompila só as funções patchadas em um arquivo único, e o linker dá prioridade aos símbolos desse arquivo. Não há hook em runtime na API descrita no README. — [N64Recomp README](https://github.com/N64Recomp/N64Recomp)
- O N64Recomp traduz `jalr` como `LOOKUP_FUNC(ctx->r25)`, ou seja, chamadas por ponteiro passam por uma tabela do runtime; substituir o símbolo da função não garante interceptar quem chega por ponteiro. — [N64Recomp README](https://github.com/N64Recomp/N64Recomp)

### Limites práticos

| Limite | Por que existe | Consequência para o port |
|---|---|---|
| **Estado de registradores/ABI** | O hook recebe `PPCContext`; precisa preservar não-voláteis, `r1` (stack), `lr`, `cr`, `xer`, `fpscr`, e devolver em `r3`/`f1` conforme a assinatura | Um hook “em C++ puro” que ignora o ABI corrompe o guest; use `REX_HOOK` tipado quando possível e `REX_HOOK_RAW` só com disciplina |
| **Memória do guest e endianness** | Ponteiros do guest são 32-bit somados a `base`; o Xbox 360 é big-endian e o código gerado usa macros `REX_LOAD/STORE_*` para converter | Acessar memória com `memcpy`/ponteiro host direto dá valor trocado; use as macros do runtime |
| **Chamada direta vs indireta** | `bl` pode virar `sub_X` (passa pelo hook) ou `__imp__sub_X` (vai direto ao corpo); `jalr`/ponteiro vai pela tabela do dispatcher | É preciso manter a lista de funções hookadas fora da reescrita (`llamadas_diretas.py`) **e** garantir que a entrada da tabela/ponteiro também aponte para o hook |
| **Fronteiras de função, inline e tail call** | O recompilador precisa adivinhar onde uma função começa/termina; código gerado pode ter cauda de função, thunk ou salto | Registrar “buracos” demais quebra o fluxo: no GH3, `--all` com 1226 buracos derrubou o boot de ~3700 para 19 quadros em 60 s — `docs/porting-playbook.md:56-58` |
| **Funções impuras** | Elas leem/escrevem globals, chamam o renderer, mexem em listas, áudio, física, locks | Não dá para “pular” a função; o padrão seguro é chamar o original e pós-processar, como o `guest_waits` faz |
| **Threads e timing** | O guest tem threads próprias; o hook pode rodar em qualquer uma; o jogo pode depender de ordem/quantização de vblank | Use `thread_local` para estado por thread; não introduza sleeps/locks sem medir; mudar contagem de instruções pode alterar timing |
| **Ponto flutuante/VMX/varargs/exceções** | PPC tem FP preciso, VMX128, varargs e convenções próprias; o host ARM64 não é idêntico | Reescrever funções que usam VMX128 ou varargs é caro e arriscado; prefira manter o gerado |
| **Tipos e assinaturas** | O XEX não tem nomes/tipos; o gerado chama `sub_X(ctx, base)` sem assinatura | Você precisa recuperar a assinatura por engenharia reversa (call sites, Ghidra, debug build) antes de escrever um hook tipado |
| **Regeneração do gerado** | O build do ReXGlue regenera `generated/`; editar arquivo gerado fora do build é sobrescrito | Correções de código gerado têm de ser `POST_BUILD` ou upstream; prefira hooks |
| **Dispatch por vtable/ponteiro** | Muitos sistemas de jogo usam vtables, callbacks e ponteiros de função; o símbolo pode não ser o alvo real | Substituir a função não basta se o ponteiro foi capturado antes; pode ser preciso trocar a entrada na tabela/vtable |

### Receita prática para um hook no gh3-nx

1. **Escolha pelo perfil, não pela intuição.** O alvo tem de estar quente no profiler e ser uma função pequena, com contrato observável (accessor, getter/setter, clamp, conversão, callback).
2. **Recupere a assinatura.** Veja os call sites no gerado, o `PPCContext` e os usos de `r3..r10`/`f1`. Se não der para provar a assinatura, use `REX_HOOK_RAW` e mexa só no que precisa.
3. **Chame o original.** O corpo original é `__imp__sub_X(ctx, base)`; o hook forte `sub_X` substitui o alias fraco. Isso permite “antes/depois” sem reimplementar a função inteira.
4. **Preserve o contrato.** Se o guest espera o valor real do contador, devolva o valor real; não “falseie” leitura para ganhar FPS. O `guest_waits` é o exemplo: ele só cede CPU depois de detectar repetição, e o valor devolvido continua sendo o do jogo.
5. **Acerte o dispatch.** Adicione o endereço à lista `HOOKED` de `tools/llamadas_diretas.py`; confira `function_dispatcher()->GetFunction(addr)` no `OnPostSetup`; se a função for chamada por ponteiro/vtable, troque a entrada da tabela também.
6. **Prove com oráculo.** Boot test, captura de paridade native/emulated no mesmo trecho de música e FPS; sem isso, é fé. Ferramentas: `tools/boot_test.py`, `tools/gameplay_capture.py`, `tools/image_stats.py`, `tools/gameplay_fps.py`.
7. **Registre o custo.** Meça FPS/frame time antes/depois; se o ganho for zero, reverta. No caso do `guest_waits`, o ganho é CPU/thermals, não FPS — e isso precisa ser dito.

Exemplo mínimo, no estilo do repo:

```cpp
extern "C" void __imp__sub_82172718(PPCContext& ctx, uint8_t* base);

REX_HOOK_RAW(sub_82172718) {
  __imp__sub_82172718(ctx, base);   // comportamento original
  gh3::guest_waits::AfterRead(ctx.r3.u64);  // pós-processamento
}
```

### Inferências

- **Hooking é viável para dezenas/centenas de funções, não para “boa parte” do jogo.** Cada função exige entender assinatura, estado e efeitos; sem símbolos, isso é decompilação manual disfarçada.
- **As funções mais seguras de reescrever são folhas e puras:** accessors, matemática, conversões, clamps, callbacks simples. As mais perigosas são as que misturam estado global, renderer, áudio e threads.
- **O `guest_waits` é um caso ideal:** dois accessors minúsculos, contrato claro, efeito colateral controlado. Ele não prova que dá para reescrever um sistema inteiro; prova que dá para resolver um problema específico com pouco código.
- **Se o objetivo é “ter o fonte para moddar”, hooks não bastam.** Você vai querer tipos, structs e nomes; isso é decompilação, não recompilação.

### Lacunas

- O ReXGlue documenta hooks de função, mas não encontrei documentação pública de **mid-asm hooks** no estilo do XenonRecomp; para patch no meio da função, o caminho provável é `POST_BUILD`/upstream, não a API de hook.
- Não há benchmark público do custo de um hook `REX_HOOK_RAW` (overhead de chamada, impacto de inline) em ARM64/Switch.
- Não há ferramenta pública de recuperação de tipos/assinaturas para XEX de 360; isso continua manual (Ghidra/IDA + paciência).

---

## 4. Performance: reescrever o fonte ajuda num jogo GPU-bound?

### Takeaway

**Não, se “reescrever” significa lógica de jogo.** Num jogo GPU-bound, o CPU já entrega comandos mais rápido do que a GPU consegue executar; acelerar o CPU (ou reescrever lógica) não aumenta FPS. **Sim, se “reescrever” significa mudar o renderer:** menos passes, menos overdraw, menos resolução, menos banda, menos resolves. No GH3 do Switch, a evidência local é clara: PGO e ajustes de codegen não deram ganho; cortar sombra e bloom deu +32% de FPS.

### Fatos citados

- **Definição de GPU-bound:** o CPU gera instruções de renderização mais rápido do que a GPU consegue processá-las; o CPU fica ocioso e o FPS é limitado pela GPU. **CPU-bound** é o oposto. — [DirectX Developer Blog](https://devblogs.microsoft.com/directx/cpu-and-gpu-boundedness); [Intel](https://www.intel.com/content/www/us/en/developer/articles/technical/locate-and-resolve-cpu-gpu-bottlenecks.html)
- **O GH3 no Switch é GPU-bound.** O README diz explicitamente: “On the Switch the game is **GPU-bound**”; o perfil mostra threads esperando, não calculando, e o PGO/codegen não deu ganho mensurável; cortar dois passes de GPU deu +32%. — `README.md:33-35`, `README.md:56-59`
- **Medições no Switch (Mariko, portátil, stock):**

| Mudança | FPS mediano | Renderer CPU / GPU por quadro | Veredito |
|---|---:|---|---|
| base (T13+T14) | ~34 | 21–39 / 17–29 ms | — |
| PGO (`GH3_PGO=gerar` → jogar → `usar`) | ~31 | ~25 / ~21 ms | sem ganho visível (o gargalo não é CPU) |
| ZCULL (`gh3_native_zcull` + patch Mesa) | ~22 | 33–41 / 25–30 ms | pior ou ruído; desligado |
| `gh3_vblank_late_no_wait` | — | — | sem ganho (frames lentos não são quantização de vblank) |
| **sem sombra e bloom** | **~45** | **13 / 13 ms**; frames >50 ms por 10 s: 46–108 → 8 | **ligado no `switch/gh3.toml`** |

  — `docs/porting-playbook.md:145-150`
- **O profiler simbolizado mostrava quase tudo em syscall de espera** (`svcSleepThread`, `svcWaitProcessWideKeyAtomic`, `svcWaitSynchronization`), não em código do jogo; as funções mais quentes do jogo ficavam em ~13–18% de um núcleo. — `docs/porting-playbook.md:152-157`
- **Custo por passe de GPU (ms/quadro):** passe principal 720p cor+profundidade 7–11 ms; segundo passe 720p 3–5 ms; resolves ~1,5–2 ms; sombra+bloom juntos ~5–10 ms. — `docs/porting-playbook.md:164-166`
- **O sinal de GPU é `fences esperadas`:** 505–656 ms/s na base, 341 ms/s sem sombra/bloom. — `docs/porting-playbook.md:157`
- **O renderer nativo substitui a GPU Xenos emulada**; o jogo em si continua sendo o código PPC recompilado, mas o desenho é feito pelo renderer Vulkan nativo. — `README.md:3-6`, `src/gh3_app.h:76-90`
- **O Xbox 360 é big-endian** (PowerPC); o código gerado usa macros de load/store para converter. — [Georgia Tech, Xbox 360 System Architecture](https://faculty.cc.gatech.edu/~hyesoon/spr11/lec_xbox1.pdf); `generated/default/gh3_recomp.119.cpp:1-12`
- **O Switch tem 3 núcleos disponíveis no processo** (máscara 0x7) e o perfil chegou a 282% de 300%; um thread queimava 90% de um núcleo no laço de espera. — `src/guest_waits.h:20-22`
- **O clock de GPU no portátil é 460,8 MHz** no modo performance aplicado pelo `switch_apm.h`. — `docs/porting-playbook.md:140`

### Por que reescrever lógica não resolve

**Inferências**

- Uma função reescrita pode ficar **mais rápida**; o jogo não necessariamente fica, porque o FPS é limitado pelo elo mais lento do frame. Se a GPU é o limite, ganhar microssegundos no CPU não aparece no contador de quadros.
- O frame tem duas metades: o CPU monta o command buffer (lógica, cena, draw calls) e a GPU executa vértices/pixels/shaders. Se a GPU é o limite, o CPU pode ficar mais rápido sem mudar o FPS. É a lei de Amdahl aplicada ao frame: otimizar a parte que não é gargalo rende ~0.
- O GH3 no Switch já tem um renderer nativo; o custo dominante é o trabalho de GPU (passes, resolves, sombra/bloom, banda) e o overhead do NVK. Reescrever `sub_XXXXXXXX` de lógica de jogo não muda shader, não reduz overdraw e não acelera o NVK.
- O único jeito de uma reescrita de lógica ajudar é se ela **reduzir trabalho de GPU**: menos draw calls, menos estado, menos overdraw, menos alvos, menos resolves, ou se ela destravar um CPU submission stall. Isso é reescrever o sistema de cena/renderer, não “o jogo” em geral.
- Um source port completo com um renderer novo (batching, culling, instancing, resolução dinâmica) poderia, sim, ser mais rápido. Mas o ganho viria do renderer, não do fato de o código estar em C++ legível. O gh3-nx já fez a parte que importa: renderer nativo.

### O que realmente mexe no FPS (na ordem que os dados sugerem)

1. **Passes de GPU:** cortar/otimizar sombra e bloom foi o maior ganho medido (+32%). O bloom, aliás, era o que estourava o branco da cena; desligá-lo resolveu visual e performance. — `docs/porting-playbook.md:150`, `docs/porting-playbook.md:168`
2. **Resolução interna do 3D:** renderizar a cena 3D abaixo de 720p e manter HUD/UI nativa; é a alavanca clássica de GPU-bound e ainda não estava implementada no momento das medições. — `README.md:35-36`
3. **Resolves/MSAA:** resolves custam ~1,5–2 ms/quadro; reduzir ou fundir alvos pode render. — `docs/porting-playbook.md:166-167`
4. **Banda e alvos compartilhados:** o caso do estágio 3D em EDRAM reutilizado como alvo 1× mostra que o desenho de alvos importa; otimizar isso é renderer, não lógica. — `README.md:52-54`
5. **Present/vblank:** o modo `immediate`/`mailbox` travava no MoltenVK; no Switch, a sobreposição de present é trabalho de renderer/runtime. — `docs/porting-playbook.md:34`; `README.md:35-36`
6. **CPU:** só depois de esgotar GPU. O `guest_waits` liberou CPU (thermals/frame pacing), mas não há ganho de FPS registrado; PGO não ajudou. — `src/guest_waits.h:20-32`, `docs/porting-playbook.md:147`

### Lacunas

- Não há medição de um subsistema de jogo reescrito em C++ para provar “ganho zero” de forma geral; a evidência é indirecta (PGO/codegen sem ganho + perfil de espera).
- Não há benchmark do teto do NVK no Switch para este jogo; não dá para saber quanto do 13/13 ms é GPU e quanto é driver sem um teste com renderer “null” ou resolução mínima.
- Não há medição de resolução interna reduzida no GH3; é a próxima experiência óbvia, mas ainda não foi feita.

---

## 5. Legal/IP: recompilação pessoal vs redistribuir lógica

> **Isto não é aconselhamento jurídico.** As notas abaixo resumem fontes públicas e servem para orientar risco de engenharia; qualquer decisão de distribuir código ou binário derivado deve passar por advogado na jurisdição relevante.

### Takeaway

Manter uma recompilação **pessoal**, sem distribuir código derivado nem assets, é uma coisa; distribuir C++ gerado, NRO com código do jogo ou source decompilado é outra. O código é protegido por copyright, e “recompilar” não apaga isso. O caso re3/reVC mostra que distribuir source reverso, mesmo sem assets, atrai DMCA e processo. O modelo OpenRW (reimplementação independente + assets do usuário) reduz risco, mas não é garantia. O SoH reduz o risco de assets, mas o código dele é derivado da decompilação; não é clean-room.

### Quatro modelos de distribuição

| Modelo | O que você distribui | Risco prático | Exemplos |
|---|---|---|---|
| **Recompilação pessoal** | Nada; você compila e joga na sua máquina, com seu dump | Menor, mas não zero: extrair/decifrar o XEX pode envolver §1201; o código derivado continua sendo derivado | gh3-nx local |
| **Distribuir C++ gerado / NRO** | Tradução do binário do jogo, com ou sem assets | Alto: é distribuição de obra derivada; o NRO contém código do jogo | não recomendado |
| **Distribuir source decompilado** | C/C++ reverso do binário | Alto: foi o que aconteceu com re3/reVC (DMCA + ação); o SoH não distribui assets, mas o código é derivado | re3/reVC; SoH |
| **Reimplementação independente + assets do usuário** | Engine nova, código não derivado (idealmente clean-room); usuário fornece ROM/assets | Menor, mas não zero: marcas, patentes, formatos, DMCA de assets | OpenRW |

### Fatos citados

- **Sega v. Accolade (9º Cir., 1992):** a desmontagem/descompilação de código objeto para entender elementos funcionais e fazer um produto compatível foi considerada fair use; a decisão enfatizou que o produto final não podia conter material infringente. — [Copyright Office summary](https://www.copyright.gov/fair-use/summaries/segaenters-accolade-9thcir1992.pdf); [Justia](https://law.justia.com/cases/federal/appellate-courts/F2/977/1510/305345/)
- **Sony v. Connectix (9º Cir., 2000):** a cópia intermediária de código para desenvolver um emulador foi tratada como fair use, no mesmo espírito de Sega; o caso trata de cópia intermediária para interoperabilidade, não de uma licença para distribuir o código original. — [Berkeley Law PDF](https://lawcat.berkeley.edu/nanna/record/1117660/files/fulltext.pdf?withWatermark=0&withMetadata=0&registerDownload=1&version=1)
- **DMCA §1201:** a circunvenção de medida tecnológica de controle de acesso é ilícito separado; há exceção estreita de engenharia reversa para interoperabilidade em §1201(f), e a lei ressalva que nada afeta as defesas de fair use contra **violação de copyright** — o que não é o mesmo que uma defesa geral contra §1201(a). — [17 U.S.C. §1201](https://www.copyright.gov/title17/92chap12.html)
- **re3/reVC:** a Take-Two enviou DMCA em 19/02/2021, o repo foi removido e restaurado após contra-notificação; a ação foi protocolada em 02/09/2021; as partes chegaram a acordo em princípio em fevereiro de 2023. O projeto distribuía código, não assets. — [complaint](https://storage.courtlistener.com/recap/gov.uscourts.cand.384429/gov.uscourts.cand.384429.1.0.pdf); [NME](https://www.nme.com/news/take-two-and-grand-theft-auto-modders-will-settle-legal-battle-3394677)
- **O re3 não tinha licença** e dizia explicitamente que o código deveria ser usado apenas para fins educacionais, documentação e modding. — [re3 README](https://github.com/jp2411/re3/blob/miami/README.md)
- **O SoH não distribui assets**: exige uma ROM legal do usuário e extrai os assets para `.o2r`; o código vem da decompilação do OoT. — [Shipwright README](https://github.com/HarbourMasters/Shipwright)
- **O OpenRW é GPL-3.0 e exige uma cópia legítima do GTA III**; não distribui assets. — [OpenRW README](https://github.com/rwengine/openrw)
- **O Unleashed Recompiled também não distribui assets**; exige arquivos de uma cópia legal do Sonic Unleashed e usa XenonRecomp/XenosRecomp. — [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp)
- **O gh3-nx já adota a postura conservadora:** `generated/` e `extracted/` são gitignored, o `default.xex` é conferido por SHA-256 e o README diz que o pacote Switch é “for personal use only — never publish it”. — `README.md:9-13`, `src/dump_gate.h`, `tools/check_no_game_content.sh`

### Inferências

- **C++ gerado pelo ReXGlue é uma tradução do XEX; distribuí-lo é distribuir uma obra derivada.** O fato de ser “gerado automaticamente” não muda a natureza: é o jogo, em outra sintaxe. Manter `generated/` fora do git e o NRO privado é coerente com isso.
- **Um NRO que embute o código recompilado é ainda mais exposto:** ele contém a tradução executável do jogo. Publicar o NRO é publicar o jogo (mesmo sem os assets), na visão de um detentor de direitos.
- **Patches pequenos e diffs têm risco menor, mas não zero.** Se o patch reproduz substantialmente a lógica original, pode ser derivado; se é uma implementação independente de uma interface, o argumento é melhor. A linha não é nítida.
- **O modelo com menor risco aparente para publicação** é a reimplementação independente + assets do usuário (OpenRW): código não derivado, sem assets, usuário fornece o dump. Ainda assim, há risco de marca, patente e de o código ser considerado derivado se foi escrito olhando o binário; “clean room” é um processo documentado, não um selo. O SoH não distribui assets, mas o source é derivado da decompilação; o risco de copyright do código continua.
- **O acordo do re3 não é precedente judicial.** Ele encerra um caso específico; não estabelece que source reverso é legal. Trate como sinal de risco, não como autorização.
- **A jurisdição importa.** Sega/Connectix são dos EUA (9º Circuito); Brasil e UE têm regimes diferentes. O §1201 pode não ter equivalente exato, mas proteção de software e medidas tecnológicas existem em vários ordenamentos.
- **“Preservação” e “abandonware” não são licenças.** São argumentos culturais/políticos; não criam, por si, uma exceção legal. O que reduz risco é o que você distribui e como, não a idade do jogo.

### Lacunas

- Não encontrei caso judicial específico sobre **recompilação estática** de jogo de console; a analogia com emulação/decompilação é o que existe.
- Não há resposta pública definitiva sobre distribuir **diffs/patch files** que contenham trechos substanciais de código derivado.
- Não há análise jurídica específica para o GH3 ou para a engine Neversoft; e eu não sou advogado.
- Não analisei os termos de uso da Nintendo/Switch nem o regime de anticircunvenção do console; o foco aqui foi copyright do jogo e distribuição do código derivado.

---

## 6. Números, confiança e plano para o gh3-nx

### Números com confiança

| Número | Valor | Fonte | Confiança |
|---|---:|---|---|
| Funções/endereços registrados no GH3 | 59.575 | `docs/porting-playbook.md:20` | **Medido** |
| Arquivos de codegen | 291 | `docs/porting-playbook.md:20` | **Medido** |
| Arquivos `.cpp` gerados | 144 | `find generated -name '*.cpp' \| wc -l` | **Medido** |
| Linhas de C++ gerado | 5.044.139 | `find generated -name '*.cpp' -print0 \| xargs -0 wc -l` | **Medido** (inclui comentários/whitespace) |
| Tamanho de `generated/` | 151 MB | `du -sh generated` | **Medido** |
| `Unresolved` no codegen | 13 | `docs/porting-playbook.md:20` | **Medido** |
| Funções adicionadas à mão | 91 | `docs/porting-playbook.md:56` | **Medido** |
| Buracos `--all` que quebraram o boot | 1.226 → 19 quadros | `docs/porting-playbook.md:57-58` | **Medido** |
| FPS no Switch, full effects | ~34 | `README.md:30` | **Medido** |
| FPS no Switch, sem sombra/bloom | ~45 | `README.md:31` | **Medido** |
| Ganho ao cortar sombra/bloom | ~+32% | `README.md:58-59`; `docs/porting-playbook.md:150` | **Medido** |
| GPU por quadro, full effects | 17–29 ms | `docs/porting-playbook.md:145-150` | **Medido** |
| GPU por quadro, sem sombra/bloom | 13 ms | `docs/porting-playbook.md:150` | **Medido** |
| PGO no Switch | ~31 FPS (vs ~34) | `docs/porting-playbook.md:147` | **Medido** |
| Build Switch (NRO) | 512 s, `-j 6` | `docs/porting-playbook.md:23` | **Medido** |
| Rebuild incremental | ~7,5 s | `README.md:46-47` | **Medido** |
| Decompilação do OoT | 21 meses | [Wikipedia](https://en.wikipedia.org/wiki/Ship_of_Harkinian) | **Relatado** |
| SoH até o primeiro release | ~4 meses após a decomp | [Wikipedia](https://en.wikipedia.org/wiki/Ship_of_Harkinian) | **Relatado** |
| re3 até o standalone | ~2 anos (2018–2020) | [re3 README](https://github.com/jp2411/re3/blob/miami/README.md) | **Relatado** |
| reVC | ~7 meses (2020) | [re3 README](https://github.com/jp2411/re3/blob/miami/README.md) | **Relatado** |
| OpenRW | iniciado em 2013; 1.0 ainda meta | [OpenRW](https://www.openrw.org/) | **Relatado** |
| Decompilação total do GH3 | multi-pessoa-ano | estimativa baseada em OoT/re3 + 59.575 funções | **Estimado** |
| Hook de função simples | horas a dias | `guest_waits` (2 funções, 151 linhas) como ordem de grandeza | **Estimado** |
| Reescrever um subsistema | semanas a meses | estimativa | **Estimado** |

### O que eu faria no gh3-nx

**Decisão principal: não reescrever “boa parte” do jogo.** O retorno não paga o custo, e o gargalo é GPU. O caminho com melhor relação esforço/resultado é:

1. **Continuar no núcleo recompilado.** Ele é fiel, cobre o jogo inteiro e já boota. Cada função que você “reescreve” é uma dívida de fidelidade e uma regressão em potencial.
2. **Usar hooks só para problemas cirúrgicos.** O `guest_waits` é o modelo: função pequena, contrato claro, chamada ao original, pós-processamento, teste de paridade. Não tente reimplementar sistemas grandes por hook.
3. **Investir no renderer/GPU.** É onde os números mandam: passes, resolução interna do 3D, resolves, sombra/bloom, present. O ganho de +32% veio daí, não do código do jogo.
4. **Medir cada mudança com oráculo.** `boot_test.py` (quadros), `gameplay_capture.py` + `image_stats.py` (paridade), `gameplay_fps.py` (FPS/frame time). Sem isso, é opinião.
5. **Se o objetivo virar modding/preservação, decompilar por subsistema.** Escolha um sistema (áudio, input, cena) e faça engenharia reversa com Ghidra/IDA + testes; não comece pelo jogo inteiro. O custo é de meses, não de dias.
6. **Antes de publicar qualquer coisa, revisar o jurídico.** Manter `generated/`, `extracted/` e o NRO privados é o padrão atual; publicar NRO ou C++ gerado é outro patamar de risco. Se a intenção é distribuir, o modelo de menor risco aparente é reimplementação independente + assets do usuário, com aconselhamento jurídico.

### Três experiências concretas (da mais barata para a mais cara)

1. **Resolução interna do 3D abaixo de 720p, HUD nativa.** Se o FPS subir, confirma GPU/banda; se não subir, o gargalo é outro (driver/CPU submission). Custo: dias. — baseado em `README.md:35-36`, ainda não medido.
2. **Hook de uma função CPU-hot de verdade.** Escolha pelo profiler uma função de áudio/animacao/input que não seja accessor; reimplemente só ela com `REX_HOOK_RAW`, preserve o contrato e meça FPS/frame time. Se não houver ganho, pare de investir em lógica. Custo: dias a semanas. — método em `src/guest_waits.h`, `tools/llamadas_diretas.py`.
3. **Decompilação de um subsistema pequeno.** Ex.: mixer de áudio ou input. Recupere assinaturas, structs e nomes; escreva C++ equivalente; valide com paridade de áudio/frames. Custo: semanas a meses; só faz sentido se o objetivo for modding/portabilidade. — **Estimativa**.

### Árvore de decisão

- **Objetivo = FPS no Switch?** → renderer/GPU. Hooks só para stalls de CPU. Não reescrever lógica.
- **Objetivo = mods/portabilidade de longo prazo?** → decompilação por subsistema, com source legível; comece pelo que você quer mudar, não pelo jogo inteiro.
- **Objetivo = publicar um port?** → reimplementação independente + assets do usuário; não distribuir `generated/` nem NRO com código do jogo; revisar juridicamente.
- **Objetivo = “entender o jogo”?** → decompilação/engenharia reversa é ótima, mas é pesquisa, não port; o custo é multi-pessoa-ano se for o jogo inteiro.

### Lacunas globais

- Não há decompilação pública do GH3 nem símbolos; qualquer estimativa de “reescrever o fonte” é de ordem de grandeza.
- Não há benchmark de reescrita de função no ReXGlue/ARM64; o `guest_waits` é o único caso local, e ele é um patch de comportamento, não uma reimplementação.
- Não há caso judicial específico sobre recompilação estática; a análise legal é analógica.
- O teto do NVK no Switch para este jogo não foi medido com rigor (renderer null/resolução mínima).

---

## Fontes

### Primárias — recompilação, decompilação e source ports

- N64Recomp — [README](https://github.com/N64Recomp/N64Recomp)
- ReXGlue — [README](https://github.com/rexglue/rexglue-sdk); [hook.h](https://github.com/rexglue/rexglue-sdk/blob/main/include/rex/hook.h)
- XenonRecomp — [README](https://github.com/hedge-dev/XenonRecomp)
- Zelda64RecompSyms — [README](https://github.com/Zelda64Recomp/Zelda64RecompSyms)
- zeldaret/oot — [README](https://github.com/zeldaret/oot)
- decomp.me — [FAQ](https://www.decomp.me/faq)
- decomp.dev — [Projects](https://decomp.dev/)
- Ship of Harkinian — [Shipwright README](https://github.com/HarbourMasters/Shipwright); [Wikipedia](https://en.wikipedia.org/wiki/Ship_of_Harkinian)
- re3/reVC — [README (fork)](https://github.com/jp2411/re3/blob/miami/README.md); [Eurogamer](https://www.eurogamer.net/how-a-small-group-of-gta-fanatics-reverse-engineered-gta-3-and-vice-city-without-so-far-getting-shut-down-by-take-two)
- OpenRW — [site](https://www.openrw.org/); [README](https://github.com/rwengine/openrw)
- Unleashed Recompiled — [README](https://github.com/hedge-dev/UnleashedRecomp)
- Ars Technica — [Ocarina of Time decompilation](https://arstechnica.com/gaming/2021/11/reverse-engineering-team-completes-its-ocarina-of-time-decompilation/)

### Performance e arquitetura

- DirectX Developer Blog — [CPU- and GPU-boundedness](https://devblogs.microsoft.com/directx/cpu-and-gpu-boundedness)
- Intel — [Locate and Resolve CPU-GPU Bottlenecks](https://www.intel.com/content/www/us/en/developer/articles/technical/locate-and-resolve-cpu-gpu-bottlenecks.html)
- Xbox 360 technical specifications — [Wikipedia](https://en.wikipedia.org/wiki/Xbox_360_technical_specifications)
- Xbox 360 System Architecture (big-endian) — [Georgia Tech slide deck](https://faculty.cc.gatech.edu/~hyesoon/spr11/lec_xbox1.pdf)

### Legal

- Sega v. Accolade — [Copyright Office summary](https://www.copyright.gov/fair-use/summaries/segaenters-accolade-9thcir1992.pdf); [Justia](https://law.justia.com/cases/federal/appellate-courts/F2/977/1510/305345/)
- Sony v. Connectix — [Berkeley Law PDF](https://lawcat.berkeley.edu/nanna/record/1117660/files/fulltext.pdf?withWatermark=0&withMetadata=0&registerDownload=1&version=1)
- 17 U.S.C. §1201 — [Copyright Office](https://www.copyright.gov/title17/92chap12.html)
- Take-Two v. Papenhoff et al. — [complaint](https://storage.courtlistener.com/recap/gov.uscourts.cand.384429/gov.uscourts.cand.384429.1.0.pdf)
- Acordo re3/reVC — [NME](https://www.nme.com/news/take-two-and-grand-theft-auto-modders-will-settle-legal-battle-3394677)

### Evidência local do gh3-nx

- `README.md` — status, FPS, arquitetura, aviso de uso pessoal
- `docs/porting-playbook.md` — §1 build, §3 funções faltantes, §8 diagnóstico GPU
- `src/guest_waits.h` — hooks de accessor, contrato e dispatch
- `src/guest_waits_spin.h` — detector puro, testável sem SDK
- `tools/llamadas_diretas.py` — chamadas diretas vs hooks
- `config/gh3_functions.toml` — flags de codegen e funções registradas à mão
- `generated/default/gh3_recomp.119.cpp` — exemplo de C++ literal gerado
- `tests/test_guest_waits.py` — teste do contrato e dos endereços hookados
- `src/gh3_app.h` — seleção do renderer nativo e `OnPostSetup`
