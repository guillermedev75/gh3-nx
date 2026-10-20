import importlib.util
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = str(ROOT / "tools/gameplay_fps.py")

spec = importlib.util.spec_from_file_location("gameplay_fps", TOOL)
fps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fps)

MARK = "[2026-10-17 09:00:00.000] [info] [core] [t1] script-input: mark 'gameplay' t=86200ms"


def block(swaps, draws, per_frame_draws, waits=0.9, wait_ms=7.6):
    p = "[2026-10-17 09:00:10.000] [info] [core] [t1] [native] "
    return "\n".join([
        f"{p}last 10 s: 5000 packets, {draws} draws, 100 copies, {swaps} swaps, 0 waits cut short",
        f"{p}renderer: 1 drawn, 2 skipped",
        f"{p}per frame: {per_frame_draws} draws, 6 state rebuilds, 81 texture binding rebuilds",
        f"{p}per frame: {waits} waits for the game, {wait_ms} ms waiting",
    ])


def log(*blocks, mark=True):
    pre = block(100, 1000, 10)  # antes da marca: não conta
    return "\n".join([pre] + ([MARK] if mark else []) + list(blocks)) + "\n"


def test_summarize_skips_the_block_that_straddles_the_mark():
    text = log(block(999, 99999, 99), block(500, 200000, 400, wait_ms=8), block(600, 240000, 400, wait_ms=6))
    d = fps.summarize(text, seconds=20)
    assert d["swaps_per_s"] == 55.0
    assert d["draws_per_s"] == 22000.0
    assert d["wait_ms"] == 7.0
    assert d["blocks"] == 2


def test_game_fps_is_draws_per_s_over_median_draws_per_frame():
    text = log(block(1, 1, 1), block(500, 200000, 400), block(500, 200000, 400), block(500, 300000, 500))
    d = fps.summarize(text, seconds=30)
    assert d["draws_per_frame"] == 400
    assert abs(d["game_fps"] - (700000 / 30) / 400) < 1e-9


def test_summarize_none_without_mark_or_enough_blocks():
    assert fps.summarize(log(block(5, 5, 5), mark=False), seconds=10) is None
    assert fps.summarize(log(block(5, 5, 5), block(5, 5, 5)), seconds=30) is None


def test_compare_flags_drop_over_five_percent():
    assert fps.within_baseline(95.1, 100.0)
    assert not fps.within_baseline(94.0, 100.0)


FAKE_GAME = """
import os, sys, time
blk = BLOCK
with open(os.environ["GH3_LOG_FILE"], "w") as f:
    f.write(MARK + "\\n")
    f.flush()
    for _ in range(3):
        time.sleep(0.2)
        f.write(blk + "\\n")
        f.flush()
open(sys.argv[1], "w").write(" ".join(sys.argv[2:]))
time.sleep(60)
"""


def run_tool(tmp_path, *extra, seconds="20"):
    game = tmp_path / "fake_game.py"
    game.write_text(FAKE_GAME.replace("BLOCK", repr(block(500, 200000, 400))).replace("MARK", repr(MARK)))
    argv_file = tmp_path / "argv.txt"
    out = tmp_path / "sub" / "r.json"
    cmd = [sys.executable, TOOL, "--renderer", "native", "--seconds", seconds, "--out", str(out),
           "--timeout", "20", *extra, "--", sys.executable, str(game), str(argv_file)]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60, cwd=ROOT)
    return r, out, argv_file


def test_tool_writes_json_and_passes_native_flag(tmp_path):
    r, out, argv_file = run_tool(tmp_path)
    assert r.returncode == 0, r.stdout + r.stderr
    d = json.loads(out.read_text())
    assert d["swaps_per_s"] == 50.0 and d["game_fps"] == 50.0 and d["wait_ms"] == 7.6
    argv = argv_file.read_text().split()
    assert "--gh3_renderer=native" in argv and "debug" in argv


def test_tool_min_vs_fails_when_slower_than_baseline(tmp_path):
    base = tmp_path / "base.json"
    base.write_text(json.dumps({"game_fps": 100.0}))
    r, out, _ = run_tool(tmp_path, "--min-vs", str(base))
    assert r.returncode == 1
    assert out.exists()  # o JSON sai mesmo assim
    base.write_text(json.dumps({"game_fps": 50.0}))
    r, _, _ = run_tool(tmp_path, "--min-vs", str(base))
    assert r.returncode == 0, r.stdout + r.stderr


