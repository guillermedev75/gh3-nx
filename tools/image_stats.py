"""Estatísticas de um quadro capturado (PNG): fração de pixels quase brancos e desvio padrão.

"Quase branco" = os 3 canais ≥ 245. O desvio padrão é o da luminância (0,299 R + 0,587 G + 0,114 B) em
todos os pixels. Serve para pegar o palco 3D que sai branco (T9) ou um quadro chapado (tela preta, loading).

Só stdlib: o PNG é decodificado com zlib (8 bits, RGB ou RGBA, sem entrelaçamento; o src/parity_capture.h
escreve RGB com filtro None).

Com --compare REF.png, também a diferença média por canal (0–255) contra outro quadro do mesmo tamanho: o
nativo contra o emulado de referência (T9).

Uso:
  python3 tools/image_stats.py QUADRO.png [--max-white F] [--min-stddev S]
  python3 tools/image_stats.py QUADRO.png --compare REF.png [--max-mean-diff D]
Sai 0 dentro dos limites, 1 fora, 2 se um arquivo não for um PNG que ele lê ou os tamanhos diferirem.
"""
from __future__ import annotations

import argparse
import struct
import sys
import zlib

SIG = b"\x89PNG\r\n\x1a\n"
WHITE = 245


class PngError(Exception):
    pass


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(f, line, prev, bpp, y):
    n = len(line)
    if f == 0:
        return
    if f == 1:
        for i in range(bpp, n):
            line[i] = (line[i] + line[i - bpp]) & 0xFF
    elif f == 2:
        for i in range(n):
            line[i] = (line[i] + prev[i]) & 0xFF
    elif f == 3:
        for i in range(n):
            left = line[i - bpp] if i >= bpp else 0
            line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
    elif f == 4:
        for i in range(n):
            left = line[i - bpp] if i >= bpp else 0
            up_left = prev[i - bpp] if i >= bpp else 0
            line[i] = (line[i] + _paeth(left, prev[i], up_left)) & 0xFF
    else:
        raise PngError(f"filtro {f} inválido na linha {y}")


def read_png(path):
    """Devolve (largura, altura, bytes RGB)."""
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(SIG):
        raise PngError("não é PNG")
    pos, idat, ihdr = 8, [], None
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if not ihdr:
        raise PngError("sem IHDR")
    width, height, depth, ctype, _, _, interlace = ihdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise PngError(f"formato não suportado (bits={depth} tipo={ctype} entrelaçado={interlace})")
    bpp = 3 if ctype == 2 else 4
    try:
        raw = zlib.decompress(b"".join(idat))
    except zlib.error as e:
        raise PngError(f"IDAT inválido: {e}") from e
    stride = width * bpp
    if len(raw) < (stride + 1) * height:
        raise PngError("IDAT curto")
    out, prev = bytearray(), bytearray(stride)
    for y in range(height):
        base = y * (stride + 1)
        line = bytearray(raw[base + 1:base + 1 + stride])
        _unfilter(raw[base], line, prev, bpp, y)
        prev = line  # Up/Avg/Paeth da próxima linha usam esta já decodificada, com alfa
        if bpp == 3:
            out += line
        else:
            rgb = bytearray(line)
            del rgb[3::4]
            out += rgb
    return width, height, bytes(out)


def stats(rgb):
    n = len(rgb) // 3
    r, g, b = rgb[0::3], rgb[1::3], rgb[2::3]
    white = 0
    total = total_sq = 0.0
    for i in range(n):
        ri, gi, bi = r[i], g[i], b[i]
        if ri >= WHITE and gi >= WHITE and bi >= WHITE:
            white += 1
        lum = 0.299 * ri + 0.587 * gi + 0.114 * bi
        total += lum
        total_sq += lum * lum
    mean = total / n
    var = max(0.0, total_sq / n - mean * mean)
    return white / n, var ** 0.5, mean


def mean_diff(a, b):
    """Diferença absoluta média por canal entre dois RGB do mesmo tamanho."""
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("png")
    ap.add_argument("--max-white", type=float)
    ap.add_argument("--min-stddev", type=float)
    ap.add_argument("--compare", metavar="REF")
    ap.add_argument("--max-mean-diff", type=float)
    a = ap.parse_args(argv)
    try:
        w, h, rgb = read_png(a.png)
    except (OSError, PngError) as e:
        print(f"ERRO: {a.png}: {e}")
        return 2
    if not w or not h:
        print(f"ERRO: {a.png}: imagem vazia")
        return 2
    white, std, mean = stats(rgb)
    print(f"{a.png}: {w}x{h} white={white:.3f} stddev={std:.1f} mean={mean:.1f}")
    bad = []
    if a.max_white is not None and white > a.max_white:
        bad.append(f"white {white:.3f} > {a.max_white}")
    if a.min_stddev is not None and std < a.min_stddev:
        bad.append(f"stddev {std:.1f} < {a.min_stddev}")
    if a.compare:
        try:
            rw, rh, ref = read_png(a.compare)
        except (OSError, PngError) as e:
            print(f"ERRO: {a.compare}: {e}")
            return 2
        if (rw, rh) != (w, h):
            print(f"ERRO: {a.compare} é {rw}x{rh}, {a.png} é {w}x{h}")
            return 2
        diff = mean_diff(rgb, ref)
        print(f"{a.png} vs {a.compare}: mean_diff={diff:.1f}")
        if a.max_mean_diff is not None and diff > a.max_mean_diff:
            bad.append(f"mean_diff {diff:.1f} > {a.max_mean_diff}")
    if bad:
        print("FALHOU: " + "; ".join(bad))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
