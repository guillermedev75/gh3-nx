"""Mede o FPS da gameplay: roda o roteiro até a marca e lê as estatísticas que o renderer nativo loga.

O renderer escreve a cada 10 s "[native] last 10 s: ... N draws, ... M swaps ..." e, em seguida, "per frame:
D draws, ..." e "per frame: W waits for the game, X ms waiting" (src/native/gh3_native_system.cpp). Depois da
marca (config/song_script.json, "gameplay", no meio da música) o primeiro bloco de 10 s está pela metade no
menu/carregamento e é descartado; os --seconds/10 seguintes entram na média.

  swaps_per_s      swaps (apresentações) por segundo
  draws_per_s      draws por segundo
  wait_ms          média de "ms waiting" por quadro (o renderer esperando o jogo)
  draws_per_frame  mediana de "per frame: N draws" nos blocos
  game_fps         draws_per_s / draws_per_frame: quadros de gameplay por segundo, sem depender de a
                   apresentação ser repetida quando o jogo não termina o quadro a tempo (estimativa)

Uso:
  python3 tools/gameplay_fps.py --renderer native --seconds 30 --out out/perf/baseline.json
  python3 tools/gameplay_fps.py ... --min-vs out/perf/baseline.json   # sai 1 se game_fps cair >5% vs a base
  python3 tools/gameplay_fps.py ... -- CMD ...                        # comando próprio (testes)
Sai 0 com o JSON em --out; 1 em timeout, se o jogo sair antes, se o log tiver "[FATAL]" ou se passar do --min-vs.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "config/song_script.json"
BUILD_DIR = "out/build/mac-arm64-p8sdk"
MARK = "gameplay"
INTERVAL = 10  # s por bloco "last 10 s"
TOLERANCE = 0.05
FATAL = re.compile(r"\[FATAL\].*")
LAST = re.compile(r"\[native\] last 10 s: .*?(\d+) draws, .*?(\d+) swaps")
PER_DRAWS = re.compile(r"\[native\] per frame: (\d+) draws")
PER_WAITS = re.compile(r"\[native\] per frame: [\d.]+ waits for the game, ([\d.]+) ms waiting")


def summarize(text: str, seconds: int):
    """Métricas dos blocos de 10 s depois da marca, ou None se a marca ou os blocos ainda não estão no log."""
    mark = re.search(rf"script-input: mark '{re.escape(MARK)}'", text)
    if not mark:
        return None
    blocks = []
    for line in text[mark.end():].splitlines():
        if (m := LAST.search(line)):
            blocks.append({"draws": int(m.group(1)), "swaps": int(m.group(2)), "per_frame": None, "wait": None})
        elif blocks and (m := PER_DRAWS.search(line)):
            blocks[-1]["per_frame"] = int(m.group(1))
        elif blocks and (m := PER_WAITS.search(line)):
            blocks[-1]["wait"] = float(m.group(1))
    used = blocks[1:1 + max(1, seconds // INTERVAL)]  # o 1º bloco pega o trecho antes da marca
    if len(used) < max(1, seconds // INTERVAL) or any(b["per_frame"] is None or b["wait"] is None for b in used):
        return None
    span = INTERVAL * len(used)
    draws_per_s = sum(b["draws"] for b in used) / span
    per_frame = statistics.median(b["per_frame"] for b in used)
    return {
        "swaps_per_s": sum(b["swaps"] for b in used) / span,
        "draws_per_s": draws_per_s,
        "wait_ms": statistics.mean(b["wait"] for b in used),
        "draws_per_frame": per_frame,
        "game_fps": draws_per_s / per_frame if per_frame else 0.0,
        "blocks": len(used),
        "seconds": span,
    }


def within_baseline(fps: float, baseline: float) -> bool:
    return fps >= baseline * (1 - TOLERANCE)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--renderer", choices=("emulated", "native"), default="native")
    ap.add_argument("--seconds", type=int, default=30)
    ap.add_argument("--out", required=True)
    ap.add_argument("--min-vs", help="JSON da linha de base; falha se game_fps cair mais de 5%%")
    ap.add_argument("--timeout", type=float, default=400)
    ap.add_argument("--script", default=str(SCRIPT))
    ap.add_argument("cmd", nargs="*")
    a = ap.parse_args(argv)

    out = pathlib.Path(a.out).resolve()
    work = out.parent / (out.stem + ".run")
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    log = work / "game.log"
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    flags = ["--log_level", "debug", f"--input_json_file={pathlib.Path(a.script).resolve()}"]
    if a.renderer == "native":
        flags.append("--gh3_renderer=native")
    # só flags depois de `--` são cvars extras para o jogo padrão (como em gameplay_capture.py)
    own_cmd = [] if a.cmd and a.cmd[0].startswith("-") else a.cmd
    extra = a.cmd if a.cmd and a.cmd[0].startswith("-") else []
    cmd = (own_cmd or [str(ROOT / "run-gh3.command")]) + flags + extra
    env = dict(os.environ, GH3_LOG_FILE=str(log))
    env.setdefault("GH3_BUILD_DIR", BUILD_DIR)

    proc = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    deadline = time.time() + a.timeout
    result = None
    try:
        while time.time() < deadline:
            time.sleep(0.5)
            text = log.read_text(errors="replace") if log.exists() else ""
            result = summarize(text, a.seconds)
            if result:
                break
            fatal = FATAL.search(text)
            if fatal:
                print(f"FALHOU: {fatal.group(0)}")
                return 1
            if proc.poll() is not None:
                print(f"FALHOU: o jogo saiu (código {proc.returncode}) antes de {a.seconds} s de gameplay medidos")
                return 1
        if not result:
            print(f"FALHOU: timeout de {a.timeout:.0f} s sem {a.seconds} s de gameplay medidos")
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

    result["renderer"] = a.renderer
    out.write_text(json.dumps(result, indent=2) + "\n")
    print(f"OK: game_fps {result['game_fps']:.1f}, swaps/s {result['swaps_per_s']:.1f}, "
          f"draws/s {result['draws_per_s']:.0f}, espera {result['wait_ms']:.1f} ms -> {out}")
    if a.min_vs:
        base = json.load(open(a.min_vs))["game_fps"]
        if not within_baseline(result["game_fps"], base):
            print(f"FALHOU: game_fps {result['game_fps']:.1f} < {base * (1 - TOLERANCE):.1f} "
                  f"(linha de base {base:.1f} - 5%)")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
