# 2026-10-23 — Rastreamento de texturas (`gh3_native_texture_trace`)

## Objetivo
A plateia do GH3 aparece como ruído/confete com estrias verticais (prints de 11:36) e o teto com
rabiscos brancos, mas o Mac não reproduz (a captura `--mark` cai noutro cenário) e não havia
instrumento para ver qual textura cada draw liga. Sem isso, a hipótese (tiling/mip/formato errado,
superfície de resolve amostrada noutro tamanho, textura streamada) não é testável.

## O que foi feito
- `src/native/native_texture_log.h` (puro, sem SDK): `Entry` + `Format` para uma linha `tex` por textura
  ligada a um draw. Campos: quadro, índice do draw, estágio (vs/ps), fetch constant, `addr` (base << 12),
  formato/dimensão do guest, `bind` (FetchOpDimension) e `signed`, tamanho, pitch, `tiled`, `packed_mips`,
  faixa de mips, `VkImageView`/`VkImage`, `src=texture|resolve(WxH[ sized])`, scissor e a passada.
- `TextureBindings::view_images[24]` guarda o `VkImage` de cada view no bloco de descritores (reaproveitado
  no `tb_hit`); `TextureCache::TextureImage(void*)` expõe a imagem de uma textura normal; destinos de
  resolve são resolvidos por endereço (`FindResolveDestination`).
- Log antes de `SubmitDraw(d)`, onde a fetch constant do draw está viva (na thread de gravação não está).
- `pass_profile_key` guarda a chave da passada corrente (setada em `BeginPass`, limpa em `EndPass`) para o
  log dizer em que passada o draw caiu.
- Cvars (padrão desligado): `gh3_native_texture_trace=N` (quadros `[N, N+2)`),
  `gh3_native_texture_trace_every=N` (2 quadros a cada N — para pegar cena sem adivinhar o quadro) e
  `gh3_native_texture_trace_addr=0x...` (filtro por endereço).
- `tests/test_texture_log.py`: 4 testes de formatação + 1 de fiação (cvars, janela, filtro, include).

## Verificação (medida)
- `pytest -q tests`: **331 passed** (326 + 5 novos).
- Mac, `--gh3_renderer=native --gh3_native_texture_trace=250`: **8 linhas**, frames 250 e 251, draws 1 e 2.
- A mesma fetch constant aparece duas vezes (`signed=0`/`signed=1`): é a tradução criando duas bindings
  para a mesma textura, não log duplicado — o campo `bind`/`signed` torna isso visível.
- `--gh3_native_texture_trace_every=80` sem janela absoluta: frames **80/81, 160/161, 240/241, 320/321**.
- Custo com a cvar em 0: dois `REXCVAR_GET` por draw (sem alocação, sem `vkGetImageMemoryRequirements` etc.).

## NRO
`build/switch/gh3.nro` = `0x3d98000` (64.633.926 B), sha256
`289757d77745f1c76efd23272401f470e3932f29fd41aabdafd9aae6752f450f`, copiado para
`out/sd/switch/gh3-nx/gh3-nx.nro` (mesmo hash). FTP do Switch recusou conexão nesta sessão
(`192.168.3.105:5000`), então o envio ficou pendente.

## Como usar no Switch
No `gh3.toml` ao lado do NRO:
```toml
log_level = "info"
gh3_native_texture_trace_every = 600   # 2 quadros a cada ~20 s no portátil (30 FPS)
# gh3_native_texture_trace_addr = 0x0FEDA000   # opcional, depois de achar o endereço
```
Jogar até a plateia aparecer, tirar print e anotar a hora; o log em `logs/` tem timestamp por linha, então
o intervalo do print casa com os blocos `tex f=...`. Para um quadro exato, trocar por
`gh3_native_texture_trace=<frame>` (lido do perfil).

## Pendente
- Rodar no Switch e comparar a textura da plateia: `tiled`/`pitch` coerentes com o formato? `src=resolve`
  com `sized`? mips 0..0 onde deveria haver mip? É o que fecha a causa.
- Depois disso, os cortes de GPU levantados pela radiografia (§13): os blits de profundidade do shadow map
  copiam 2048 linhas onde só 1024/512 são usadas (~0,5–0,9 ms), e o resolve de `0x0BEEC000` não tem
  consumidor provado (~0,4–0,7 ms) — ambos presos a validação no Switch.
