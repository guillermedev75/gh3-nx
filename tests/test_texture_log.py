"""Rastreamento de texturas (src/native/native_texture_log.h).

Uma linha por textura ligada a um draw: endereco do guest, formato, tamanho, pitch, tiling, mips,
VkImage/View e se veio de um resolve (e de uma copia de tamanho exato). A formatacao e pura e
compila sem SDK nem GPU.
"""
import pathlib
import shutil
import subprocess

MAIN = r"""
#include "native_texture_log.h"
#include <cstring>
#include <iostream>
using namespace gh3::native::texture_log;
int main(int argc, char** argv) {
  Entry e;
  if (!std::strcmp(argv[1], "texture")) {
    e.frame = 400;
    e.draw = 87;
    e.stage = 1;
    e.slot = 3;
    e.address = 0x0A5C4000u;
    e.guest_format = 6;
    e.dimension = 1;
    e.bind_dimension = 2;
    e.signed_components = true;
    e.width = 1024; e.height = 512; e.layers = 1;
    e.pitch = 1024;
    e.mip_min = 0; e.mip_max = 3;
    e.tiled = true;
    e.packed_mips = true;
    e.view_id = 0x1122334455667788ull;
    e.image = 0x00000000DEADBEEFull;
    e.scissor_x = 0; e.scissor_y = 0; e.scissor_width = 1280; e.scissor_height = 720;
    e.pass = "pass h720 c=t0p1280 d=t720p1280";
  } else if (!std::strcmp(argv[1], "resolve_sized")) {
    e.frame = 1;
    e.draw = 2;
    e.stage = 0;
    e.slot = 0;
    e.address = 0x0BEEC000u;
    e.guest_format = 6;
    e.dimension = 1;
    e.width = 320; e.height = 180; e.layers = 1;
    e.pitch = 320;
    e.tiled = false;
    e.resolve = true;
    e.sized = true;
    e.resolve_width = 1280; e.resolve_height = 720;
    e.scissor_x = 12; e.scissor_y = 34; e.scissor_width = 56; e.scissor_height = 78;
    e.pass = "pass h720 c=t0p1280 d=-";
  } else if (!std::strcmp(argv[1], "resolve_exact")) {
    e.resolve = true;
    e.width = 256; e.height = 256;
    e.resolve_width = 256; e.resolve_height = 256;
  }
  std::cout << Format(e) << "\n";
}
"""


def build(tmp_path):
    inc = tmp_path / "inc"
    inc.mkdir()
    shutil.copy("src/native/native_texture_log.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "texture_log"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)],
                   check=True)
    return exe


def run(exe, *args):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True,
                            check=True)
    return result.stdout.strip()


def test_texture_line_has_address_format_extent_pitch_and_tiling(tmp_path):
    line = run(build(tmp_path), "texture")
    assert line == (
        "tex f=400 d=87 ps slot=3 addr=0A5C4000 fmt=6 dim=1 bind=3D/stacked signed=1 "
        "1024x512x1 pitch=1024 tiled=1 "
        "packed=1 mips=0..3 view=1122334455667788 image=00000000DEADBEEF src=texture "
        "scissor=0,0 1280x720 pass=pass h720 c=t0p1280 d=t720p1280"
    )


def test_resolve_destination_marks_sized_exact_size_copy(tmp_path):
    line = run(build(tmp_path), "resolve_sized")
    assert "src=resolve(1280x720 sized)" in line
    assert "addr=0BEEC000" in line
    assert "320x180x1 pitch=320 tiled=0" in line
    assert line.endswith("pass=pass h720 c=t0p1280 d=-")


def test_resolve_without_size_difference_is_not_marked_sized(tmp_path):
    line = run(build(tmp_path), "resolve_exact")
    assert "src=resolve(256x256)" in line
    assert "sized" not in line


def test_defaults_are_vertex_stage_and_no_view_or_image(tmp_path):
    exe = build(tmp_path)
    assert " ps " in run(exe, "texture")
    line = run(exe, "resolve_exact")
    assert " vs " in line
    assert "view=0000000000000000 image=0000000000000000" in line


def test_renderer_wires_the_texture_trace_with_a_frame_window_and_address_filter():
    src = pathlib.Path("src/native/native_renderer.cpp").read_text()
    assert 'REXCVAR_DEFINE_UINT32(gh3_native_texture_trace, 0' in src
    assert 'REXCVAR_DEFINE_UINT32(gh3_native_texture_trace_addr, 0' in src
    assert "const bool trace_window =" in src
    assert "texture_trace && frame >= texture_trace && frame < texture_trace + 2" in src
    assert "const bool trace_periodic = texture_trace_every && frame % texture_trace_every < 2;" in src
    assert 'REXCVAR_DEFINE_UINT32(gh3_native_texture_trace_every, 0' in src
    assert "if (!address || (trace_address && address != trace_address)) continue;" in src
    assert 'REXLOG_INFO("[native] {}", texture_log::Format(entry))' in src
    assert '#include "native_texture_log.h"' in src
