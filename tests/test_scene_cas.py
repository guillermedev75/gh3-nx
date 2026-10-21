import shutil, subprocess

import pytest

# CAS no blit da cena escalada (T32/F1.1c): a parte pura (constantes do FidelityFX, retângulo NDC,
# proporção e formato) em src/native/native_scene_cas.h, compilada sozinha, sem SDK nem GPU.
MAIN = r"""
#include "native_scene_cas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace gh3::native::scene;
int main(int argc, char** argv) {
  if (!std::strcmp(argv[1], "clamp")) {
    float s = ClampCasSharpness(std::atof(argv[2]));
    std::printf("%g %d\n", s, int(CasEnabled(true, s)));
    return 0;
  }
  if (!std::strcmp(argv[1], "setup")) {
    std::printf("%.6f %.6f\n", CasSetupSharpness(std::atof(argv[2])),
                CasSetupSharpness(1.0f));
    return 0;
  }
  if (!std::strcmp(argv[1], "compat")) {
    std::printf("%d\n", int(CasCompatible(std::atoi(argv[2]), std::atoi(argv[3]),
                                         std::atoi(argv[4]), std::atoi(argv[5]))));
    return 0;
  }
  if (!std::strcmp(argv[1], "params")) {
    CasParams p = CasFor(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]),
                         std::atoi(argv[5]), std::atoi(argv[6]), std::atoi(argv[7]),
                         std::atof(argv[8]));
    std::printf("%d %u %u %.6f %.6f %.6f %.6f %d %d %.6f %.6f %.6f\n", int(p.valid), p.width,
                p.height, p.push.rect.offset[0], p.push.rect.offset[1], p.push.rect.size[0],
                p.push.rect.size[1], p.push.effect.output_offset[0],
                p.push.effect.output_offset[1], p.push.effect.input_output_ratio[0],
                p.push.effect.input_output_ratio[1], p.push.effect.sharpness_post_setup);
    return 0;
  }
  return 2;
}
"""


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("scene_cas")
    inc = tmp / "inc"; inc.mkdir()
    shutil.copy("src/native/native_scene_cas.h", inc)
    (tmp / "main.cpp").write_text(MAIN)
    out = tmp / "scene_cas"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp / "main.cpp"), "-o", str(out)],
                   check=True)
    return out


def run(exe, *args):
    r = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True)
    return [float(v) if "." in v else int(v) for v in r.stdout.split()]


def approx(a, b, eps=1e-5):
    return abs(a - b) <= eps


def test_sharpness_is_clamped_and_zero_disables(exe):
    assert run(exe, "clamp", -0.5) == [0, 0]
    assert run(exe, "clamp", 0.0) == [0, 0]
    assert run(exe, "clamp", 0.2) == [0.2, 1]
    assert run(exe, "clamp", 1.5) == [1, 1]


def test_setup_constant_matches_the_fidelityfx_presenter(exe):
    # presenter.h: -1 / (8 - 3 * sharpness)
    assert approx(run(exe, "setup", 0.0)[0], -1.0 / 8.0)
    assert approx(run(exe, "setup", 0.2)[0], -1.0 / 7.4)
    assert approx(run(exe, "setup", 1.0)[0], -1.0 / 5.0)


def test_only_opaque_r8g8b8a8_color_can_take_cas(exe):
    assert run(exe, "compat", 1, 0, 37, 37) == [1]
    assert run(exe, "compat", 0, 0, 37, 37) == [0]   # CAS desligado
    assert run(exe, "compat", 1, 1, 37, 37) == [0]   # profundidade
    assert run(exe, "compat", 1, 0, 44, 44) == [0]   # R16G16B16A16_SFLOAT
    assert run(exe, "compat", 1, 0, 37, 44) == [0]   # formatos diferentes


def test_params_cover_the_whole_720p_region_when_upscaling_065(exe):
    # 832x468 -> 1280x720 num alvo 1280x720
    p = run(exe, "params", 832, 468, 1280, 720, 1280, 720, 0.2)
    assert p[0] == 1 and p[1] == 1280 and p[2] == 720
    assert p[3] == -1 and p[4] == -1          # canto superior esquerdo em NDC
    assert approx(p[5], 2.0) and approx(p[6], 2.0)  # retângulo cobre o alvo inteiro
    assert p[7] == 0 and p[8] == 0            # output_offset (região na origem)
    assert approx(p[9], 0.65) and approx(p[10], 0.65)  # ratio origem/destino
    assert approx(p[11], -1.0 / 7.4)          # sharpness pós-setup


def test_params_shrink_the_rectangle_and_ratio_with_a_shorter_target(exe):
    # Alvo cheio com 600 linhas: região 1280x600 de um alvo de 720.
    p = run(exe, "params", 832, 390, 1280, 600, 1280, 720, 0.2)
    assert p[0] == 1 and p[1] == 1280 and p[2] == 600
    assert approx(p[5], 2.0) and approx(p[6], 600.0 * 2.0 / 720.0)
    assert approx(p[9], 0.65) and approx(p[10], 0.65)


def test_params_with_an_empty_region_are_invalid(exe):
    assert run(exe, "params", 0, 468, 1280, 720, 1280, 720, 0.2) == [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]


def test_renderer_wires_the_cvar_and_logs_at_startup():
    src = open("src/native/native_renderer.cpp").read()
    assert "gh3_native_scene_cas, false" in src  # padrão desligado: Mac/paridade iguais
    assert "native_scene_cas.h" in src
    assert "scene CAS: enabled" in src
    assert "CasUpscale(" in src
    # O T47 tornou o fsr1 o upscaler padrão do pacote; o cas continua uma opção da config.
    assert "gh3_native_scene_cas" in open("switch/gh3.toml").read()
