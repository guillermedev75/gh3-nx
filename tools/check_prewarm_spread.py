#!/usr/bin/env python3
"""Confere que o prewarm de pipelines incremental não segurou o jogo (T55).

Lê o game.log de uma captura e exige três coisas: pelo menos duas fatias
("pipeline prewarm: k/N incremental"), a linha final
("pipeline prewarm: done in T ms over F frames") com F >= 2, e pelo menos um
XELOG_GPU PRESENT antes dela — isto é, o jogo apareceu antes de o prewarm
terminar. Sai 0 se tudo bate, 1 se não.
"""
from __future__ import annotations  # o aceite roda com o python3 do sistema (3.9)

import pathlib
import re
import sys

SLICE = re.compile(r"pipeline prewarm: (\d+)/(\d+) incremental")
DONE = re.compile(r"pipeline prewarm: done in (\d+) ms over (\d+) frames")
PRESENT = "XELOG_GPU PRESENT"


def check(text: str) -> str | None:
    slices = 0
    last = (0, 0)
    presents = 0
    done = None
    for line in text.splitlines():
        if SLICE.search(line):
            slices += 1
            last = tuple(int(v) for v in SLICE.search(line).groups())
            continue
        found = DONE.search(line)
        if found and done is None:
            done = (int(found.group(1)), int(found.group(2)), presents)
            continue
        if PRESENT in line:
            presents += 1
    if slices < 2:
        return f"{slices} fatia(s) incremental, esperado >= 2"
    if done is None:
        return "sem a linha 'pipeline prewarm: done in ... over ... frames'"
    ms, frames, presents_before = done
    if frames < 2:
        return f"prewarm terminou em {frames} quadro(s), esperado >= 2"
    if last[1] and last[0] != last[1]:
        return f"última fatia {last[0]}/{last[1]}, o arquivo tinha {last[1]}"
    if presents_before < 1:
        return "nenhum XELOG_GPU PRESENT antes do prewarm terminar (o jogo não apareceu)"
    return None


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"uso: {argv[0]} CAMINHO/game.log", file=sys.stderr)
        return 2
    path = pathlib.Path(argv[1])
    if not path.is_file():
        print(f"não achei {path}", file=sys.stderr)
        return 2
    problem = check(path.read_text(errors="replace"))
    if problem:
        print(f"FALHOU: {problem}", file=sys.stderr)
        return 1
    print("ok: prewarm incremental espalhado, jogo apareceu antes do fim")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
