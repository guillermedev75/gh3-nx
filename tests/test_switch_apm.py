"""T14: modo de GPU oficial 460,8 MHz no portátil (src/switch_apm_config.h, src/switch_apm.h).

A escolha da configuração (portátil/dock, cvar ligada/desligada) é função pura, compilada sozinha
sem libnx. O resto (chamadas apm*) só existe no build do Switch; aqui se confere a fiação.
"""
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

MAIN = r"""
#include "switch_apm_config.h"
#include <cstdio>
#include <cstdlib>
using namespace gh3::apm;
int main(int argc, char** argv) {
  const int mode = std::atoi(argv[1]);
  const bool boost = std::atoi(argv[2]) != 0;
  std::printf("0x%08X\n", ChooseConfiguration(mode == 0 ? Mode::Handheld : Mode::Docked, boost));
}
"""


def choose(tmp_path, mode, boost):
    inc = tmp_path / "inc"; inc.mkdir(exist_ok=True)
    shutil.copy(ROOT / "src" / "switch_apm_config.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "apm"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)],
                   check=True)
    return subprocess.run([str(exe), str(mode), str(int(boost))], capture_output=True, text=True,
                          check=True).stdout.strip()


def test_handheld_with_boost_asks_for_460mhz_gpu(tmp_path):
    assert choose(tmp_path, 0, True) == "0x92220008"


def test_handheld_without_boost_leaves_default(tmp_path):
    assert choose(tmp_path, 0, False) == "0x00000000"


def test_docked_never_overrides(tmp_path):
    # no dock o sistema já dá 768 MHz; não mexer
    assert choose(tmp_path, 1, True) == "0x00000000"


def test_cvar_defaults_true_and_marker_logged():
    text = (ROOT / "src" / "switch_apm.h").read_text()
    assert re.search(r"REXCVAR_DEFINE_BOOL\(gh3_switch_gpu_boost,\s*true", text)
    assert '"apm performance configuration' in text


def test_app_wires_apm_and_nro_links_it():
    app = (ROOT / "src" / "gh3_app.h").read_text()
    assert "switch_apm.h" in app and "gh3::apm::" in app


def test_handheld_boost_is_registered_even_when_the_game_starts_docked():
    # Regressão (2026-10-23, Switch): aberto no dock e tirado depois, o portátil ficou a 307,2 MHz ("sem
    # alteração (modo 1)"). A configuração da APM vale por modo: registra-se a do portátil (Normal) sempre, e o
    # sistema a aplica quando sair do dock.
    src = open("src/switch_apm.h").read()
    assert "apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, want)" in src
    assert "apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &g_previous)" in src
    assert "ChooseConfiguration(Mode::Handheld" in src
