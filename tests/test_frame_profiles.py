import re
import shutil
import subprocess

import pytest

# Trava de quadro por modo (2026-10-23). Num jogo de ritmo, quadro irregular pesa mais que FPS: o port do Mark
# (sa2-nx, travado em 30) tem 95,5% dos quadros em 33 ms cravados e é mais jogável que o nosso solto entre 30 e
# 40. No portátil, 30 cravados; no dock (GPU 768 MHz), 60. Cena a 1.0 (a 0.65 ficou mais cara no Switch).

MAIN = r"""
#include "frame_cap.h"
#include "native_scene_dynamic.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc, char** argv) {
  if (!std::strcmp(argv[1], "cap")) {
    std::printf("%u\n", gh3::frame_cap::Vblanks(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]),
                                                std::atoi(argv[5])));
    return 0;
  }
  std::printf("%d\n", int(gh3::native::scene::DynamicUsable(std::atoi(argv[2]))));
  return 0;
}
"""


def keys(path):
    return dict(re.findall(r"^([a-z_0-9]+) = (.+)$", open(path).read(), re.M))


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("cap")
    inc = tmp / "inc"; inc.mkdir()
    shutil.copy("src/frame_cap.h", inc)
    shutil.copy("src/native/native_scene_dynamic.h", inc)
    (tmp / "m.cpp").write_text(MAIN)
    out = tmp / "m"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp / "m.cpp"), "-o", str(out)], check=True)
    return out


def run(exe, *a):
    return subprocess.run([str(exe), *map(str, a)], capture_output=True, text=True, check=True).stdout.strip()


def test_handheld_30_docked_60(exe):
    assert run(exe, "cap", 0, 2, 1, 1) == "2"   # portátil: 30 cravados
    assert run(exe, "cap", 1, 2, 1, 1) == "1"   # dock: 60


def test_global_cap_is_a_floor_and_zero_means_one(exe):
    assert run(exe, "cap", 1, 2, 1, 2) == "2"   # gh3_min_frame_vblanks = 2 vale também no dock
    assert run(exe, "cap", 1, 0, 0, 0) == "1"


def test_switch_config_caps_handheld_at_30_and_docked_at_60_with_scene_at_065():
    k = keys("switch/gh3.toml")
    assert k["gh3_frame_vblanks_handheld"] == "2" and k["gh3_frame_vblanks_docked"] == "1"
    assert k["gh3_pace_before_present"] == "true"
    assert k.get("gh3_min_frame_vblanks", "1") == "1"
    # cena a 1.0: a 0.65 a GPU do portátil foi a ~23 ms contra 13,5 ms (medido, 2026-10-23)
    assert k["gh3_native_scene_scale"] == "1.0" and k["gh3_native_scene_dynamic"] == "false"
    assert k["gh3_native_scene_upscale"] == '"fsr1"'


def test_native_system_reads_the_mode_and_applies_the_cap():
    src = open("src/native/gh3_native_system.cpp").read()
    assert "gh3::frame_cap::Vblanks(" in src
    assert "appletGetOperationMode()" in src and "AppletOperationMode_Console" in src
    assert "frame cap:" in src


def test_dynamic_scale_is_off_under_a_frame_cap(exe):
    # O controlador mede o intervalo entre swaps; com a trava ele fica sempre no alvo e a escala desceria à toa.
    assert run(exe, "dyn", 1) == "1" and run(exe, "dyn", 0) == "1"
    assert run(exe, "dyn", 2) == "0"


def test_renderer_turns_the_controller_off_under_the_cap():
    src = open("src/native/native_renderer.cpp").read()
    assert "scene::DynamicUsable(REXCVAR_GET(gh3_min_frame_vblanks))" in src
    assert "scene dynamic off: frame cap" in src
