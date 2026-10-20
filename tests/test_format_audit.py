"""F1.4: auditoria de formatos (src/native/native_format_audit.h).

O log por alvo (F0.4) vira uma linha só com a conta do que custa: quantos alvos por VkFormat,
memória estimada (bytes/pixel x amostras), e os candidatos a corte do F1.4 — 16F, gamma, 64bpp e
profundidade de 24 bits do guest guardada em 8 bytes/pixel. Puro: compila sem SDK nem GPU.
"""
import pathlib
import shutil
import subprocess

MAIN = r"""
#include "native_format_audit.h"
#include <cstring>
#include <iostream>
using namespace gh3::native::format_audit;
using namespace gh3::native::format_log;

static void add(Audit& a, uint32_t vk, uint32_t guest, uint32_t w, uint32_t h, uint32_t samples,
                bool depth) {
  gh3::native::format_audit::Target t;
  t.vk_format = vk;
  t.guest_format = guest;
  t.width = w;
  t.height = h;
  t.samples = samples;
  t.depth = depth;
  a.Add(t);
}

int main(int argc, char** argv) {
  Audit a;
  const char* what = argv[1];
  if (!std::strcmp(what, "basic")) {
    add(a, kR8G8B8A8Unorm, 0, 1280, 720, 1, false);
    add(a, kR8G8B8A8Unorm, 0, 320, 192, 1, false);
    add(a, kD32SfloatS8Uint, 0, 1280, 720, 1, true);
  } else if (!std::strcmp(what, "plain")) {
    add(a, kR8G8B8A8Unorm, 0, 1280, 720, 1, false);
    add(a, kD24UnormS8Uint, 0, 1280, 720, 1, true);
  } else if (!std::strcmp(what, "float16")) {
    add(a, kR16G16B16A16Sfloat, 7, 208, 224, 4, false);
    add(a, kR8G8B8A8Unorm, 0, 1280, 720, 1, false);
  } else if (!std::strcmp(what, "gamma64")) {
    add(a, kR8G8B8A8Unorm, 1, 1280, 720, 1, false);
    add(a, kR16G16B16A16Snorm, 5, 640, 360, 1, false);
  } else if (!std::strcmp(what, "depth24")) {
    add(a, kD32SfloatS8Uint, 0, 1280, 720, 1, true);
    add(a, kD24UnormS8Uint, 0, 1280, 720, 1, true);
    add(a, kD32Sfloat, 1, 160, 160, 1, true);
  } else if (!std::strcmp(what, "memory")) {
    add(a, kR8G8B8A8Unorm, 0, 100, 100, 1, false);
    add(a, kR16G16B16A16Sfloat, 7, 100, 100, 4, false);
  } else if (!std::strcmp(what, "empty")) {
    std::cout << "[" << a.Report(9) << "]\n";
    return 0;
  } else if (!std::strcmp(what, "signature")) {
    add(a, kR8G8B8A8Unorm, 0, 1280, 720, 1, false);
    uint64_t first = a.Signature();
    add(a, kR8G8B8A8Unorm, 0, 320, 192, 1, false);
    std::cout << "same:" << (first == a.Signature() ? 1 : 0);
    add(a, kD32SfloatS8Uint, 0, 1280, 720, 1, true);
    std::cout << " changed:" << (first != a.Signature() ? 1 : 0) << "\n";
    return 0;
  }
  std::cout << a.Report(345) << "\n";
}
"""


def build(tmp_path):
    inc = tmp_path / "inc"
    inc.mkdir()
    for header in ("native_format_audit.h", "native_format_log.h"):
        shutil.copy(pathlib.Path("src/native") / header, inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "format_audit"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)],
                   check=True)
    return exe


def run(exe, what):
    result = subprocess.run([str(exe), what], capture_output=True, text=True, check=True)
    return result.stdout.strip()


def test_report_is_one_line_with_counts_per_format(tmp_path):
    line = run(build(tmp_path), "basic")
    assert line.startswith("format audit: frame=345 ")
    assert "\n" not in line
    assert "targets:3" in line
    assert "color:2" in line
    assert "depth:1" in line
    assert "VK_FORMAT_R8G8B8A8_UNORM=2" in line
    assert "VK_FORMAT_D32_SFLOAT_S8_UINT=1" in line


def test_memory_estimate_uses_bytes_per_pixel_and_sample_count(tmp_path):
    line = run(build(tmp_path), "memory")
    # 100x100 RGBA8 (4 B) + 100x100 RGBA16F 4x (8 B x 4) = 40 000 + 320 000 bytes.
    assert "bytes:360000" in line
    assert "memory:0.3MiB" in line


def test_16f_target_is_flagged_as_expensive(tmp_path):
    line = run(build(tmp_path), "float16")
    assert "16f:1" in line
    assert "expensive:16f" in line
    assert "VK_FORMAT_R16G16B16A16_SFLOAT=1" in line


def test_gamma_and_64bpp_guests_are_counted(tmp_path):
    line = run(build(tmp_path), "gamma64")
    assert "gamma:1" in line
    assert "bpp64:1" in line
    assert "expensive:gamma,bpp64" in line


def test_24_bit_depth_stored_wide_is_the_depth24_candidate(tmp_path):
    line = run(build(tmp_path), "depth24")
    assert "depth_guest24:3" in line
    # D32_SFLOAT_S8_UINT (8 B) e guarda o guest de 24 bits; D24 (4 B) e D32_SFLOAT (4 B) nao.
    assert "depth_stored_wide:1" in line
    # 1280x720 D32_SFLOAT_S8_UINT = 8 B/px; o gh3_native_depth24 fica com 4 B/px (-3,5 MiB).
    assert "depth24_save:3.5MiB" in line
    assert "expensive:depth24-storage" in line
    assert "gh3_native_depth24" in line


def test_no_expensive_format_says_none(tmp_path):
    line = run(build(tmp_path), "plain")
    assert "expensive:none" in line


def test_empty_audit_reports_nothing(tmp_path):
    assert run(build(tmp_path), "empty") == "[]"


def test_signature_changes_only_when_a_new_format_appears(tmp_path):
    assert run(build(tmp_path), "signature") == "same:1 changed:1"


def test_renderer_logs_the_audit_at_most_once_per_format_set():
    src = pathlib.Path("src/native/native_renderer.cpp").read_text()
    assert '#include "native_format_audit.h"' in src
    assert "format_audit::Audit" in src
    assert 'REXLOG_INFO("[native] {}", format_audit.Report(frame))' in src
    assert "format_audit_logged_targets" in src