def test_tool_fails_when_game_exits_without_data(tmp_path):
    game = tmp_path / "dies.py"
    game.write_text("import sys\nsys.exit(3)\n")
    out = tmp_path / "none.json"
    r = subprocess.run([sys.executable, TOOL, "--out", str(out), "--seconds", "10", "--timeout", "20",
                        "--", sys.executable, str(game)], capture_output=True, text=True, timeout=60, cwd=ROOT)
    assert r.returncode == 1 and not out.exists()


# --- T26/F0.1: protocolo A/B (3 corridas, mediana, p1, % >50 ms) ---

def present(ts):
    return f"[{ts}] [debug] [gpu] [t1] XELOG_GPU PRESENT native"


def block_at(ts, swaps, draws, per_frame_draws=10, wait_ms=7.6):
    p = f"[{ts}] [info] [core] [t1] [native] "
    return "\n".join([
        f"{p}last 10 s: 5000 packets, {draws} draws, 100 copies, {swaps} swaps, 0 waits cut short",
        f"{p}per frame: {per_frame_draws} draws, 6 state rebuilds, 81 texture binding rebuilds",
        f"{p}per frame: 0.9 waits for the game, {wait_ms} ms waiting",
    ])


def present_log(steps, start="2026-10-17 09:00:10.100"):
    """101 presents com os intervalos (s) de `steps`; devolve as linhas e o total de deltas."""
    import datetime

    t = datetime.datetime.strptime(start, "%Y-%m-%d %H:%M:%S.%f")
    lines = []
    for i in range(101):
        lines.append(present(t.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]))
        if i < len(steps):
            t += datetime.timedelta(seconds=steps[i])
    return lines


def test_summarize_p1_and_over_50ms_from_present_timestamps():
    # 95 quadros de 20 ms + 5 de 60 ms: p99 = 60 ms -> p1 low = 16,7 fps; 5% >50 ms.
    presents = present_log([0.020] * 95 + [0.060] * 5)
    text = "\n".join([
        MARK,
        block_at("2026-10-17 09:00:10.000", 300, 3000),
        block_at("2026-10-17 09:00:20.000", 300, 3000),
        block_at("2026-10-17 09:00:30.000", 300, 3000),
    ] + presents) + "\n"
    d = fps.summarize(text, seconds=20)
    assert d["frames_sampled"] == 100
    assert d["p99_ms"] == 60.0
    assert abs(d["p1_fps"] - 1000 / 60) < 1e-9
    assert d["pct_over_50ms"] == 5.0
    assert d["frame_ms_median"] == 20.0
    assert d["frame_fps_median"] == 50.0


def test_aggregate_runs_takes_median_and_pools_frames():
    runs = [
        {"swaps_per_s": 50, "draws_per_s": 500, "wait_ms": 5, "game_fps": 50,
         "frame_ms": [20, 20, 60]},
        {"swaps_per_s": 60, "draws_per_s": 600, "wait_ms": 3, "game_fps": 60,
         "frame_ms": [16, 16, 16]},
        {"swaps_per_s": 70, "draws_per_s": 700, "wait_ms": 1, "game_fps": 70,
         "frame_ms": [14, 14, 14]},
    ]
    d = fps.aggregate(runs)
    assert d["runs"] == 3 and len(d["run_results"]) == 3
    assert d["game_fps"] == 60 and d["swaps_per_s"] == 60 and d["wait_ms"] == 3
    assert "frame_ms" not in d["run_results"][0]
    assert d["p99_ms"] == 60.0  # pior dos 9 quadros, p99 por posto
    assert d["frames_sampled"] == 9
    assert abs(d["pct_over_50ms"] - 100 / 9) < 1e-9


def test_tool_runs_the_game_three_times(tmp_path):
    r, out, _ = run_tool(tmp_path, "--runs", "3", seconds="20")
    assert r.returncode == 0, r.stdout + r.stderr
    d = json.loads(out.read_text())
    assert d["runs"] == 3 and len(d["run_results"]) == 3
    assert d["game_fps"] == 50.0
    for i in range(3):
        assert (tmp_path / "sub" / "r.run" / f"run{i}" / "game.log").exists()
