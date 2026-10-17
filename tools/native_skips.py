"""Draws pulados pelo renderer nativo, por motivo, a partir do log.

O renderer escreve a cada 10 s "[native] skipped by reason: R:N R:N ..." com o que pulou no
intervalo (os motivos estão em Renderer::Impl::Draw, src/native/native_renderer.cpp; 5 = sem
geometry shader). Soma as linhas da corrida inteira e sai 1 se o motivo pedido passar de --max,
se o log não existir ou se não tiver nenhuma dessas linhas (aí não dá para afirmar nada).

  python3 tools/native_skips.py out/boot-test.log --reason 5 --max 0
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter

LINE = re.compile(r"\[native\] skipped by reason:(.*)$")
PAIR = re.compile(r"(\d+):(\d+)")


def totals(text: str) -> Counter | None:
    found = False
    total: Counter = Counter()
    for line in text.splitlines():
        m = LINE.search(line)
        if not m:
            continue
        found = True
        for reason, count in PAIR.findall(m.group(1)):
            total[int(reason)] += int(count)
    return total if found else None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log")
    ap.add_argument("--reason", type=int, required=True)
    ap.add_argument("--max", type=int, default=0)
    a = ap.parse_args(argv)
    try:
        text = open(a.log, errors="replace").read()
    except OSError as e:
        print(f"FALHA: {e}", file=sys.stderr)
        return 1
    total = totals(text)
    if total is None:
        print(f"FALHA: nenhuma linha '[native] skipped by reason' em {a.log}", file=sys.stderr)
        return 1
    summary = " ".join(f"{r}:{n}" for r, n in sorted(total.items())) or "nenhum"
    n = total[a.reason]
    if n > a.max:
        print(f"FALHA: motivo {a.reason} pulou {n} draws (máx {a.max}); todos: {summary}")
        return 1
    print(f"OK: motivo {a.reason} pulou {n} draws (máx {a.max}); todos: {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
