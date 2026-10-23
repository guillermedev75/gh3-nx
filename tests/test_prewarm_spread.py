import subprocess
import sys


def log(tmp_path, lines):
    path = tmp_path / "game.log"
    path.write_text("\n".join(lines) + "\n")
    return path


def run(path):
    return subprocess.run([sys.executable, "tools/check_prewarm_spread.py", str(path)],
                          capture_output=True, text=True)


def good_log():
    return [
        "[native] pipeline prewarm: 159 queued, incremental over the first frames",
        "XELOG_GPU PRESENT 1",
        "[native] pipeline prewarm: 2/159 incremental, 1.9 ms this frame",
        "XELOG_GPU PRESENT 2",
        "[native] pipeline prewarm: 159/159 incremental, 1.4 ms this frame",
        "[native] pipeline prewarm: done in 812 ms over 159 frames (159 submitted, 0 failed, 0 skipped)",
    ]


def test_spread_prewarm_passes(tmp_path):
    assert run(log(tmp_path, good_log())).returncode == 0


def test_single_frame_is_rejected(tmp_path):
    lines = [good_log()[0], "XELOG_GPU PRESENT 1",
             "[native] pipeline prewarm: done in 40000 ms over 1 frames (159 submitted, 0 failed, 0 skipped)"]
    assert run(log(tmp_path, lines)).returncode == 1


def test_missing_done_line_is_rejected(tmp_path):
    assert run(log(tmp_path, good_log()[:-1])).returncode == 1


def test_present_after_done_is_rejected(tmp_path):
    lines = good_log()[:-1]
    lines.append(good_log()[-1])
    lines = [l for l in lines if "XELOG_GPU PRESENT" not in l] + ["XELOG_GPU PRESENT 9"]
    assert run(log(tmp_path, lines)).returncode == 1


def test_short_incremental_slice_is_rejected(tmp_path):
    lines = good_log()
    lines[4] = "[native] pipeline prewarm: 120/159 incremental, 1.4 ms this frame"
    assert run(log(tmp_path, lines)).returncode == 1


def test_missing_file_opens_way_to_error(tmp_path):
    assert run(tmp_path / "nao-existe.log").returncode != 0
