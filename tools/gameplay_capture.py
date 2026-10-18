"""Captura um quadro da gameplay: abre o jogo com o roteiro de entrada e a captura de paridade, espera o PNG.

O roteiro (config/song_script.json, src/rexglue_script_input.h) leva do menu a uma música no Quickplay; a
captura (src/parity_capture.h) grava a saída do guest quando o roteiro passa pela marca --mark (padrão
"gameplay", no meio da música; src/mark_screenshot.h). A marca é relógio de parede, o mesmo das entradas,
então cai no mesmo ponto nos dois renderers. Com --frame, a captura é no quadro N, contado pelas linhas
"XELOG_GPU PRESENT" reais (src/present_counter.h) - mas o nativo apresenta ~60 quadros/s no menu e o
emulado ~34, então o mesmo N cai em cenas diferentes. O jogo roda com --log_level debug (o contador precisa).
O PNG é copiado para --out e o jogo é fechado (só o grupo de processos que este script abriu).

Uso:
  python3 tools/gameplay_capture.py --renderer emulated --out out/parity/emulated.png
  python3 tools/gameplay_capture.py --renderer native   --out out/parity/native.png
  python3 tools/gameplay_capture.py ... --mark song | --frame 2700
  python3 tools/gameplay_capture.py ... -- CMD ...      # comando próprio (testes); recebe as mesmas flags
  python3 tools/gameplay_capture.py ... -- --cvar=valor # só flags: cvars extras para o run-gh3.command
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
# A marca no fim do roteiro, no meio da música (Slow Ride, Medium; ver PROGRESS.md, T8 e T9).
DEFAULT_MARK = "gameplay"
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


def marked_png(capture_dir: pathlib.Path, mark: str):
    """O PNG da marca no marks.csv (escrito depois do PNG, como o captures.csv)."""
    manifest = capture_dir / "marks.csv"
    if not manifest.exists():
        return None
    with open(manifest, newline="") as f:
        rows = [r for r in csv.DictReader(f) if r.get("label") == mark and r.get("file")]
    if not rows:
        return None
    png = capture_dir / rows[0]["file"]
    return png if png.exists() else None


def split_cmd(rest):
    """Depois de `--`: só flags (`--cvar=x`) são cvars extras do jogo padrão; senão é um comando próprio."""
    if rest and rest[0].startswith("-"):
        return [], list(rest)
    return list(rest), []


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--renderer", choices=("emulated", "native"), default="emulated")
    ap.add_argument("--out", required=True)
    ap.add_argument("--frame", type=int, help="quadro (contagem de PRESENT) em vez da marca")
    ap.add_argument("--mark", default=DEFAULT_MARK)
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

    flags = ["--log_level", "debug", f"--input_json_file={pathlib.Path(a.script).resolve()}"]
    if a.frame is not None:
        flags += [f"--parity_capture_frames={a.frame}", f"--parity_capture_dir={work / 'frames'}"]
        find = lambda: captured_png(work / "frames")
        what = f"o quadro {a.frame}"
    else:
        flags += [f"--mark_screenshot={a.mark}", f"--mark_screenshot_dir={work / 'frames'}"]
        find = lambda: marked_png(work / "frames", a.mark)
        what = f"a marca {a.mark}"
    if a.renderer == "native":
        flags.append("--gh3_renderer=native")
    own_cmd, extra = split_cmd(a.cmd)
    cmd = (own_cmd or [str(ROOT / "run-gh3.command")]) + flags + extra
    env = dict(os.environ, GH3_LOG_FILE=str(log))
    env.setdefault("GH3_BUILD_DIR", BUILD_DIR)

    proc = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    deadline = time.time() + a.timeout
    try:
        while time.time() < deadline:
            time.sleep(0.5)
            png = find()
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
                print(f"FALHOU: o jogo saiu (código {proc.returncode}) sem {what}")
                return 1
        presents = log.read_text(errors="replace").count("XELOG_GPU PRESENT") if log.exists() else 0
        print(f"FALHOU: timeout de {a.timeout:.0f} s sem {what} ({presents} PRESENT no log)")
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
