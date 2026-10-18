"""Reescreve as chamadas `bl` do código gerado para irem direto ao corpo da função.

O codegen emite `sub_X(ctx, base);` para cada `bl`. `sub_X` é um símbolo fraco (alias no GCC, trampolim
musttail no clang) que só existe para o runtime/ganchos substituírem a função; o compilador não enxerga o
corpo através dele, então não inlina nem faz LTO entre funções traduzidas. Trocar por `__imp__sub_X` (o
corpo real, já declarado por DECLARE_REX_FUNC) corta o salto extra e libera inline/LTO.

Não há ganchos por nome em `sub_*` no GH3 (src/ não define nenhum); se surgir, a função hookada precisa
ficar fora da reescrita. Chamadas indiretas (REX_CALL_INDIRECT_FUNC) e `__imp__*` não são tocadas.

Uso: python3 tools/llamadas_diretas.py generated/default/gh3_recomp.*.cpp
"""
import re
import sys

CALL = re.compile(r"^(\s+)(sub_[0-9A-Fa-f]+)\(ctx, base\);", re.M)


def rewrite(text: str) -> str:
    return CALL.sub(r"\1__imp__\2(ctx, base);", text)


if __name__ == "__main__":
    total = 0
    for path in sys.argv[1:]:
        with open(path) as f:
            original = f.read()
        result = rewrite(original)
        if result != original:
            total += len(CALL.findall(original))
            with open(path, "w") as f:
                f.write(result)
    print(f"llamadas_diretas: {total} chamadas reescritas em {len(sys.argv) - 1} arquivos")
