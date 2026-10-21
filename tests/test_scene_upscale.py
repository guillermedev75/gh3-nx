import shutil, subprocess

import pytest

# Upscaler da cena escalada (T47/F1.5): a parte pura (modo linear/cas/fsr1, constantes do EASU do
# FSR1 e do RCAS, retângulo NDC, proporção e formato) em src/native/native_scene_upscale.h,
# compilada sozinha, sem SDK nem GPU.
MAIN = r"""
#include "native_scene_upscale.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace gh3::native::scene;
int main(int argc, char** argv) {
  if (!std::strcmp(argv[1], "parse")) {
    std::printf("%d\n", int(ParseUpscaleMode(argv[2])));
    return 0;
  }
  if (!std::strcmp(argv[1], "easu")) {
    EasuParams p = EasuFor(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]),
                           std::atoi(argv[5]), std::atoi(argv[6]), std::atoi(argv[7]),
                           std::atoi(argv[8]), std::atoi(argv[9]));
    std::printf("%d %u %u %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n", int(p.valid), p.width,
                p.height, p.push.rect.offset[0], p.push.rect.offset[1], p.push.rect.size[0],
                p.push.rect.size[1], p.push.effect.input_output_size_ratio[0],
                p.push.effect.input_output_size_ratio[1], p.push.effect.input_size_inv[0],
                p.push.effect.input_size_inv[1]);
    return 0;
  }
  if (!std::strcmp(argv[1], "rcas")) {
    RcasParams p = RcasFor(std::atoi(argv[2]), std::atoi(argv[3]), std::atof(argv[4]));
    std::printf("%d %d %d %.6f\n", int(p.valid), p.push.effect.output_offset[0],
                p.push.effect.output_offset[1], p.push.effect.sharpness_post_setup);
    return 0;
  }
  if (!std::strcmp(argv[1], "compat")) {
    std::printf("%d\n", int(Fsr1Compatible(std::atoi(argv[2]), std::atoi(argv[3]),
                                           std::atoi(argv[4]), std::atoi(argv[5]))));
    return 0;
  }
  return 2;
}
"""


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("scene_upscale")
    inc = tmp / "inc"; inc.mkdir()
    shutil.copy("src/native/native_scene_upscale.h", inc)
    shutil.copy("src/native/native_scene_cas.h", inc)
    (tmp / "main.cpp").write_text(MAIN)
    out = tmp / "scene_upscale"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp / "main.cpp"), "-o", str(out)],
                   check=True)
    return out


def run(exe, *args):
    r = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True)
    return [float(v) if "." in v else int(v) for v in r.stdout.split()]


def approx(a, b, eps=1e-5):
    return abs(a - b) <= eps


def test_mode_parses_and_unknown_falls_back_to_linear(exe):
    assert run(exe, "parse", "linear") == [0]
    assert run(exe, "parse", "cas") == [1]
    assert run(exe, "parse", "fsr1") == [2]
    assert run(exe, "parse", "fsr2") == [0]   # desconhecido: nunca desliga a imagem
    assert run(exe, "parse", "") == [0]


def test_easu_params_match_the_fidelityfx_convention(exe):
    # 832x468 -> 1280x720 num alvo 1280x720 (0.65)
    p = run(exe, "easu", 832, 468, 832, 468, 1280, 720, 1280, 720)
    assert p[0] == 1 and p[1] == 1280 and p[2] == 720
    assert p[3] == -1 and p[4] == -1          # canto superior esquerdo em NDC
    assert approx(p[5], 2.0) and approx(p[6], 2.0)  # retângulo cobre o alvo inteiro
    assert approx(p[7], 0.65) and approx(p[8], 0.65)  # input_output_size_ratio
    assert approx(p[9], 1.0 / 832.0) and approx(p[10], 1.0 / 468.0)  # input_size_inv


def test_easu_normalizes_by_the_texture_not_the_region(exe):
    # Regressão (2026-10-20): a imagem escalada é 832x666 e a cena ocupa só 832x468 dela. O EASU lê
    # com UV normalizada, então input_size_inv tem de ser 1/tamanho da TEXTURA; com 1/468 a cena saía
    # comprimida na vertical em 468/666 ~ 0,70 e preta embaixo. A proporção continua sendo da região.
    p = run(exe, "easu", 832, 468, 832, 666, 1280, 720, 1280, 720)
    assert p[0] == 1 and p[1] == 1280 and p[2] == 720
    assert approx(p[7], 0.65) and approx(p[8], 0.65)
    assert approx(p[9], 1.0 / 832.0) and approx(p[10], 1.0 / 666.0)


def test_easu_rejects_a_region_larger_than_the_texture(exe):
    assert run(exe, "easu", 832, 700, 832, 666, 1280, 720, 1280, 720)[0] == 0


def test_easu_shrinks_with_a_shorter_target(exe):
    p = run(exe, "easu", 832, 390, 832, 390, 1280, 600, 1280, 720)
    assert p[0] == 1 and p[2] == 600
    assert approx(p[6], 600.0 * 2.0 / 720.0)
    assert approx(p[7], 0.65) and approx(p[8], 0.65)


def test_easu_with_an_empty_region_is_invalid(exe):
    assert run(exe, "easu", 0, 468, 832, 468, 1280, 720, 1280, 720) == [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]


def test_rcas_is_a_1to1_sharpen_with_exp2_stops(exe):
    # RCAS pós-setup = 2^-stops; stops 0 = 1.0 (sem redução).
    p = run(exe, "rcas", 1280, 720, 0.0)
    assert p[0] == 1 and p[1] == 0 and p[2] == 0 and approx(p[3], 1.0)
    assert run(exe, "rcas", 0, 720, 0.0) == [0, 0, 0, 0]  # região vazia


def test_fsr1_shares_the_cas_format_rule(exe):
    assert run(exe, "compat", 1, 0, 37, 37) == [1]
    assert run(exe, "compat", 0, 0, 37, 37) == [0]   # desligado
    assert run(exe, "compat", 1, 1, 37, 37) == [0]   # profundidade
    assert run(exe, "compat", 1, 0, 44, 44) == [0]   # R16G16B16A16_SFLOAT
    assert run(exe, "compat", 1, 0, 37, 44) == [0]   # formatos diferentes


def test_renderer_wires_the_selector_and_the_easu_shader():
    src = open("src/native/native_renderer.cpp").read()
    assert "gh3_native_scene_upscale" in src
    assert "native_scene_upscale.h" in src
    assert "guest_output_ffx_fsr_easu_ps" in src
    assert "EasuUpscale(" in src
    assert "scene upscale:" in src
    assert 'gh3_native_scene_upscale = "fsr1"' in open("switch/gh3.toml").read()
