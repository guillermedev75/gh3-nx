import shutil, subprocess

# O medidor de FPS do overlay de debug (src/frame_stats.h): contador monotônico de quadros +
# tempo -> FPS suavizado. Compilado sozinho, sem SDK nem GPU (como o native_primitive_expand).
MAIN = r"""
#include "frame_stats.h"
#include <cstdio>
#include <cstdlib>
using namespace gh3::frame_stats;
int main(int argc, char** argv) {
  Sampler s;
  for (int i = 1; i < argc; i += 2) {
    uint64_t total = std::strtoull(argv[i], nullptr, 10);
    double now_ms = std::atof(argv[i + 1]);
    Stats st = s.Sample(total, now_ms);
    std::printf("%.3f %.3f %llu\n", st.fps, st.frame_time_ms,
                (unsigned long long)st.frame_count);
  }
}
"""


def build(tmp_path):
    inc = tmp_path / "inc"; inc.mkdir()
    shutil.copy("src/frame_stats.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "fps"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)], check=True)
    return exe


def run(exe, *pairs):
    return subprocess.run([str(exe), *[str(v) for p in pairs for v in p]],
                          capture_output=True, text=True, check=True).stdout.strip().splitlines()


def test_first_sample_only_starts_the_window(tmp_path):
    # Sem janela anterior: guarda o tempo/contador e não arrisca um FPS (frame_count nunca 0).
    exe = build(tmp_path)
    assert run(exe, (0, 0)) == ["0.000 0.000 1"]


def test_steady_60(tmp_path):
    # 60 quadros numa janela de 1 s -> 60 FPS, 16.667 ms/quadro.
    exe = build(tmp_path)
    assert run(exe, (0, 0), (60, 1000.0)) == ["0.000 0.000 1", "60.000 16.667 60"]


def test_window_shorter_than_a_second_is_ignored(tmp_path):
    # Só recomputa a cada >= 1 s; entre isso devolve a leitura anterior.
    exe = build(tmp_path)
    got = run(exe, (0, 0), (30, 999.0), (60, 1000.0))
    assert got[1] == "0.000 0.000 30"
    assert got[2] == "60.000 16.667 60"


def test_no_frames_in_window_reads_zero(tmp_path):
    # Nada apresentado na janela: FPS 0, sem divisão por zero; frame_time vira o dt.
    exe = build(tmp_path)
    got = run(exe, (5, 0), (5, 1500.0))
    assert got[1] == "0.000 1500.000 5"


def test_smooths_with_the_previous_window(tmp_path):
    # 2ª janela: 30 quadros em 1 s = 33.333 ms; média com os 16.667 ms anteriores -> 25 ms (40 FPS).
    exe = build(tmp_path)
    got = run(exe, (0, 0), (60, 1000.0), (90, 2000.0))
    assert got[2] == "40.000 25.000 90"
