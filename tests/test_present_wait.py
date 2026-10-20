"""F3.1 do plano 60 FPS: espera de fence do presenter medida por 10 s.

O presenter do SDK (`vulkan_presenter.cpp`) espera fences do próprio anel de paint e do refresh
do guest output. O patch local `patches/rexglue-local/0002-present-fence-wait.patch` acumula só
esses fences (trackers marcados pelo presenter), e o runtime lê e zera a janela a cada 10 s,
logando `present fence wait X ms/10 s`. A formatação é pura e compila sem SDK nem GPU.
"""
import pathlib
import shutil
import subprocess

RAIZ = pathlib.Path(__file__).resolve().parent.parent
PATCH = RAIZ / "patches" / "rexglue-local" / "0002-present-fence-wait.patch"

MAIN = r"""
#include "native_present_wait.h"
#include <cstdlib>
#include <iostream>
using namespace gh3::native::present_wait;
int main(int argc, char** argv) {
  Window w;
  w.wait_ns = std::strtoull(argv[1], nullptr, 10);
  w.count = std::strtoull(argv[2], nullptr, 10);
  w.frames = std::strtoull(argv[3], nullptr, 10);
  std::cout << Format(w) << "\n";
}
"""


def build(tmp_path):
    inc = tmp_path / "inc"
    inc.mkdir()
    shutil.copy(RAIZ / "src" / "native" / "native_present_wait.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "present_wait"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)],
                   check=True)
    return exe


def run(exe, wait_ns, count, frames):
    result = subprocess.run([str(exe), str(wait_ns), str(count), str(frames)],
                            capture_output=True, text=True, check=True)
    return result.stdout.strip()


def test_wait_is_reported_in_milliseconds_per_10_seconds(tmp_path):
    line = run(build(tmp_path), 12_345_678, 3, 300)
    assert line.startswith("present fence wait 12.3 ms/10 s")


def test_count_average_and_frames_are_shown(tmp_path):
    line = run(build(tmp_path), 12_345_678, 3, 300)
    assert "(3 waits, 4.1 ms each, 300 frames)" in line


def test_no_wait_in_the_window_is_zero_not_missing(tmp_path):
    line = run(build(tmp_path), 0, 0, 120)
    assert line == "present fence wait 0.0 ms/10 s (0 waits, 120 frames)"


def test_wait_above_a_second_is_not_truncated(tmp_path):
    line = run(build(tmp_path), 1_234_567_890, 60, 600)
    assert "1234.6 ms/10 s" in line
    assert "20.6 ms each" in line


def test_local_series_applies_the_presenter_fence_instrumentation():
    series = (RAIZ / "patches" / "rexglue-local" / "series").read_text(encoding="utf-8")
    assert "0002-present-fence-wait.patch" in series.split()
    texto = PATCH.read_text(encoding="utf-8")
    # Só os fences dos trackers do presenter entram na conta.
    assert "SetPresentFenceWaitAccounting" in texto
    assert "vkWaitForFences" in texto
    assert "AddPresentFenceWaitNs" in texto
    # A janela é lida e zerada de uma vez a cada relatório.
    assert ".exchange(0" in texto
    assert "paint_context_.submission_tracker" in texto
    assert "guest_output_image_refresher_submission_tracker_" in texto


def test_sdk_tree_has_the_instrumentation_applied():
    tracker = (RAIZ / "sdk" / "src" / "ui" / "vulkan"
               / "vulkan_submission_tracker.cpp").read_text(encoding="utf-8")
    presenter = (RAIZ / "sdk" / "src" / "ui" / "vulkan"
                 / "vulkan_presenter.cpp").read_text(encoding="utf-8")
    header = RAIZ / "sdk" / "include" / "rex" / "ui" / "vulkan" / "present_wait_stats.h"
    assert header.is_file()
    assert "AddPresentFenceWaitNs" in tracker
    assert "SetPresentFenceWaitAccounting(true)" in presenter
    assert "ConsumePresentFenceWaitNs" in presenter


def test_runtime_logs_the_wait_in_the_10s_report():
    src = (RAIZ / "src" / "native" / "gh3_native_system.cpp").read_text(encoding="utf-8")
    assert '#include "native_present_wait.h"' in src
    assert "ConsumePresentFenceWaitNs" in src
    assert "present_wait::Format" in src
    # Na mesma rotina que escreve as linhas "[native] last 10 s".
    relatorio = src[src.index("void Report()"):]
    assert "ConsumePresentFenceWaitNs" in relatorio
