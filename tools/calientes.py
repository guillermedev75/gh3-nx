"""Lista as funções mais quentes do perfil de pilha (a entrada do `switch/orden_funciones.ld`).

Uso: python3 tools/calientes.py <rex_perfil.log> <ELF|nm.txt> [saida.ld]

O `rex_perfil.log` é escrito pelo profiler de amostragem do app (src/switch_perf.cpp, só no Switch):
cada amostra traz os endereços que viu (pc, lr e pilha) como `imagen+0x…`. Cada endereço é resolvido
para o símbolo que o contém com `nm` do ELF do MESMO build (o NRO é stripado; um ELF de outro build
dá nomes errados — playbook §8). A saída é o script que o linker do NRO usa via
`-Wl,--section-ordering-file` (formato do nfsmw-nx: um bloco `.text : { … }`, com o `KEEP (*(.crt0))`
primeiro para o `_start` continuar no endereço 0).

O ELF pode ser o binário (`nm` roda por baixo) ou um arquivo de texto com a saída de `nm`
(útil sem o devkitA64 e nos testes).
"""
import bisect
import collections
import os
import re
import shutil
import subprocess
import sys

ADDR = re.compile(r"imagen\+0x([0-9a-f]+)")
AMOSTRAS = re.compile(r"^\s+(\d+) muestras")
QUENTE = re.compile(r"^\s+([\d.]+)%\s+(?:pc|lr|pila) (.*)")
NM_LINHA = re.compile(r"^\s*([0-9a-fA-F]+)\s+\S+\s+(\S+)\s*$")


def parse_profile(texto):
    """Endereço (int) → peso em amostras, a partir das linhas `… % pc|lr|pila imagen+0x…`."""
    peso = collections.Counter()
    ns = 0
    for linha in texto.splitlines():
        m = AMOSTRAS.match(linha)
        if m:
            ns = int(m.group(1))
            continue
        m = QUENTE.match(linha)
        if not m:
            continue
        enderecos = set(int(a, 16) for a in ADDR.findall(m.group(2)))
        if not enderecos:
            continue
        # Um endereço por linha vale o peso da linha; a linha com vários (lr/pilha) divide.
        por_endereco = float(m.group(1)) * ns / 100.0 / len(enderecos)
        for a in enderecos:
            peso[a] += por_endereco
    return peso


def parse_symbols(texto):
    """Saída de `nm` → lista ordenada [(endereço, nome)]."""
    simbolos = []
    for linha in texto.splitlines():
        m = NM_LINHA.match(linha)
        if not m:
            continue
        nome = m.group(2)
        if nome in ("??", ""):
            continue
        simbolos.append((int(m.group(1), 16), nome))
    simbolos.sort()
    return simbolos


def resolve(peso, simbolos):
    """Endereços ponderados → nomes de função, do mais quente para o mais frio."""
    if not simbolos:
        return []
    enderecos = [a for a, _ in simbolos]
    por_nome = collections.Counter()
    for endereco, w in peso.items():
        i = bisect.bisect_right(enderecos, endereco) - 1
        if i < 0:
            continue
        nome = simbolos[i][1]
        # `sub_X` e `__imp__sub_X` são o mesmo corpo (alias): normaliza para não inflar o peso.
        if nome.startswith("__imp__"):
            nome = nome[len("__imp__"):]
        por_nome[nome] += w
    nomes = [n for n in por_nome if por_nome[n] > 0]
    return sorted(nomes, key=lambda n: (-por_nome[n], n))


def format_ordering(nomes, fonte="perfil de pilha do Switch (rex_perfil.log)"):
    """Script do `--section-ordering-file` com o crt0 primeiro e as quentes na ordem."""
    saida = [
        "/*",
        f" * Funções mais quentes agrupadas no começo do .text ({len(nomes)}), da mais quente",
        f" * para a mais fria. Gerado por tools/calientes.py a partir de {fonte}.",
        " */",
        ".text : {",
        "  /*",
        "   * O crt0 do libnx fica no endereço 0 do .text: se sair daqui o _start muda de lugar e o",
        "   * NRO não inicia (nm build/switch/gh3 | grep ' _start$' tem que dar 0).",
        "   */",
        "  KEEP (*(.crt0))",
    ]
    for nome in nomes:
        saida.append(f"  *(.text.{nome})")
    saida.append("}")
    return "\n".join(saida) + "\n"


def symbols_from(path):
    """Aceita um arquivo texto com saída de `nm` ou um ELF (roda `nm`)."""
    if path.endswith((".txt", ".nm")):
        with open(path, encoding="utf-8", errors="ignore") as f:
            return parse_symbols(f.read())
    nm = shutil.which("nm") or shutil.which("aarch64-none-elf-nm") or shutil.which("llvm-nm")
    if nm is None:
        raise SystemExit("nm não encontrado; passe um arquivo .txt com a saída de nm")
    out = subprocess.run([nm, path], capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit(f"nm falhou em {path}: {out.stderr.strip()}")
    return parse_symbols(out.stdout)


def main(argv):
    if len(argv) != 4:
        raise SystemExit(__doc__)
    with open(argv[1], encoding="utf-8", errors="ignore") as f:
        peso = parse_profile(f.read())
    nomes = resolve(peso, symbols_from(argv[2]))
    if not nomes:
        raise SystemExit("nenhuma função resolvida: perfil e ELF são do mesmo build?")
    with open(argv[3], "w", encoding="utf-8") as f:
        f.write(format_ordering(nomes, os.path.basename(argv[1])))
    quentes = [n for n in nomes if "sub_8" in n]
    print(f"calientes: {len(nomes)} funções ({len(quentes)} do jogo); top: {nomes[:5]}")


if __name__ == "__main__":
    main(sys.argv)
