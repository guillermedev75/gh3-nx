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
  if (argc > 9) {  // AliasBlit: pitch da fonte (já 1 amostra), altura da fonte, alvo 1x (w, h)
    AliasBlit b = AliasBlitExtent(msaa, pitch, std::atoi(argv[9]), std::atoi(argv[10]), std::atoi(argv[11]));
    std::printf("%d %u %u %u %u\n", int(b.valid), b.src_w, b.src_h, b.dst_w, b.dst_h);
    return 0;
  }
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


def alias_blit(exe, guest_msaa, src_pitch, src_h, dst_w, dst_h):
    args = [src_pitch, guest_msaa, 0, 0, 0, 0, 0, 0, src_h, dst_w, dst_h]
    return [int(v) for v in subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True,
                                           check=True).stdout.split()]


def test_alias_blit_scales_one_sample_stage_up_to_the_1x_target(tmp_path):
    # T15: palco 4x de pitch 640 desenhado com 1 amostra (640x1024) sobe 2x2 para o alvo 1x 1280x720,
    # cortado pela altura do destino (720/2 = 360 linhas da fonte)
    exe = build(tmp_path)
    assert alias_blit(exe, M4X, 640, 1024, 1280, 720) == [1, 640, 360, 1280, 720]
    # profundidade: 1328 linhas no destino, a fonte tem 672 -> 664
    assert alias_blit(exe, M4X, 640, 672, 1280, 1328) == [1, 640, 664, 1280, 1328]
    # fonte menor que o destino: não passa dela
    assert alias_blit(exe, M4X, 640, 100, 1280, 720) == [1, 640, 100, 1280, 200]


def test_alias_blit_2x_only_doubles_height_and_1x_has_nothing_to_alias(tmp_path):
    exe = build(tmp_path)
    assert alias_blit(exe, M2X, 1280, 400, 1280, 720) == [1, 1280, 360, 1280, 720]
    assert alias_blit(exe, M1X, 1280, 400, 1280, 720)[0] == 0
    # destino mais estreito que a fonte escalada: corta a largura
    assert alias_blit(exe, M4X, 640, 400, 1000, 720) == [1, 500, 360, 1000, 720]
