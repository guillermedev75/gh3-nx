# gh3-nx

Port não oficial do Guitar Hero III: Legends of Rock (Xbox 360, USA Rev 1) para o Nintendo Switch
desbloqueado, por recompilação estática com [ReXGlue](https://github.com/rexglue/rexglue-sdk).

**Nenhum arquivo do jogo está neste repositório.** Você precisa do seu próprio disco: copie-o para
`extracted/` (ignorado pelo git). O código C++ gerado a partir do executável do jogo vai para
`generated/`, também ignorado.

Design: `docs/superpowers/specs/2026-10-17-gh3-nx-design.md` · Plano do marco A:
`docs/superpowers/plans/2026-10-17-gh3-nx-marco-a-mac.md`

## Preparar

```bash
bash tools/fetch_rexglue.sh                         # ReXGlue SDK 0.10.0 (macOS arm64) em tools/rexglue
python3 -m venv .venv && .venv/bin/pip install pytest
.venv/bin/python -m pytest -q tests
```

## Créditos

- [ReXGlue](https://github.com/rexglue/rexglue-sdk) e [Xenia](https://github.com/xenia-project/xenia)
- [project8-sw](https://github.com/R-drg/project8-sw) (R-drg) e [nfsmw-nx](https://github.com/StevensND/nfsmw-nx)
  (StevensND): referência de renderer nativo e camada Horizon (GPL-3.0)
- [Guitar-Hero-3-Recomp-](https://github.com/luisxl15/Guitar-Hero-3-Recomp-) (luisxl15): referência dos dados
  do executável (sem código copiado)

Licença: GPL-3.0.
