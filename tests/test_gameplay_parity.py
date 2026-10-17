"""T9: o quadro nativo comparado ao emulado.

- tools/image_stats.py --compare: diferença média por canal entre dois PNGs do mesmo tamanho.
- tools/gameplay_capture.py sem --frame: captura na marca "gameplay" do roteiro (relógio de parede, o mesmo
  das entradas), não num índice de quadro. O nativo apresenta ~60 quadros/s no menu e o emulado ~34, então
  o quadro 2700 do T8 caía no menu principal no nativo.
"""
import json
import pathlib
import subprocess
import sys
import textwrap

from test_gameplay_capture import CAPTURE, ROOT, stats, write_png


# ---------------------------------------------------------------- image_stats --compare


def test_compare_identical_frames_has_zero_diff(tmp_path):
    pix = lambda x, y: ((x * 9) % 256, (y * 13) % 256, 77)
    a = write_png(tmp_path / "a.png", 16, 8, pix)
    b = write_png(tmp_path / "b.png", 16, 8, pix, color_type=6, filters=(4, 1))
    r = stats(a, "--compare", b, "--max-mean-diff", "0")
    assert r.returncode == 0, r.stdout + r.stderr
    assert "mean_diff=0.0" in r.stdout


def test_compare_mean_diff_is_per_channel_average(tmp_path):
    # metade dos pixels difere 60 em R e 0 em G/B: (60 + 0 + 0) / 3 / 2 = 10
    a = write_png(tmp_path / "a.png", 4, 4, lambda x, y: (100, 50, 50))
    b = write_png(tmp_path / "b.png", 4, 4, lambda x, y: (160, 50, 50) if x < 2 else (100, 50, 50))
    r = stats(a, "--compare", b, "--max-mean-diff", "40")
    assert r.returncode == 0, r.stdout + r.stderr
    assert "mean_diff=10.0" in r.stdout


def test_compare_fails_above_max_mean_diff(tmp_path):
    # o T9 antes da correção: palco branco contra o palco escuro do emulado
    a = write_png(tmp_path / "white.png", 8, 8, lambda x, y: (255, 255, 255))
    b = write_png(tmp_path / "dark.png", 8, 8, lambda x, y: (30, 40, 50))
    r = stats(a, "--compare", b, "--max-mean-diff", "40")
    assert r.returncode == 1
    assert "FALHOU" in r.stdout and "mean_diff" in r.stdout


def test_compare_rejects_size_mismatch(tmp_path):
    a = write_png(tmp_path / "a.png", 8, 8, lambda x, y: (1, 2, 3))
    b = write_png(tmp_path / "b.png", 4, 8, lambda x, y: (1, 2, 3))
    assert stats(a, "--compare", b, "--max-mean-diff", "40").returncode == 2


# ---------------------------------------------------------------- roteiro e captura pela marca


def test_song_script_ends_with_gameplay_mark_in_the_middle_of_the_song():
    # Slow Ride/Medium: a nota chega ~66 s depois da partida e a música falha (sem tocar) ~93 s
    # (linha do tempo do T8, PROGRESS.md); a marca fica no meio.
    script = json.loads((ROOT / "config/song_script.json").read_text())
    assert script[-1] == {"action": "mark", "value": "gameplay"}
    t = sum(e["value"] for e in script if e["action"] == "delay")
    assert 76000 <= t <= 86000


FAKE_MARK_GAME = """
    import pathlib, struct, sys, time, zlib
    args = sys.argv[1:]
    pathlib.Path(args[0]).write_text(" ".join(args[1:]))
    out = pathlib.Path(next(a.split("=", 1)[1] for a in args if a.startswith("--mark_screenshot_dir=")))
    mark = next(a.split("=", 1)[1] for a in args if a.startswith("--mark_screenshot="))
    def chunk(k, d):
        return struct.pack(">I", len(d)) + k + d + struct.pack(">I", zlib.crc32(k + d))
    raw = b"".join(b"\\0" + bytes([7, 8, 9]) * 4 for _ in range(4))
    png = b"\\x89PNG\\r\\n\\x1a\\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    out.mkdir(parents=True, exist_ok=True)
    time.sleep(0.5)
    (out / "mark_title_emu.png").write_bytes(b"not this one")
    name = f"mark_{mark}_emu.png"
    (out / name).write_bytes(png)
    (out / "marks.csv").write_text("label,mark_t_ms,frame_before,frame_after,guest_ticks,guest_seconds,frame_delta_s,mode,width,height,file\\n"
                                   "title,38300,1,1,0,0,0,emu,4,4,mark_title_emu.png\\n"
                                   f"{mark},82000,9,9,0,0,0,emu,4,4,{name}\\n")
    time.sleep(60)
"""


def run_mark_capture(tmp_path, *extra):
    game = tmp_path / "fake_game.py"
    game.write_text(textwrap.dedent(FAKE_MARK_GAME))
    argv_file = tmp_path / "argv.txt"
    out = tmp_path / "sub" / "shot.png"
    r = subprocess.run(
        [sys.executable, CAPTURE, "--out", str(out), "--timeout", "20", *extra,
         "--", sys.executable, str(game), str(argv_file)],
        capture_output=True, text=True, timeout=60, cwd=ROOT)
    return r, out, argv_file


def test_capture_defaults_to_the_gameplay_mark(tmp_path):
    r, out, argv_file = run_mark_capture(tmp_path, "--renderer", "native")
    assert r.returncode == 0, r.stdout + r.stderr
    assert out.read_bytes().startswith(b"\x89PNG")
    argv = argv_file.read_text().split()
    assert "--mark_screenshot=gameplay" in argv
    assert not any(a.startswith("--parity_capture_frames=") for a in argv)
    assert "--gh3_renderer=native" in argv


def test_capture_mark_can_be_chosen(tmp_path):
    r, out, argv_file = run_mark_capture(tmp_path, "--mark", "song")
    assert r.returncode == 0, r.stdout + r.stderr
    assert "--mark_screenshot=song" in argv_file.read_text().split()


# ---------------------------------------------------------------- ligação no app


def test_app_screenshots_the_marks_asked_for():
    app = (ROOT / "src/gh3_app.h").read_text()
    assert "REXCVAR_DEFINE_STRING(mark_screenshot" in app
    assert "rexglue_script_input::SetMarkObserver(" in app
    assert "parity_capture_.RequestNamedCapture(" in app
    assert "rexglue_script_input::SetMarkObserver({})" in app  # tirado no OnShutdown


MARK_MAIN = r"""
#include "mark_screenshot.h"
#include <cstdio>
int main(int argc, char** argv) { std::printf("%d", gh3::mark_screenshot::Wanted(argv[1], argv[2]) ? 1 : 0); }
"""


def test_mark_list_matching(tmp_path):
    (tmp_path / "main.cpp").write_text(MARK_MAIN)
    exe = tmp_path / "m"
    subprocess.run(["c++", "-std=c++20", f"-I{ROOT / 'src'}", str(tmp_path / "main.cpp"), "-o", str(exe)], check=True)
    run = lambda lst, name: subprocess.run([str(exe), lst, name], capture_output=True, text=True, check=True).stdout
    assert run("gameplay", "gameplay") == "1"
    assert run("title,gameplay", "gameplay") == "1"
    assert run("title,gameplay", "game") == "0"
    assert run("*", "anything") == "1"
    assert run("", "gameplay") == "0"
