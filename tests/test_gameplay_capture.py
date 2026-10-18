import json
import pathlib
import re
import struct
import subprocess
import sys
import textwrap
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
STATS = str(ROOT / "tools/image_stats.py")
CAPTURE = str(ROOT / "tools/gameplay_capture.py")


def png_bytes(width, height, pixel, color_type=2, filters=(0,)):
    """PNG mínimo. pixel(x, y) -> (r, g, b); filtros por linha em ciclo (0 None, 1 Sub, 2 Up, 3 Avg, 4 Paeth)."""
    bpp = 4 if color_type == 6 else 3
    rows, prev = [], bytes(width * bpp)

    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

    for y in range(height):
        line = b"".join(bytes(pixel(x, y)) + (b"\xff" if bpp == 4 else b"") for x in range(width))
        f = filters[y % len(filters)]
        enc = bytearray()
        for i in range(len(line)):
            left = line[i - bpp] if i >= bpp else 0
            up = prev[i]
            up_left = prev[i - bpp] if i >= bpp else 0
            pred = (0, left, up, (left + up) >> 1, paeth(left, up, up_left))[f]
            enc.append((line[i] - pred) & 0xFF)
        rows.append(bytes([f]) + bytes(enc))
        prev = line

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")


def write_png(path, *a, **k):
    path.write_bytes(png_bytes(*a, **k))
    return str(path)


def stats(*args):
    return subprocess.run([sys.executable, STATS, *args], capture_output=True, text=True, timeout=60)


# ---------------------------------------------------------------- image_stats


def test_all_white_frame_fails_max_white(tmp_path):
    # O sintoma do T9: palco 3D branco atrás da HUD.
    png = write_png(tmp_path / "w.png", 32, 16, lambda x, y: (255, 255, 255))
    r = stats(png, "--max-white", "0.15", "--min-stddev", "20")
    assert r.returncode == 1, r.stdout + r.stderr
    assert "white=1.000" in r.stdout


def test_flat_gray_frame_fails_min_stddev(tmp_path):
    png = write_png(tmp_path / "g.png", 32, 16, lambda x, y: (90, 90, 90))
    r = stats(png, "--max-white", "0.15", "--min-stddev", "20")
    assert r.returncode == 1
    assert "stddev=0.0" in r.stdout


def test_varied_frame_passes(tmp_path):
    png = write_png(tmp_path / "v.png", 32, 16, lambda x, y: ((x * 8) % 240, (y * 15) % 240, 40))
    r = stats(png, "--max-white", "0.15", "--min-stddev", "20")
    assert r.returncode == 0, r.stdout + r.stderr


def test_white_fraction_counts_only_pixels_white_in_all_three_channels(tmp_path):
    # Metade branca (≥245 nos 3 canais); a outra metade tem R=255 mas não é branca.
    png = write_png(tmp_path / "h.png", 10, 10, lambda x, y: (255, 255, 250) if x < 5 else (255, 0, 0))
    r = stats(png)
    assert r.returncode == 0
    assert "white=0.500" in r.stdout


def test_decodes_every_filter_and_rgba(tmp_path):
    pix = lambda x, y: ((x * 37) % 256, (y * 53) % 256, (x * y * 11) % 256)
    plain = stats(write_png(tmp_path / "a.png", 12, 10, pix))
    filtered = stats(write_png(tmp_path / "b.png", 12, 10, pix, color_type=6, filters=(1, 2, 3, 4, 0)))
    assert plain.returncode == filtered.returncode == 0, filtered.stdout + filtered.stderr
    assert plain.stdout.split(":", 1)[1] == filtered.stdout.split(":", 1)[1]


def test_rejects_non_png(tmp_path):
    p = tmp_path / "x.png"
    p.write_bytes(b"not a png")
    r = stats(str(p))
    assert r.returncode == 2


# ---------------------------------------------------------------- roteiro


def test_song_script_follows_the_quickplay_route():
    # Roteiro do Guillerme: Start → baixo ×2 → A ×3 leva do menu a uma música no Quickplay. Antes dele, só
    # Start para pular os vídeos de abertura (sem isso a abertura passa de 3000 quadros).
    script = json.loads((ROOT / "config/song_script.json").read_text())
    buttons = [e["value"] for e in script if e["action"] == "button"]
    assert buttons[-6:] == ["start", "down", "down", "a", "a", "a"]
    assert set(buttons[:-6]) <= {"start"}
    assert script[0]["action"] == "delay" and script[0]["value"] > 0
    for e in script:
        assert e["action"] in ("delay", "button", "mark")


# ---------------------------------------------------------------- gameplay_capture


FAKE_GAME = """
    import pathlib, struct, sys, time, zlib
    args = sys.argv[1:]
    pathlib.Path(args[0]).write_text(" ".join(args[1:]))
    out = pathlib.Path(next(a.split("=", 1)[1] for a in args if a.startswith("--parity_capture_dir=")))
    frame = next(a.split("=", 1)[1] for a in args if a.startswith("--parity_capture_frames="))
    def chunk(k, d):
        return struct.pack(">I", len(d)) + k + d + struct.pack(">I", zlib.crc32(k + d))
    raw = b"".join(b"\\0" + bytes([7, 8, 9]) * 4 for _ in range(4))
    png = b"\\x89PNG\\r\\n\\x1a\\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    out.mkdir(parents=True, exist_ok=True)
    time.sleep(0.5)
    name = f"frame_{int(frame) + 1}_emu.png"
    (out / name).write_bytes(png)
    (out / "captures.csv").write_text("requested_frame,frame_before,file\\n" + f"{frame},{int(frame) + 1},{name}\\n")
    time.sleep(60)
"""


