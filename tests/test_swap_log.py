"""F0.2: log por swap (src/native/native_swap_log.h).

Uma linha por swap com frontbuffer, formato, clear do quadro, resolve origem/destino, chave do
render target, alias_seq e espera de fence. A formatação é pura e compila sem SDK nem GPU.
"""
import shutil
import subprocess

MAIN = r"""
#include "native_swap_log.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
using namespace gh3::native::swap_log;
int main(int argc, char** argv) {
  Entry e;
  if (!std::strcmp(argv[1], "full")) {
    e.frame = 1234;
    e.frontbuffer = 0xA0000000u;
    e.width = 1280; e.height = 720;
    e.format = 44;
    e.source_width = 1360; e.source_height = 768;
    e.has_clear_color = true; e.clear_color = 0x3F800000ull; e.clear_color_target = 0xA0000000u;
    e.has_clear_depth = true; e.clear_depth = 0x00FFFFFFu; e.clear_depth_target = 0xB0000000u;
    e.resolve_count = 2; e.has_resolve = true;
    e.resolve_src = 0xA0000000u; e.resolve_src_format = 44; e.resolve_dst = 0x1A2B3000u;
    e.has_rt = true; e.rt_base = 0xA0000000u; e.rt_pitch = 1280; e.rt_format = 44;
    e.rt_samples = 4; e.rt_depth = false;
    e.alias_seq = 17;
    e.fence_wait_ns = 1500000;
    e.presented = true;
  } else if (!std::strcmp(argv[1], "fence")) {
    e.fence_wait_ns = std::strtoull(argv[2], nullptr, 10);
  }
  std::cout << Format(e) << "\n";
}
"""


def build(tmp_path):
    inc = tmp_path / "inc"
    inc.mkdir()
    shutil.copy("src/native/native_swap_log.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "swap_log"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)],
                   check=True)
    return exe


def run(exe, *args):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True,
                            check=True)
    return result.stdout.strip()


def test_one_line_per_swap_with_frontbuffer_and_format(tmp_path):
    line = run(build(tmp_path), "full")
    assert line.startswith("swap: ")
    assert "\n" not in line
    for field in ("frame=1234", "fb=A0000000", "1280x720", "fmt:44", "src=1360x768"):
        assert field in line, field


def test_clear_shows_color_depth_and_their_targets(tmp_path):
    line = run(build(tmp_path), "full")
    assert "clear=3F800000@A0000000" in line
    assert "depth=00FFFFFF@B0000000" in line


def test_resolve_shows_source_format_and_destination(tmp_path):
    line = run(build(tmp_path), "full")
    assert "resolve=2 A0000000 fmt:44 -> 1A2B3000" in line


def test_render_target_key_and_alias_sequence(tmp_path):
    line = run(build(tmp_path), "full")
    assert "rt=base:A0000000 pitch:1280 fmt:44 samples:4 color" in line
    assert "alias:17" in line


def test_fence_wait_in_milliseconds(tmp_path):
    exe = build(tmp_path)
    assert "fence:1.500ms" in run(exe, "fence", 1500000)
    assert "fence:0.000ms" in run(exe, "fence", 0)


def test_nothing_known_is_logged_as_dash_not_garbage(tmp_path):
    line = run(build(tmp_path), "empty")
    assert "clear=-@00000000" in line
    assert "depth=-@00000000" in line
    assert "resolve=0 -" in line
    assert "rt=-" in line
    assert "presented:0" in line


def test_renderer_wires_the_swap_log_cvar():
    # O log de verdade sai do Swap do renderer, só com a cvar ligada (padrão false).
    import pathlib
    src = pathlib.Path("src/native/native_renderer.cpp").read_text()
    assert 'REXCVAR_DEFINE_BOOL(gh3_native_swap_log, false' in src
    assert "if (REXCVAR_GET(gh3_native_swap_log))" in src
    assert 'REXLOG_INFO("[native] {}", swap_log::Format(entry))' in src
