"""F0.4: auditoria de formato (src/native/native_format_log.h).

Uma linha por alvo com o VkFormat escolhido, o formato do guest, 16F, gamma, profundidade e o
clear inicial. A formatação é pura e compila sem SDK nem GPU.
"""
import pathlib
import shutil
import subprocess

MAIN = r"""
#include "native_format_log.h"
#include <cstring>
#include <iostream>
using namespace gh3::native::format_log;
int main(int argc, char** argv) {
  Target t;
  if (!std::strcmp(argv[1], "color")) {
    t.frame = 1234;
    t.base = 0xA0000000u;
    t.pitch = 1280;
    t.width = 1280; t.height = 720;
    t.samples = 1;
    t.vk_format = kR8G8B8A8Unorm;
    t.guest_format = 6;
    t.gamma = true;
    t.clear_color = true;
  } else if (!std::strcmp(argv[1], "float16")) {
    t.frame = 50;
    t.base = 0x0E00F000u;
    t.pitch = 320;
    t.width = 208; t.height = 224;
    t.samples = 4;
    t.vk_format = kR16G16B16A16Sfloat;
    t.guest_format = 8;
    t.half_float = true;
    t.clear_color = true;
    t.scene_scaled = true;
  } else if (!std::strcmp(argv[1], "depth")) {
    t.frame = 50;
    t.base = 0xB0000000u;
    t.pitch = 1280;
    t.width = 1280; t.height = 720;
    t.samples = 1;
    t.depth = true;
    t.vk_format = kD24UnormS8Uint;
    t.guest_format = 1;
    t.clear_depth = true;
  } else if (!std::strcmp(argv[1], "alias")) {
    t.vk_format = kD32SfloatS8Uint;
    t.depth = true;
    t.alias_msaa = 2;
  } else if (!std::strcmp(argv[1], "unknown")) {
    t.vk_format = 4242;
  } else if (!std::strcmp(argv[1], "name")) {
    std::cout << Name(static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10))) << "\n";
    return 0;
  }
  std::cout << Format(t) << "\n";
}
"""


def build(tmp_path):
    inc = tmp_path / "inc"
    inc.mkdir()
    shutil.copy("src/native/native_format_log.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "format_log"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)],
                   check=True)
    return exe


def run(exe, *args):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True,
                            check=True)
    return result.stdout.strip()


def test_one_line_per_target_with_vkformat_and_guest_format(tmp_path):
    line = run(build(tmp_path), "color")
    assert line.startswith("format: ")
    assert "\n" not in line
    for field in ("frame=1234", "color", "base:A0000000", "pitch:1280", "1280x720",
                  "samples:1", "guest:6", "vk:VK_FORMAT_R8G8B8A8_UNORM"):
        assert field in line, field


def test_gamma_target_is_flagged_and_cleared_to_color_zero(tmp_path):
    line = run(build(tmp_path), "color")
    assert "gamma:1" in line
    assert "hdr16f:0" in line
    assert "clear:color=0" in line


def test_16f_target_is_flagged_with_its_vkformat(tmp_path):
    line = run(build(tmp_path), "float16")
    assert "vk:VK_FORMAT_R16G16B16A16_SFLOAT" in line
    assert "hdr16f:1" in line
    assert "gamma:0" in line
    assert "samples:4" in line
    assert "208x224" in line
    assert "scene:1" in line


def test_depth_target_shows_depth_vkformat_and_clear_value(tmp_path):
    line = run(build(tmp_path), "depth")
    assert " depth " in line
    assert "vk:VK_FORMAT_D24_UNORM_S8_UINT" in line
    assert "clear:depth=1.0" in line
    assert "hdr16f:0" in line


def test_alias_sample_count_and_no_clear(tmp_path):
    line = run(build(tmp_path), "alias")
    assert "vk:VK_FORMAT_D32_SFLOAT_S8_UINT" in line
    assert "alias:2" in line
    assert "clear:none" in line


def test_unknown_format_still_prints_vk_format(tmp_path):
    line = run(build(tmp_path), "unknown")
    assert "VK_FORMAT_" in line
    assert "VK_FORMAT_UNKNOWN_4242" in line


def test_name_maps_every_format_the_renderer_uses(tmp_path):
    exe = build(tmp_path)
    expected = {
        37: "VK_FORMAT_R8G8B8A8_UNORM",
        64: "VK_FORMAT_A2B10G10R10_UNORM_PACK32",
        78: "VK_FORMAT_R16G16_SNORM",
        83: "VK_FORMAT_R16G16_SFLOAT",
        92: "VK_FORMAT_R16G16B16A16_SNORM",
        97: "VK_FORMAT_R16G16B16A16_SFLOAT",
        100: "VK_FORMAT_R32_SFLOAT",
        103: "VK_FORMAT_R32G32_SFLOAT",
        126: "VK_FORMAT_D32_SFLOAT",
        129: "VK_FORMAT_D24_UNORM_S8_UINT",
        130: "VK_FORMAT_D32_SFLOAT_S8_UINT",
    }
    for value, name in expected.items():
        assert run(exe, "name", value) == name


def test_renderer_wires_the_format_log_cvar_once_per_target():
    # O log de verdade sai do CreateOrGetRenderTarget, só com a cvar ligada (padrão false).
    src = pathlib.Path("src/native/native_renderer.cpp").read_text()
    assert 'REXCVAR_DEFINE_BOOL(gh3_native_format_log, false' in src
    assert "if (REXCVAR_GET(gh3_native_format_log))" in src
    assert 'REXLOG_INFO("[native] {}", format_log::Format(entry))' in src
    assert '#include "native_format_log.h"' in src
