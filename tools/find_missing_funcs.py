"""Acha funções que a análise estática do ReXGlue não registrou.

As quedas "Call to invalid or unregistered function" do GH3 vêm de funções minúsculas (2 a 18 instruções)
que ficam no buraco entre duas funções registradas: ninguém as chama diretamente, só por ponteiro
(vtable, tabela de callbacks). Este script:

1. mede o tamanho de cada função gerada (uma linha "// <instrução>" por instrução no C++ gerado);
2. lista os buracos entre o fim de uma função e o início da próxima;
3. descarta buraco que é só preenchimento (palavras 0x00000000 ou nop 0x60000000);
4. marca como candidato o início de cada buraco com código, e diz se algum ponteiro na imagem aponta para ele.

Uso:
  GH3_DUMP_IMAGE=out/image.bin ./run-gh3.command   # grava a imagem carregada (ver src/gh3_app.h)
  python3 tools/find_missing_funcs.py out/image.bin [--toml [--all]]   # --toml imprime linhas novas para gh3_functions.toml
  cmake --build ... > out/build.log; python3 tools/find_missing_funcs.py --prune out/build.log   # tira falsos positivos
"""
import glob
import re
import struct
import sys

IMAGE_BASE = 0x82000000
PADDING = {0x00000000, 0x60000000}

FUNC_RE = re.compile(r"(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?sub_([0-9A-F]{8})\)")
INSN_RE = re.compile(r"\t// [a-z]")


def function_sizes(cpp_texts) -> dict:
    """{endereço: tamanho em bytes} a partir do C++ gerado (4 bytes por instrução comentada)."""
    sizes, cur = {}, None
    for text in cpp_texts:
        for line in text.splitlines():
            m = FUNC_RE.match(line)
            if m:
                cur = int(m.group(1), 16)
                sizes.setdefault(cur, 0)
            elif cur is not None and INSN_RE.match(line):
                sizes[cur] += 4
    return sizes


def word(image: bytes, addr: int) -> int:
    return struct.unpack_from(">I", image, addr - IMAGE_BASE)[0]


def find_candidates(image: bytes, sizes: dict, code_end: int) -> list:
    """[(endereço, tamanho_do_buraco, referenciado_por_ponteiro)] dos buracos com código de verdade."""
    starts = sorted(sizes)
    pointers = set()
    for off in range(0, len(image) - 3, 4):
        v = struct.unpack_from(">I", image, off)[0]
        if starts and starts[0] <= v < code_end and v % 4 == 0:
            pointers.add(v)
    out = []
    for a, b in zip(starts, starts[1:] + [code_end]):
        pos = a + sizes[a]
        while pos < b and word(image, pos) in PADDING:
            pos += 4
        if pos < b:
            out.append((pos, b - pos, pos in pointers))
    return out


def split_candidates(build_log: str, candidates) -> set:
    """Candidatos que partiram uma função existente: ficam entre a origem e o destino de um
    "Unresolved conditional branch" que o codegen passou a acusar. Meu tamanho de função subestima corpos com
    código depois de um retorno; esses falsos positivos aparecem assim e devem sair do config."""
    bad = set()
    for t, f in re.findall(r"Unresolved conditional branch to 0x([0-9A-F]+) from 0x([0-9A-F]+)", build_log):
        lo, hi = sorted((int(t, 16), int(f, 16)))
        bad |= {c for c in candidates if lo < c <= hi}
    # Candidato que o codegen não consegue gerar (ex.: dados no fim da seção de código) vira símbolo indefinido no link.
    bad |= {int(a, 16) for a in re.findall(r'"_sub_([0-9A-F]{8})", referenced from', build_log)} & set(candidates)
    return bad


def main(argv):
    if "--prune" in argv:  # --prune out/build.log: tira do config os candidatos que partiram funções
        cfg = "config/gh3_functions.toml"
        lines = open(cfg).read().splitlines(keepends=True)
        cands = {int(m.group(1), 16): i for i, l in enumerate(lines)
                 if (m := re.match(r"0x([0-9A-F]{8}) = .*find_missing_funcs", l))}
        bad = split_candidates(open(argv[argv.index("--prune") + 1]).read(), cands)
        open(cfg, "w").write("".join(l for i, l in enumerate(lines) if i not in {cands[b] for b in bad}))
        print(f"removidos {len(bad)} candidatos que partiam funções: {sorted(hex(b) for b in bad)}")
        return
    image = open(argv[1], "rb").read()
    texts = [open(f).read() for f in glob.glob("generated/default/gh3_recomp.*.cpp")]
    sizes = function_sizes(texts)
    pch = open("generated/default/gh3_pch.h").read()
    base = int(re.search(r"REX_CODE_BASE (0x[0-9A-F]+)", pch).group(1), 16)
    size = int(re.search(r"REX_CODE_SIZE (0x[0-9A-F]+)", pch).group(1), 16)
    cands = find_candidates(image, sizes, base + size)
    if "--toml" in argv:
        # --all inclui buracos sem ponteiro na imagem: o jogo pode montar o endereço em runtime (lis/addi),
        # como 0x825E9670 (carregamento de música). Falsos positivos que partem funções saem com --prune.
        registered = set(re.findall(r"^0x([0-9A-F]{8}) =", open("config/gh3_functions.toml").read(), re.M))
        for addr, gap, ref in cands:
            if (ref or "--all" in argv) and f"{addr:08X}" not in registered:
                how = "referenciado por ponteiro" if ref else "sem ponteiro na imagem"
                print(f'0x{addr:08X} = {{ name = "sub_{addr:08X}" }}  # find_missing_funcs.py: buraco de {gap} bytes, {how}')
    else:
        print(f"{len(sizes)} funções; {len(cands)} buracos com código; {sum(r for *_, r in cands)} referenciados por ponteiro")


if __name__ == "__main__":
    main(sys.argv)
