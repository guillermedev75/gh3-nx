import shutil, subprocess

import pytest

# Controlador dinâmico da escala da cena (T31): a parte pura (níveis, histerese, tempo no nível) em
# src/native/native_scene_dynamic.h, compilada sozinha, sem SDK nem GPU.
MAIN = r"""
#include "native_scene_dynamic.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace gh3::native::scene;
int main(int argc, char** argv) {
  double start = std::atof(argv[2]);
  DynamicScale d(DynamicConfig(), start);
  if (!std::strcmp(argv[1], "init")) { std::printf("%.2f %d\n", d.scale(), d.level()); return 0; }
  if (!std::strcmp(argv[1], "run")) {
    int changes = 0;
    double last = d.scale();
    for (int i = 3; i < argc; ++i) {
      double s = d.Update(std::atof(argv[i]));
      if (s != last) { ++changes; last = s; }
    }
    std::printf("%.2f %d\n", d.scale(), changes);
    return 0;
  }
  return 2;
}
"""


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("scene_dynamic")
    inc = tmp / "inc"; inc.mkdir()
    shutil.copy("src/native/native_scene_dynamic.h", inc)
    (tmp / "main.cpp").write_text(MAIN)
    out = tmp / "dynamic"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp / "main.cpp"), "-o", str(out)], check=True)
    return out


def run(exe, op, start, *frames):
    r = subprocess.run([str(exe), op, str(start), *map(str, frames)], capture_output=True, text=True,
                       check=True)
    return [float(v) if "." in v else int(v) for v in r.stdout.split()]


def feed(ms, n):
    return [ms] * n


def test_start_is_snapped_to_the_005_grid_and_clamped(exe):
    assert run(exe, "init", 0.65) == [0.65, 13]
    assert run(exe, "init", 1.0) == [1.0, 20]
    assert run(exe, "init", 0.73) == [0.75, 15]
    assert run(exe, "init", 0.98) == [1.0, 20]
    assert run(exe, "init", 0.4) == [0.65, 13]   # piso do controlador
    assert run(exe, "init", 1.4) == [1.0, 20]    # teto


def test_three_over_frames_are_not_enough_inside_the_hold_time(exe):
    # 3 quadros de 40 ms = 0,12 s: a histerese já pediu, mas o nível ainda não pode mudar.
    assert run(exe, "run", 0.8, *feed(40, 3)) == [0.8, 0]


def test_a_short_spike_that_is_not_three_in_a_row_does_not_change_the_scale(exe):
    # 2 quadros estourados, 1 normal (zera a contagem), repetido por mais de 1 s.
    pattern = [40, 40, 16.7] * 30
    assert run(exe, "run", 0.8, *pattern) == [0.8, 0]


def test_sustained_over_budget_steps_down_005_per_hold_window(exe):
    assert run(exe, "run", 1.0, *feed(25, 25)) == [0.95, 1]   # 25 quadros = 0,625 s
    assert run(exe, "run", 1.0, *feed(25, 50)) == [0.9, 2]    # duas janelas de 0,6 s


def test_sustained_headroom_steps_up_005_per_hold_window(exe):
    assert run(exe, "run", 0.65, *feed(10, 61)) == [0.7, 1]   # 0,61 s abaixo de 15,2 ms
    assert run(exe, "run", 0.65, *feed(10, 121)) == [0.75, 2]


def test_steps_stop_at_the_floor_and_the_ceiling(exe):
    assert run(exe, "run", 0.65, *feed(40, 200)) == [0.65, 0]
    assert run(exe, "run", 1.0, *feed(5, 200)) == [1.0, 0]


def test_neutral_frames_reset_the_hysteresis_but_keep_the_hold_clock(exe):
    # 3 estourados (count 3) + 1 normal (zera) + 2 estourados: só 2 seguidos, não muda aos 0,6 s.
    frames = feed(40, 3) + [16.7] + feed(40, 2)
    frames += [16.7] * 40  # tempo total > 0,6 s, sempre neutralizando
    assert run(exe, "run", 0.8, *frames) == [0.8, 0]


def test_invalid_frame_times_do_not_move_the_controller(exe):
    assert run(exe, "run", 0.8, 0, -5, 5000, 0) == [0.8, 0]


def test_renderer_wires_the_controller_and_logs_the_changes():
    src = open("src/native/native_renderer.cpp").read()
    assert 'gh3_native_scene_dynamic, false' in src  # padrão false: Mac e paridade iguais
    assert "native_scene_dynamic.h" in src
    assert '"[native] scene dynamic: enabled' in src
    assert '"[native] scene dynamic: {:g} -> {:g}' in src
    toml = open("switch/gh3.toml").read()
    assert "gh3_native_scene_dynamic = true" in toml
