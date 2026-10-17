#!/usr/bin/env python3
"""Valida um NRO do Switch: magia NRO0 em 0x10, tamanho em 0x18 igual ao do arquivo.

Depois do tamanho declarado só pode vir a asset section (ícone, NACP, RomFS), que começa
com a magia ASET; o elf2nro a anexa quando recebe --icon/--nacp. Sai 0 se válido, 1 se não.
"""
from __future__ import annotations  # o aceite roda com o python3 do sistema (3.9)

import struct
import sys

HEADER = 0x80


def check(data: bytes) -> str | None:
    if len(data) < HEADER:
        return f"arquivo de {len(data)} bytes, menor que o cabeçalho"
    if data[0x10:0x14] != b"NRO0":
        return f"magia {data[0x10:0x14]!r} em 0x10, esperado b'NRO0'"
    (size,) = struct.unpack_from("<I", data, 0x18)
    if size < HEADER or size > len(data):
        return f"tamanho declarado 0x{size:x}, arquivo tem 0x{len(data):x}"
    tail = data[size:]
    if tail and tail[:4] != b"ASET":
        return f"0x{len(tail):x} bytes depois do NRO sem asset section (ASET)"
    return None


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("uso: check_nro.py ARQUIVO.nro", file=sys.stderr)
        return 1
    try:
        with open(argv[1], "rb") as f:
            data = f.read()
    except OSError as e:
        print(f"NRO inválido: {e}", file=sys.stderr)
        return 1
    err = check(data)
    if err:
        print(f"NRO inválido: {err}", file=sys.stderr)
        return 1
    (size,) = struct.unpack_from("<I", data, 0x18)
    assets = "com" if len(data) > size else "sem"
    print(f"NRO OK: 0x{size:x} bytes, {assets} asset section")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