def run_capture(tmp_path, *extra):
    game = tmp_path / "fake_game.py"
    game.write_text(textwrap.dedent(FAKE_GAME))
    argv_file = tmp_path / "argv.txt"
    out = tmp_path / "sub" / "shot.png"
    r = subprocess.run(
        [sys.executable, CAPTURE, "--out", str(out), "--frame", "1234", "--timeout", "20", *extra,
         "--", sys.executable, str(game), str(argv_file)],
        capture_output=True, text=True, timeout=60, cwd=ROOT)
    return r, out, argv_file


def test_capture_copies_the_scheduled_frame_to_out(tmp_path):
    r, out, argv_file = run_capture(tmp_path, "--renderer", "emulated")
    assert r.returncode == 0, r.stdout + r.stderr
    assert out.read_bytes().startswith(b"\x89PNG")
    argv = argv_file.read_text().split()
    assert "--parity_capture_frames=1234" in argv
    assert any(a.startswith("--input_json_file=") and a.endswith("config/song_script.json") for a in argv)
    assert "--gh3_renderer=native" not in argv


def test_capture_native_passes_renderer_flag(tmp_path):
    r, out, argv_file = run_capture(tmp_path, "--renderer", "native")
    assert r.returncode == 0, r.stdout + r.stderr
    assert "--gh3_renderer=native" in argv_file.read_text().split()


def test_capture_extra_cvars_after_dashdash_go_to_the_default_command(tmp_path):
    """`-- --cvar=x` (só flags) vai para o comando padrão; o resto de `--` segue sendo comando próprio."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("gameplay_capture", CAPTURE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    assert mod.split_cmd(["--gh3_native_msaa_as_samples=false"]) == ([], ["--gh3_native_msaa_as_samples=false"])
    assert mod.split_cmd(["python3", "x.py", "--a=1"]) == (["python3", "x.py", "--a=1"], [])
    assert mod.split_cmd([]) == ([], [])


def test_capture_fails_when_no_frame_arrives(tmp_path):
    game = tmp_path / "idle.py"
    game.write_text("import time\ntime.sleep(60)\n")
    out = tmp_path / "none.png"
    r = subprocess.run(
        [sys.executable, CAPTURE, "--out", str(out), "--frame", "5", "--timeout", "2", "--renderer", "emulated",
         "--", sys.executable, str(game)],
        capture_output=True, text=True, timeout=60, cwd=ROOT)
    assert r.returncode == 1
    assert not out.exists()


def test_capture_fails_fast_when_game_exits(tmp_path):
    game = tmp_path / "dies.py"
    game.write_text("import sys\nsys.exit(3)\n")
    out = tmp_path / "none.png"
    r = subprocess.run(
        [sys.executable, CAPTURE, "--out", str(out), "--frame", "5", "--timeout", "30", "--renderer", "emulated",
         "--", sys.executable, str(game)],
        capture_output=True, text=True, timeout=20, cwd=ROOT)
    assert r.returncode == 1
    assert "saiu" in r.stdout


# ---------------------------------------------------------------- ligação no app


def test_app_wires_script_input_and_parity_capture():
    app = (ROOT / "src/gh3_app.h").read_text()
    assert '#include "rexglue_script_input.h"' in app
    assert '#include "parity_capture.h"' in app
    assert "rexglue_script_input::Install(config" in app
    assert "parity_capture_.Start(runtime())" in app
    assert "parity_capture_.Stop()" in app
    assert "present_counter" in app


def test_parity_frame_index_defaults_to_counted_presents():
    # O GH3 não tem o contador de swap do THPS em 0x82970A10: o índice de quadro é a contagem das
    # linhas "XELOG_GPU PRESENT" reais (as mesmas que o tools/boot_test.py conta).
    src = (ROOT / "src/parity_capture.h").read_text()
    assert re.search(r'REXCVAR_DEFINE_STRING\(parity_frame_counter, "present"', src)
    assert '"0x82970A10"' not in src


PRESENT_MAIN = r"""
#include "present_counter.h"
#include <cstdio>
int main() {
  const char* lines[] = {
    "XELOG_GPU PRESENT: swap_texture_view=0x1 packet_size=1280x720",
    "XELOG_GPU PRESENT native",
    "XELOG_GPU PRESENT: NO PRESENTER",
    "XELOG_GPU PRESENT: BeginSubmission FAILED",
    "XELOG_GPU PRESENT: swap_texture_view=NULL",
    "something else",
  };
  for (const char* l : lines) std::printf("%d", gh3::present_counter::IsPresent(l) ? 1 : 0);
  for (const char* l : lines) gh3::present_counter::Observe(l);
  std::printf(" %llu", (unsigned long long)gh3::present_counter::Count());
}
"""


def test_present_counter_counts_only_real_presents(tmp_path):
    (tmp_path / "main.cpp").write_text(PRESENT_MAIN)
    exe = tmp_path / "pc"
    subprocess.run(["c++", "-std=c++20", f"-I{ROOT / 'src'}", str(tmp_path / "main.cpp"), "-o", str(exe)], check=True)
    assert subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout == "110000 2"
