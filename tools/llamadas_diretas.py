"""Reescreve as chamadas `bl` do código gerado para irem direto ao corpo da função.

O codegen emite `sub_X(ctx, base);` para cada `bl`. `sub_X` é um símbolo fraco (alias no GCC, trampolim
musttail no clang) que só existe para o runtime/ganchos substituírem a função; o compilador não enxerga o
corpo através dele, então não inlina nem faz LTO entre funções traduzidas. Trocar por `__imp__sub_X` (o
corpo real, já declarado por DECLARE_REX_FUNC) corta o salto extra e libera inline/LTO.

Funções que têm gancho em `src/` precisam ficar FORA da reescrita: `__imp__` chama o corpo original direto
e não passa pelo símbolo forte do gancho. A lista fica em HOOKED (as mesmas de src/guest_waits.h); para
elas o script faz o contrário — `__imp__sub_X` volta a ser `sub_X`. Assim ele é idempotente e conserta um
gerado que já tinha sido reescrito antes do gancho existir.

Chamadas indiretas (REX_CALL_INDIRECT_FUNC) não são tocadas.

Uso: python3 tools/llamadas_diretas.py generated/default/gh3_recomp.*.cpp
"""
import re
import sys

# Endereços com REX_HOOK_RAW em src/guest_waits.h. test_guest_waits.py confere que esta lista e os
# kSwapCounter/kVblankCounter do header são os mesmos.
HOOKED = {0x82172718, 0x82172758}

CALL = re.compile(r"^(\s+)(sub_[0-9A-Fa-f]+)\(ctx, base\);", re.M)
IMP = re.compile(r"^(\s+)__imp__(sub_[0-9A-Fa-f]+)\(ctx, base\);", re.M)


def _hooked(name: str) -> bool:
    return int(name[4:], 16) in HOOKED


def _target(name: str) -> str:
    return name if _hooked(name) else "__imp__" + name


def rewrite(text: str) -> str:
    def call(m):
        return m.group(1) + _target(m.group(2)) + "(ctx, base);"

    def imp(m):
        return m.group(1) + _target(m.group(2)) + "(ctx, base);"

    return IMP.sub(imp, CALL.sub(call, text))


if __name__ == "__main__":
    total = 0
    for path in sys.argv[1:]:
        with open(path) as f:
            original = f.read()
        result = rewrite(original)
        if result != original:
            total += 1
            with open(path, "w") as f:
                f.write(result)
    print(f"llamadas_diretas: {total} arquivos reescritos em {len(sys.argv) - 1}")
