"""Escreve generated/rexglue.cmake a partir do template do sdk/ patcheado, sem rodar o rexglue.

Só serve para o primeiro configure de um checkout sem generated/: o CMakeLists.txt inclui esse arquivo, e é ele
que define o passo de codegen do build (que depois regenera o arquivo com o próprio rexglue).

Uso: python3 tools/render_glue.py [--sdk sdk] [--out generated/rexglue.cmake]
"""
import argparse
import pathlib
import re
import sys

VALUES = {
    "sdk_version": "0.10.0",
    "entrypoint_out_dir": "generated/default",
    "manifest_file": "gh3_manifest.toml",
    "names.snake_case": "gh3",
}


def render(template):
    text = re.sub(r'\{\{ cmake_var\("(\w+)"\) \}\}', r"${\1}", template)
    for key, value in VALUES.items():
        text = text.replace("{{ %s }}" % key, value)
    if "{{" in text or "{%" in text:
        raise ValueError("template com construção não suportada")
    return text


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", default="sdk")
    ap.add_argument("--out", default="generated/rexglue.cmake")
    a = ap.parse_args(argv)
    out = pathlib.Path(a.out)
    if out.exists():
        return 0
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(render((pathlib.Path(a.sdk) / "resources/templates/init/rexglue_cmake.inja").read_text()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
