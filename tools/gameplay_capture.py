"""Captura um quadro da gameplay: abre o jogo com o roteiro de entrada e a captura de paridade, espera o PNG.

O roteiro (config/song_script.json, src/rexglue_script_input.h) leva do menu a uma música no Quickplay; a
captura (src/parity_capture.h) grava a saída do guest no quadro --frame, contado pelas linhas
"XELOG_GPU PRESENT" reais (src/present_counter.h; por isso o jogo roda com --log_level debug). O PNG é
copiado para --out e o jogo é fechado (só o grupo de processos que este script abriu).

Uso:
  python3 tools/gameplay_capture.py --renderer emulated --out out/parity/emulated.png
  python3 tools/gameplay_capture.py --renderer native   --out out/parity/native.png
  python3 tools/gameplay_capture.py ... -- CMD ...      # comando próprio (testes); recebe as mesmas flags
Sai 0 com o PNG em --out; 1 em timeout, se o jogo sair antes ou se o log tiver "[FATAL]".
"""
from __future__ import annotations

import argparse
import csv
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "config/song_script.json"
BUILD_DIR = "out/build/mac-arm64-p8sdk"
# Quadro no meio da música: com config/song_script.json a gameplay (Slow Ride, Medium) vai de ~2350 a ~3150
# quadros (a música falha sem notas tocadas, ~3200); medido em 2026-10-17, ver PROGRESS.md (T8).
DEFAULT_FRAME = 2700
FATAL = re.compile(r"\[FATAL\].*")


def captured_png(capture_dir: pathlib.Path):
    """O PNG da primeira linha do captures.csv. O manifesto é escrito depois do PNG, então o PNG está completo."""
    manifest = capture_dir / "captures.csv"
    if not manifest.exists():
        return None
    with open(manifest, newline="") as f:
        rows = [r for r in csv.DictReader(f) if r.get("file")]
    if not rows:
        return None
    png = capture_dir / rows[0]["file"]
    return png if png.exists() else None


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--renderer", choices=("emulated", "native"), default="emulated")
    ap.add_argument("--out", required=True)
    ap.add_argument("--frame", type=int, default=DEFAULT_FRAME)
    ap.add_argument("--timeout", type=float, default=300)
    ap.add_argument("--script", default=str(SCRIPT))
    ap.add_argument("cmd", nargs="*")
    a = ap.parse_args(argv)

    out = pathlib.Path(a.out).resolve()
    work = out.parent / (out.stem + ".capture")
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    log = work / "game.log"
    if out.exists():
        out.unlink()

    flags = [
        "--log_level", "debug",
        f"--input_json_file={pathlib.Path(a.script).resolve()}",
        f"--parity_capture_frames={a.frame}",
        f"--parity_capture_dir={work / 'frames'}",
    ]
    if a.renderer == "native":
        flags.append("--gh3_renderer=native")
    cmd = (a.cmd or [str(ROOT / "run-gh3.command")]) + flags
    env = dict(os.environ, GH3_LOG_FILE=str(log))
    env.setdefault("GH3_BUILD_DIR", BUILD_DIR)

    proc = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    deadline = time.time() + a.timeout
    try:
        while time.time() < deadline:
            time.sleep(0.5)
            png = captured_png(work / "frames")
            if png:
                shutil.copyfile(png, out)
                print(f"OK: {png.name} -> {out}")
                return 0
            if log.exists():
                fatal = FATAL.search(log.read_text(errors="replace"))
                if fatal:
                    print(f"FALHOU: {fatal.group(0)}")
                    return 1
            if proc.poll() is not None:
                print(f"FALHOU: o jogo saiu (código {proc.returncode}) antes do quadro {a.frame}")
                return 1
        presents = log.read_text(errors="replace").count("XELOG_GPU PRESENT") if log.exists() else 0
        print(f"FALHOU: timeout de {a.timeout:.0f} s sem o quadro {a.frame} ({presents} PRESENT no log)")
        return 1
    finally:
        try:
            os.killpg(proc.pid, signal.SIGTERM)  # só o grupo que abrimos
        except ProcessLookupError:
            pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
