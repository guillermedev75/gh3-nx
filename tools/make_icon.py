"""Ícone do NRO a partir do próprio jogo: recorta o logo do menu principal e salva 256x256 JPEG.

A arte é da publisher, então o ícone NUNCA entra no git: é gerado na máquina de quem tem o disco e fica em
out/switch/icon.jpg (ignorado). O cmake/switch.cmake usa esse arquivo quando existe e, sem ele, o ícone
genérico switch/icon.jpg.

Uso:
  python3 tools/make_icon.py                   # abre o jogo, captura o menu principal (marca main_menu) e recorta
  python3 tools/make_icon.py --from menu.png   # recorta de uma captura 1280x720 que já existe
"""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "switch", "icon.jpg")
# Quadrado com o logo "Guitar Hero III - Legends of Rock" no menu principal (1280x720).
CROP = {"x": 210, "y": 50, "size": 380}


def crop_command(src, dst):
    """sips: recorta CROP de src e reduz para 256x256 JPEG em dst."""
    return [
        ["sips", "-c", str(CROP["size"]), str(CROP["size"]), "--cropOffset", str(CROP["y"]), str(CROP["x"]),
         src, "--out", dst + ".png"],
        ["sips", "-z", "256", "256", "-s", "format", "jpeg", dst + ".png", "--out", dst],
    ]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="src")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args(argv)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    src = a.src
    if not src:
        src = os.path.join(tempfile.mkdtemp(), "menu.png")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gameplay_capture.py"), "--renderer", "emulated",
                        "--mark", "main_menu", "--out", src], check=True)
    for cmd in crop_command(src, a.out):
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    os.remove(a.out + ".png")
    print(f"ícone: {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
