"""Gera src/supported_dumps.h: a tabela que a trava do default.xex (src/dump_gate.h) aceita.

O cabeçalho leva só o SHA-256, o tamanho e o title id do executável do disco do Guillerme; hash não é
conteúdo do jogo. O código gerado pelo codegen vale para um único default.xex, então a trava não tem
flag para desligar: um hash desconhecido é recusa.

    python3 tools/gen_supported_dumps.py [--xex extracted/default.xex] [--out src/supported_dumps.h] [--check]

--check sai 1 se o cabeçalho não bate com o --xex (não escreve nada).
"""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tools.check_xex import identify, parse_xex  # noqa: E402

TEMPLATE = """\
// Gerado por tools/gen_supported_dumps.py a partir do default.xex do disco. Não editar à mão.
//
// Só o SHA-256, o tamanho e o title id do executável: nada do conteúdo do jogo. O código gerado
// (generated/) é a tradução de um único default.xex, então o hash é o que decide se os arquivos do
// jogador batem com o build. Não há override: hash desconhecido é recusa (src/dump_gate.h).

#pragma once

#include <cstdint>
#include <string_view>

namespace gh3::identify {{

struct SupportedDump {{
  std::string_view sha256;    // do default.xex, hex minúsculo
  std::string_view title_id;  // do cabeçalho XEX2
  std::string_view release;   // revisão (tools/check_xex.py), mostrada ao jogador
  uint64_t xex_size;          // bytes; só muda a mensagem ("outra revisão")
}};

inline constexpr SupportedDump kSupportedDumps[] = {{
    {{"{sha256}", "0x{title_id:08X}", "Guitar Hero III: {release}", {size}}},
}};

}}  // namespace gh3::identify
"""


def render(xex: Path) -> str:
    data = xex.read_bytes()
    info = parse_xex(data[:0x10000])
    release = identify(info)
    if not release:
        raise ValueError(f"{xex}: não é uma revisão suportada do GH3 (tools/check_xex.py)")
    return TEMPLATE.format(sha256=hashlib.sha256(data).hexdigest(), title_id=info["title_id"],
                           release=release, size=len(data))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--xex", type=Path, default=Path("extracted/default.xex"))
    ap.add_argument("--out", type=Path, default=Path("src/supported_dumps.h"))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)
    try:
        text = render(args.xex)
    except (OSError, ValueError) as e:
        print(e, file=sys.stderr)
        return 1
    if args.check:
        if not args.out.exists() or args.out.read_text() != text:
            print(f"{args.out} desatualizado: rode tools/gen_supported_dumps.py", file=sys.stderr)
            return 1
        print(f"{args.out}: OK")
        return 0
    args.out.write_text(text)
    print(f"{args.out}: escrito")
    return 0


if __name__ == "__main__":
    sys.exit(main())
