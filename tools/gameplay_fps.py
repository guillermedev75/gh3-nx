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

Protocolo A/B (F0.1): `--runs N` repete a corrida N vezes (mesma música/trecho) e agrega por mediana. Se as
linhas de PRESENT (`XELOG_GPU PRESENT native`, só com --log_level debug) tiverem timestamp, o mesmo trecho
medido pelos blocos também vira a distribuição de frame times:

  frame_fps_median mediana de 1000/frame time
  p99_ms           p99 do frame time (posto mais próximo)
  p1_fps           1% low = 1000/p99 do frame time (pior 1% dos quadros)
  pct_over_50ms    % de quadros acima de 50 ms
  frames_sampled   quadros usados de todas as corridas

Com `--runs`, os campos de FPS do topo são a mediana das corridas e `run_results` guarda cada corrida; p1 e
% >50 ms são calculados sobre os quadros de todas as corridas (mais estável que 3 amostras). O timestamp do
log tem resolução de 1 ms; no Switch ele é o relógio do próprio log (não o do NVK, que pede ×1,627).

Uso:
  python3 tools/gameplay_fps.py --renderer native --seconds 30 --out out/perf/baseline.json
  python3 tools/gameplay_fps.py --renderer native --seconds 30 --runs 3 --out out/perf/ab.json
  python3 tools/gameplay_fps.py ... --min-vs out/perf/baseline.json   # sai 1 se game_fps cair >5% vs a base
  python3 tools/gameplay_fps.py ... -- CMD ...                        # comando próprio (testes)
