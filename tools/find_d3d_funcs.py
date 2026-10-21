#!/usr/bin/env python3
"""Acha a biblioteca Direct3D do XDK dentro do código traduzido do GH3 (T52, viabilidade do reNut/deko3d).

O reNut (masterspike52) intercepta ~355 funções D3D do Banjo N&B por endereço fixo (config/renut_gpu_funcs.toml).
Para o GH3 seria preciso achar as mesmas. Sem um jogo de referência com as funções nomeadas (a imagem do Banjo,
que o tools/d3d_match.py do peggle1-nx usa), dá para ancorar algumas pelas importações do kernel que só a D3D
chama (VdSwap = o Swap, VdInitializeRingBuffer = a criação do device...) e delimitar a faixa da biblioteca.

  python3 tools/find_d3d_funcs.py --gen generated/default --names out/refs/reNut/config/renut_gpu_funcs.toml \
      --out out/research/d3d_funcs.json

Saída JSON: renut_hooks (nomes do reNut), found (nome -> endereço, só os ancorados), missing, anchors
(importação -> funções que a chamam) e d3d_range (menor e maior endereço ancorado).
"""
import argparse
import glob
import json
import os
import re
import sys

# Importação do kernel -> função D3D que, no XDK, é a única a chamá-la (o nome do reNut correspondente).
ANCHORS = {
    "VdSwap": "D3DDevice_Swap",
    "VdInitializeRingBuffer": "Direct3D_CreateDevice",
    "VdRetrainEDRAM": None,
    "VdSetDisplayMode": None,
    "VdSetGraphicsInterruptCallback": None,
    "VdShutdownEngines": None,
    "VdPersistDisplay": None,
}
FUNC = re.compile(r"^DEFINE_REX_FUNC\((sub_[0-9A-F]{8})\)", re.M)


def read_names(path):
    """Nomes do toml do reNut: linhas `0x8264DF28 = { name = "Direct3D_CreateDevice" }`."""
    if not path or not os.path.exists(path):
        return []
    return sorted(set(re.findall(r'name\s*=\s*"([^"]+)"', open(path, errors="replace").read())))


def scan(gen):
    """importação -> lista ordenada de funções que a chamam."""
    calls = {imp: set() for imp in ANCHORS}
    for path in glob.glob(os.path.join(gen, "*_recomp.*.cpp")):
        text = open(path, errors="replace").read()
        heads = list(FUNC.finditer(text))
        for i, m in enumerate(heads):
            body = text[m.end(): heads[i + 1].start() if i + 1 < len(heads) else len(text)]
            for imp in ANCHORS:
                if "__imp__" + imp in body:
                    calls[imp].add(m.group(1))
    return {imp: sorted(v) for imp, v in calls.items()}


def build(gen, names):
    anchors = scan(gen)
    found = {}
    for imp, name in ANCHORS.items():
        funcs = anchors[imp]
        if name and len(funcs) == 1:  # ambíguo não vale como achado
            found[name] = "0x" + funcs[0][4:]
    addrs = [int(f[4:], 16) for fs in anchors.values() for f in fs]
    rng = ["0x%08X" % min(addrs), "0x%08X" % max(addrs)] if addrs else None
    return {
        "renut_hooks": names,
        "found": found,
        "missing": [n for n in names if n not in found],
        "anchors": anchors,
        "d3d_range": rng,
    }


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", default="generated/default")
    ap.add_argument("--names", default="out/refs/reNut/config/renut_gpu_funcs.toml")
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    if not glob.glob(os.path.join(a.gen, "*_recomp.*.cpp")):
        print(f"sem código traduzido em {a.gen}", file=sys.stderr)
        return 1
    d = build(a.gen, read_names(a.names))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    json.dump(d, open(a.out, "w"), indent=1)
    print(f"{len(d['found'])} de {len(d['renut_hooks'])} funções do reNut ancoradas; faixa D3D {d['d3d_range']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
