---
created: 2026-10-21
updated: 2026-10-21
---

# reNut + plume/deko3d para o GH3: viabilidade (T52)

Pergunta: trocar o renderer atual (emula a GPU do 360: PM4, EDRAM, resolves; Vulkan no NVK) pelo caminho
**D3D-level** do reNut, que intercepta as chamadas Direct3D do próprio jogo e desenha na **deko3d**, a API nativa do
Switch. Motivo: no Switch o GH3 é limitado pela GPU (playbook §8) e os nossos custos e bugs estão justamente na
emulação (EDRAM, resolves, alias do T15, escala da cena — playbook §9).

Fontes lidas (só leitura, em `out/refs/`, fora do git): `masterspike52/reNut`, `ChanseyIsTheBest/peggle1-nx`
(que carrega o renderer do reNut "como o banjo-nx o leva" e documenta o método em
`docs/porting-a-360-game-prompt.md`).

## Como o reNut funciona

- Ganchos `REX_HOOK(Nome, Nome_hook)` do ReXGlue em **359 funções** (354 D3D/XG + 1 outra), com o endereço de cada
  uma fixo para o Banjo N&B em `config/renut_gpu_funcs.toml` (`0x8264E8B0 = { name = "D3DDevice_CreateVertexShader" }`).
  Os ganchos leem as estruturas do D3D na memória do guest (device, recursos, alocador de EDRAM/HiZ), também com
  endereços e offsets do Banjo (`kFormatTable = 0x820052F8`, `kHiZOwnerVa = 0x82FA36C8`...).
- Shaders: **recompilados no build** com XenosRecomp a partir dos contêineres do jogo (sem tradução em tempo de
  execução). No Switch: SPIR-V → SPIRV-Cross → GLSL do deko3d, compilado no console com `uam` e guardado no SD.
- Desenha pela **plume** (camada de render; back end deko3d escrito pelo banjo-nx).
- Para achar as funções D3D de outro jogo, a linhagem usa `tools/d3d_match.py` (peggle1-nx, GPL-3.0): compara a
  *forma* das funções (opcodes e registradores, sem imediatos) contra um jogo cujas funções já têm nome — a imagem
  decriptada do Banjo + a tabela do reNut. Funciona entre versões do XDK porque o código é o mesmo e só os
  offsets mudam; depois confere pelo alinhamento das chamadas.

## O que achamos no GH3

- XDK do GH3 (cabeçalho do XEX): **2.0.6132** em todas as bibliotecas (D3D9, D3DX9, XGRAPHC, XAPILIB...). A versão
  do Banjo N&B não foi confirmada (é de 2008, provavelmente mais nova): os offsets das estruturas do D3D mudam, e
  os ganchos do reNut que leem o device precisam ser ajustados (o próprio guia avisa: "the device's bound VS/PS
  offsets move between XDK versions").
- A biblioteca D3D do GH3 está na faixa **0x824B2A70–0x824D95E0** (`tools/find_d3d_funcs.py`, ancorada pelas
  importações do kernel que só a D3D chama; ~1.540 funções em 0x824B0000–0x824E0000, D3D e outras bibliotecas do
  XDK misturadas). Nomeadas com certeza: `D3DDevice_Swap = 0x824B2A70` (chama `VdSwap`/`VdPersistDisplay`) e
  `Direct3D_CreateDevice = 0x824D0E20` (`VdInitializeRingBuffer`, `VdEnableRingBufferRPtrWriteBack`).
- A função mais quente do jogo no Switch, `sub_824D0070` (T18), fica **dentro da D3D**: é a espera por espaço no
  ring de comandos da GPU — o mesmo diagnóstico do playbook §8 (GPU-bound), visto de dentro do jogo.

## O que exigiria

1. **Nomear ~357 funções D3D do GH3.** Caminho viável: `d3d_match.py` com a imagem decriptada do **Banjo N&B
   (USA)** como referência — precisa do disco do próprio usuário (nunca no repositório). Sem a referência, nomear
   à mão com decompilador: semanas.
2. **Permissão do masterspike52.** O reNut **não tem licença** (todos os direitos reservados); o peggle1-nx o usa
   com "reuse permitted by its author". Antes de copiar qualquer linha, pedir a mesma permissão e registrar no
   NOTICE. plume (renderbag), uam (zlib/MIT), deko3d (zlib), XenosRecomp, SPIRV-Cross (Apache-2.0): ok com GPL.
3. **Biblioteca de shaders do GH3**: dumpar os contêineres de shader dos dados do jogo (pak), rodar o laço de
   descoberta até "0 unidentified loads", compilar com XenosRecomp. Os shaders são dados do jogo: gerados na
   máquina do usuário, fora do git.
4. **Ajustar os ganchos** às estruturas do XDK 6132 e trocar os endereços do Banjo pelos do GH3 (alocador de EDRAM,
   tabela de formatos, HiZ).
5. **Validar quadro a quadro** contra o renderer atual (o guia exige "no visible difference across boot, menus,
   gameplay, movies") e só então medir no Switch.

## O que se ganha e o que se arrisca

- Ganha: some a emulação de PM4/EDRAM/resolves (onde estão nossos custos e os bugs dos T15/T16/T47); deko3d tem
  menos overhead de driver que o NVK; shaders já compilados (sem stutter de tradução).
- Evidência de desempenho é **fraca para um jogo 3D pesado**: peggle1-nx faz 60 FPS, mas é 2D leve; nfscarbon-nx e
  fear-nx (NVK, nossa pilha) ficam em ~30. (Os números do Mark são da nossa pilha, NVK, não do deko3d.) Nenhum jogo 3D pesado medido a 60 em deko3d.
- Arrisca: semanas de trabalho, regressões visuais, dependência de código de terceiros sem licença.

## Veredito

**Condicional — tecnicamente viável, mas não começar agora.** A arquitetura casa (XDK D3D linkado
estaticamente, mesma ferramenta de codegen, a biblioteca D3D do GH3 já localizada), e é o único caminho que ataca
a raiz dos nossos custos de GPU. Três condições antes de investir:

1. ~~Números do Mark~~ — **descartado (2026-10-21)**: o Mark esclareceu que usa a mesma pilha que nós (método do
   nfsmw-nx: NVK + ReXGlue), não reNut/deko3d. Os "~30 FPS, CPU-bound, GPU ~40%" dele não dizem nada sobre o
   deko3d. Continua faltando evidência de um jogo 3D pesado a 60 FPS nessa pilha.
2. **Referência para nomear as funções**: o disco do Banjo N&B (ou outro jogo com as funções nomeadas no XDK
   próximo do 6132).
3. **Permissão do masterspike52** para reutilizar o reNut.

Com as três, o próximo passo é uma tarefa só de nomeação (`d3d_match.py` no GH3, meta: os ~20 ganchos mínimos que o
guia lista — carga de shader no draw, DrawVertices/DrawIndexedVertices/DrawVerticesUP, Create*Shader, flip) antes
de qualquer código de renderer. Até lá, as otimizações no renderer atual (T48–T54) seguem valendo.