Sai 0 com o JSON em --out; 1 em timeout, se o jogo sair antes, se o log tiver "[FATAL]" ou se passar do --min-vs.
"""
from __future__ import annotations

import argparse
import datetime
import json
import math
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
SLOW = re.compile(r"\[native\] slow frames: (\d+) over 50 ms")
STAMP = re.compile(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]")
PRESENT = re.compile(r"XELOG_GPU PRESENT native")
MEDIAN_KEYS = ("swaps_per_s", "draws_per_s", "wait_ms", "draws_per_frame", "game_fps",
               "frame_fps_median", "p99_ms", "p1_fps", "pct_over_50ms")


def _stamp(line: str):
    m = STAMP.match(line)
    if not m:
        return None
    return datetime.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f")


def percentile(values, p: float):
    """Percentil por posto mais próximo (p em 0..100), sem interpolar."""
    if not values:
        return None
    ordered = sorted(values)
    rank = math.ceil(p / 100.0 * len(ordered))
    return float(ordered[min(max(rank - 1, 0), len(ordered) - 1)])


def frame_metrics(frame_ms):
    """Métricas de distribuição dos frame times (ms); {} sem quadros."""
    if not frame_ms:
        return {}
    median = statistics.median(frame_ms)
    p99 = percentile(frame_ms, 99)
    return {
        "frame_ms_median": median,
        "frame_fps_median": 1000.0 / median if median else 0.0,
        "p99_ms": p99,
        "p1_fps": 1000.0 / p99 if p99 else 0.0,
        "pct_over_50ms": 100.0 * sum(1 for ms in frame_ms if ms > 50) / len(frame_ms),
        "frames_sampled": len(frame_ms),
    }


def _frame_times(lines, start, end):
    """Deltas (ms) entre PRESENTs consecutivos dentro de (start, end], se os timestamps são utilizáveis."""
    if start is None or end is None or end <= start:
        return []
    times = []
    for line in lines:
        if not PRESENT.search(line):
            continue
        when = _stamp(line)
        if when is not None and start < when <= end:
            times.append(when)
    return [(b - a).total_seconds() * 1000.0 for a, b in zip(times, times[1:])]


def summarize(text: str, seconds: int):
    """Métricas dos blocos de 10 s depois da marca, ou None se a marca ou os blocos ainda não estão no log."""
    mark = re.search(rf"script-input: mark '{re.escape(MARK)}'", text)
    if not mark:
        return None
    after = text[mark.end():]
    lines = after.splitlines()
    blocks = []
    for line in lines:
        if (m := LAST.search(line)):
            blocks.append({"draws": int(m.group(1)), "swaps": int(m.group(2)), "per_frame": None,
                           "wait": None, "slow": None, "time": _stamp(line)})
        elif blocks and (m := PER_DRAWS.search(line)):
            blocks[-1]["per_frame"] = int(m.group(1))
        elif blocks and (m := PER_WAITS.search(line)):
            blocks[-1]["wait"] = float(m.group(1))
        elif blocks and (m := SLOW.search(line)):
            blocks[-1]["slow"] = int(m.group(1))
    used = blocks[1:1 + max(1, seconds // INTERVAL)]  # o 1º bloco pega o trecho antes da marca
    if len(used) < max(1, seconds // INTERVAL) or any(b["per_frame"] is None or b["wait"] is None for b in used):
        return None
    span = INTERVAL * len(used)
    draws_per_s = sum(b["draws"] for b in used) / span
    per_frame = statistics.median(b["per_frame"] for b in used)
    result = {
        "swaps_per_s": sum(b["swaps"] for b in used) / span,
        "draws_per_s": draws_per_s,
        "wait_ms": statistics.mean(b["wait"] for b in used),
        "draws_per_frame": per_frame,
        "game_fps": draws_per_s / per_frame if per_frame else 0.0,
        "blocks": len(used),
        "seconds": span,
    }
    start = blocks[0]["time"] if blocks else None
    end = used[-1]["time"]
    frame_ms = _frame_times(lines, start, end)
    if frame_ms:
        result.update(frame_metrics(frame_ms))
        result["frame_ms"] = frame_ms
    elif all(b["slow"] is not None for b in used) and sum(b["swaps"] for b in used):
        # Sem PRESENTs no log: % de quadros lentos pelo contador do próprio renderer.
        result["pct_over_50ms"] = 100.0 * sum(b["slow"] for b in used) / sum(b["swaps"] for b in used)
    return result


def aggregate(runs):
    """Agrega N corridas: mediana das métricas por corrida e p1/%>50ms sobre os quadros de todas."""
    out = {"runs": len(runs), "run_results": []}
    for run in runs:
        out["run_results"].append({k: v for k, v in run.items() if k != "frame_ms"})
    for key in MEDIAN_KEYS:
        values = [r[key] for r in runs if r.get(key) is not None]
        if values:
            out[key] = statistics.median(values)
    for key in ("seconds", "blocks"):
        values = [r[key] for r in runs if r.get(key) is not None]
        if values:
            out[key] = statistics.median(values)
    frames = [ms for run in runs for ms in run.get("frame_ms", [])]
    if frames:  # p1 e % >50 ms sobre a distribuição inteira do protocolo
        out.update(frame_metrics(frames))
    return out


def within_baseline(fps: float, baseline: float) -> bool:
    return fps >= baseline * (1 - TOLERANCE)


def run_once(a, work: pathlib.Path):
    work = pathlib.Path(work)
    log = work / "game.log"
    work.mkdir(parents=True, exist_ok=True)

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
                return None
            if proc.poll() is not None:
                print(f"FALHOU: o jogo saiu (código {proc.returncode}) antes de {a.seconds} s de gameplay medidos")
                return None
        if not result:
            print(f"FALHOU: timeout de {a.timeout:.0f} s sem {a.seconds} s de gameplay medidos")
            return None
    finally:
        try:
            os.killpg(proc.pid, signal.SIGTERM)  # só o grupo que abrimos
        except ProcessLookupError:
            pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
    return result


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--renderer", choices=("emulated", "native"), default="native")
    ap.add_argument("--seconds", type=int, default=30)
    ap.add_argument("--runs", type=int, default=1, help="corridas A/B na mesma música (mediana + p1)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--min-vs", help="JSON da linha de base; falha se game_fps cair mais de 5%%")
    ap.add_argument("--timeout", type=float, default=400, help="timeout por corrida (s)")
    ap.add_argument("--script", default=str(SCRIPT))
    ap.add_argument("cmd", nargs="*")
    a = ap.parse_args(argv)
    if a.runs < 1:
        ap.error("--runs precisa ser >= 1")

    out = pathlib.Path(a.out).resolve()
    work = out.parent / (out.stem + ".run")
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    runs = []
    for i in range(a.runs):
        run_dir = work if a.runs == 1 else work / f"run{i}"
        result = run_once(a, run_dir)
        if result is None:
            return 1
        runs.append(result)

    result = aggregate(runs)
    result["renderer"] = a.renderer
    out.write_text(json.dumps(result, indent=2) + "\n")
    extra = ""
    if result.get("p1_fps") is not None:
        extra = (f", p1 {result['p1_fps']:.1f}, p99 {result['p99_ms']:.1f} ms, "
                 f">50 ms {result['pct_over_50ms']:.1f}%")
    print(f"OK: game_fps {result['game_fps']:.1f} (mediana de {a.runs} corrida(s)), "
          f"swaps/s {result['swaps_per_s']:.1f}, draws/s {result['draws_per_s']:.0f}, "
          f"espera {result['wait_ms']:.1f} ms{extra} -> {out}")
    if a.min_vs:
        base = json.load(open(a.min_vs))["game_fps"]
        if not within_baseline(result["game_fps"], base):
            print(f"FALHOU: game_fps {result['game_fps']:.1f} < {base * (1 - TOLERANCE):.1f} "
                  f"(linha de base {base:.1f} - 5%)")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
