import shutil, subprocess

# Superfícies MSAA do guest na grade de amostras do EDRAM (src/native/native_edram_samples.h), compiladas
# sozinhas, sem SDK nem GPU. O GH3 limpa o alvo 1280x720 1x (pitch 1280) por um alias 4x de pitch 640
# no mesmo EDRAM; sem isso a cena nunca era limpa e o palco saía branco (T9).
MAIN = r"""
#include "native_edram_samples.h"
#include <cstdio>
#include <cstdlib>
using namespace gh3::native::edram;
int main(int argc, char** argv) {
  uint32_t pitch = std::atoi(argv[1]), msaa = std::atoi(argv[2]);
  bool as_samples = argv[3][0] == '1', host_msaa = argv[4][0] == '1';
  Surface s = HostSurface(pitch, msaa, as_samples, host_msaa);
  int32_t r[4] = {std::atoi(argv[5]), std::atoi(argv[6]), std::atoi(argv[7]), std::atoi(argv[8])};
  s.ScaleRect(r);
  std::printf("%u %u %u %u %d %d %d %d\n", s.pitch, s.msaa, s.scale_x, s.scale_y, r[0], r[1], r[2], r[3]);
}
"""

M1X, M2X, M4X = 0, 1, 2


def build(tmp_path):
    inc = tmp_path / "inc"; inc.mkdir()
    shutil.copy("src/native/native_edram_samples.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "edram"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)], check=True)
    return exe


def out(exe, pitch, msaa, as_samples, host_msaa, rect=(8, 16, 640, 360)):
    args = [pitch, msaa, int(as_samples), int(host_msaa), *rect]
    return [int(v) for v in subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True,
                                           check=True).stdout.split()]


def test_4x_alias_lands_on_the_1x_surface_with_twice_the_pitch(tmp_path):
    # pitch 640 com 4x = 1280x720 amostras = o alvo 1x de pitch 1280; o retângulo vai junto (2x2)
    exe = build(tmp_path)
    assert out(exe, 640, M4X, True, True) == [1280, 0, 2, 2, 16, 32, 1280, 720]
    assert out(exe, 640, M4X, True, True)[:2] == out(exe, 1280, M1X, True, True)[:2]


def test_2x_stacks_samples_vertically(tmp_path):
    exe = build(tmp_path)
    assert out(exe, 1280, M2X, True, True) == [1280, 0, 1, 2, 8, 32, 640, 720]


def test_single_sampled_surfaces_are_untouched(tmp_path):
    exe = build(tmp_path)
    assert out(exe, 1280, M1X, True, True) == [1280, 0, 1, 1, 8, 16, 640, 360]
    assert out(exe, 1280, M1X, False, True) == [1280, 0, 1, 1, 8, 16, 640, 360]


def test_without_samples_as_pixels_keeps_the_old_keys(tmp_path):
    # gh3_native_msaa_as_samples=false: MSAA de verdade (ou 1 amostra com gh3_native_msaa=false), pitch do guest
    exe = build(tmp_path)
    assert out(exe, 640, M4X, False, True) == [640, M4X, 1, 1, 8, 16, 640, 360]
    assert out(exe, 640, M4X, False, False) == [640, 0, 1, 1, 8, 16, 640, 360]
