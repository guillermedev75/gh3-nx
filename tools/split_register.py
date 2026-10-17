"""Reparte a função gh3_RegisterFunctions do codegen em pedaços menores.

O ReXGlue gera uma única função com ~60 mil chamadas registrar->SetFunction(...). Uma função desse tamanho
leva de 6 a mais de 11 minutos para compilar (medido em -O3, -O1 e -O0 num M1 Pro), o que trava cada rodada de
"função nova descoberta em runtime". Repartir em pedaços de N chamadas não muda o resultado: as mesmas
chamadas, na mesma ordem.

Uso: python3 tools/split_register.py generated/default/gh3_register.cpp [--chunk 2000]
"""
import re
import sys

MAIN = "void gh3_RegisterFunctions(rex::runtime::IModuleRegistrar* registrar) {"
CALL = re.compile(r"^\s*registrar->SetFunction\(.*\);\s*$")


def split_register(text: str, chunk: int = 2000) -> str:
    if "gh3_RegisterFunctions_part" in text or MAIN not in text:
        return text  # já repartido, ou formato desconhecido: não mexe
    head, rest = text.split(MAIN, 1)
    body, tail = rest.rsplit("}", 1)
    lines = [line for line in body.splitlines() if line.strip()]
    if len(lines) <= chunk or not all(CALL.match(line) for line in lines):
        return text
    parts = [lines[i:i + chunk] for i in range(0, len(lines), chunk)]
    out = [head]
    for n, part in enumerate(parts):
        out.append(f"static void gh3_RegisterFunctions_part{n}(rex::runtime::IModuleRegistrar* registrar) {{\n")
        out.append("\n".join(part) + "\n}\n\n")
    out.append(MAIN + "\n")
    out.extend(f"  gh3_RegisterFunctions_part{n}(registrar);\n" for n in range(len(parts)))
    out.append("}" + tail)
    return "".join(out)


if __name__ == "__main__":
    path = sys.argv[1]
    chunk = int(sys.argv[sys.argv.index("--chunk") + 1]) if "--chunk" in sys.argv else 2000
    with open(path) as f:
        original = f.read()
    result = split_register(original, chunk)
    if result != original:
        with open(path, "w") as f:
            f.write(result)
        print(f"{path}: repartido em pedaços de {chunk}")
    else:
        print(f"{path}: nada a fazer")
