import shutil, subprocess

import pytest

# Cena 3D em resolução menor que a HUD (T16): a parte pura (tamanhos, scissor escalado, região do blit)
# em src/native/native_scene_scale.h, compilada sozinha, sem SDK nem GPU.
MAIN = r"""
#include "native_scene_scale.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace gh3::native::scene;
int main(int argc, char** argv) {
  float s = std::atof(argv[2]);
  if (!std::strcmp(argv[1], "clamp")) { std::printf("%g %d\n", ClampScale(s), int(Enabled(ClampScale(s)))); return 0; }
  if (!std::strcmp(argv[1], "size")) { std::printf("%u\n", ScaledSize(s, std::atoi(argv[3]))); return 0; }
  if (!std::strcmp(argv[1], "scissor")) {
    ScaledSpan r = ScaleSpan(s, std::atoi(argv[3]), std::atoi(argv[4]));
    std::printf("%u %u\n", r.offset, r.extent); return 0;
  }
  if (!std::strcmp(argv[1], "blit")) {
    SceneBlit b = BlitExtent(s, std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), std::atoi(argv[6]));
    std::printf("%d %u %u %u %u\n", int(b.valid), b.full_w, b.full_h, b.scaled_w, b.scaled_h); return 0;
  }
  if (!std::strcmp(argv[1], "qualifies")) {
    std::printf("%d\n", int(Qualifies(std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), std::atoi(argv[6])))); return 0;
  }
  return 2;
}
"""


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("scene")
    inc = tmp / "inc"; inc.mkdir()
    shutil.copy("src/native/native_scene_scale.h", inc)
    (tmp / "main.cpp").write_text(MAIN)
    out = tmp / "scene"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp / "main.cpp"), "-o", str(out)], check=True)
    return out


def run(exe, *args):
    r = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True)
    return [float(v) if "." in v else int(v) for v in r.stdout.split()]


def test_scale_is_clamped_and_one_means_off(exe):
    assert run(exe, "clamp", 1.0) == [1, 0]
    assert run(exe, "clamp", 1.5) == [1, 0]
    assert run(exe, "clamp", 0.75) == [0.75, 1]
    assert run(exe, "clamp", 0.1) == [0.25, 1]
    assert run(exe, "clamp", 0.0) == [1, 0]  # 0 = valor inválido, não escala


def test_720p_at_075_is_960x540(exe):
    assert run(exe, "size", 0.75, 1280) == [960]
    assert run(exe, "size", 0.75, 720) == [540]


def test_size_rounds_half_up_and_never_reaches_zero(exe):
    assert run(exe, "size", 0.75, 721) == [541]   # 540.75
    assert run(exe, "size", 0.5, 3) == [2]        # 1.5
    assert run(exe, "size", 0.25, 1) == [1]       # 0.25 -> mínimo 1
    assert run(exe, "size", 0.75, 0) == [0]


def test_scissor_grows_to_cover_the_scaled_edges(exe):
    # início para baixo, fim para cima: nenhum pixel que o jogo desenharia fica de fora
    assert run(exe, "scissor", 0.75, 0, 1280) == [0, 960]
    assert run(exe, "scissor", 0.75, 3, 5) == [2, 4]    # [3,8) -> [2.25,6.0) -> [2,6): extent 4
    assert run(exe, "scissor", 0.75, 100, 0) == [75, 0]


def test_blit_region_covers_the_scaled_image(exe):
    # imagem escalada 960x540, alvo cheio 1280x720
    assert run(exe, "blit", 0.75, 960, 540, 1280, 720) == [1, 1280, 720, 960, 540]


def test_blit_region_is_cut_by_the_smaller_full_target(exe):
    # alvo cheio mais baixo (600 linhas): a região escalada acompanha
    assert run(exe, "blit", 0.75, 960, 540, 1280, 600) == [1, 1280, 600, 960, 450]


def test_blit_with_empty_image_is_invalid(exe):
    assert run(exe, "blit", 0.75, 0, 540, 1280, 720)[0] == 0


def test_only_the_1280_single_sample_depth_scene_qualifies(exe):
    # Qualifies(pitch, depth_used, msaa, alias_msaa)
    assert run(exe, "qualifies", 0, 1280, 1, 0, 0) == [1]
    assert run(exe, "qualifies", 0, 1280, 0, 0, 0) == [0]  # HUD, sem profundidade
    assert run(exe, "qualifies", 0, 640, 1, 0, 0) == [0]   # outro alvo (bloom/sombra)
    assert run(exe, "qualifies", 0, 1280, 1, 2, 0) == [0]  # MSAA de host
    assert run(exe, "qualifies", 0, 1280, 1, 0, 2) == [0]  # imagem de alias da T15


def test_renderer_wires_the_cvar_and_logs_once():
    src = open("src/native/native_renderer.cpp").read()
    assert 'gh3_native_scene_scale, 1.0' in src  # padrão 1.0: Mac e paridade iguais
    assert '"scene scale {:g}: {}x{}"' in src or '[native] scene scale {:g}: {}x{}' in src
    assert "native_scene_scale.h" in src
    assert "gh3_native_scene_scale = 0.65" in open("switch/gh3.toml").read()



def test_stage_alias_reaches_the_scaled_scene_through_the_full_image():
    # Regressão (2026-10-21, Switch): msaa_as_samples=false + escala < 1 -> palco preto (só silhuetas e luzes).
    # O espelho do palco (T15) ia só para a imagem cheia, e depois da subida da cena escalada, apagando-a.
    # Agora: pedida a escalada, o espelho atualiza a cheia (e a marca como tocada) e a escala a desce; no
    # Draw() o espelho vem antes da escala.
    src = open("src/native/native_renderer.cpp").read()
    body = src[src.find("void Renderer::Impl::SyncAliasTarget("):]
    body = body[:body.find("\nvoid Renderer::Impl::", 10)]
    assert "if (target->key.scene_scaled) {" in body and "full_key.scene_scaled = 0;" in body
    assert "target->scene_touch = ++scene_clock;" in body
    draw = src[src.find("T15/T16: a cached setup does not go through GetRenderTarget again"):]
    assert draw.find("SyncAliasTarget(t);") < draw.find("SyncSceneTarget(t);")
    toml = open("switch/gh3.toml").read()
    assert "gh3_native_scene_scale = 0.65" in toml and "gh3_native_msaa_as_samples = false" in toml
