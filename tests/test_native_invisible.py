"""F1.3: draws que nao podem mudar nenhum pixel (src/native/native_invisible.h).

Blend que copia o destino (0 x origem + 1 x destino, ADD) ou alpha test NEVER, sem escrita de
Z/stencil. A decisao e' pura e compila sem SDK (como o native_edram_samples.h).
"""
import shutil
import subprocess

MAIN = r"""
#include "native_invisible.h"
#include <cstdio>
#include <cstdlib>
using namespace gh3::native::invisible;
int main(int argc, char** argv) {
  const uint32_t mask = uint32_t(std::strtoul(argv[1], nullptr, 10));
  const bool alpha_never = std::atoi(argv[2]) != 0;
  const bool writes = std::atoi(argv[3]) != 0;
  Blend b[4] = {};
  for (int i = 0; i < 4; ++i) {
    int a = 4 + i * 6;
    b[i] = {uint32_t(std::atoi(argv[a])),     uint32_t(std::atoi(argv[a + 1])),
            uint32_t(std::atoi(argv[a + 2])), uint32_t(std::atoi(argv[a + 3])),
            uint32_t(std::atoi(argv[a + 4])), uint32_t(std::atoi(argv[a + 5]))};
  }
  std::printf("%d\n", DrawIsInvisible(mask, b, alpha_never, writes) ? 1 : 0);
}
"""

COPY = (0, 1, 0, 0, 1, 0)      # 0*src + 1*dst, ADD (todos os canais)
NORMAL = (4, 1, 0, 6, 1, 0)    # src color/alpha normais


def build(tmp_path):
    inc = tmp_path / "inc"; inc.mkdir()
    shutil.copy("src/native/native_invisible.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "inv"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)],
                   check=True)
    return exe


def run(exe, mask, alpha_never, writes, blends):
    args = [str(mask), str(int(alpha_never)), str(int(writes))]
    for b in blends:
        args += [str(v) for v in b]
    return int(subprocess.run([str(exe), *args], capture_output=True, text=True,
                              check=True).stdout.strip())


def test_blend_that_copies_destination_is_invisible(tmp_path):
    exe = build(tmp_path)
    assert run(exe, 0x000F, False, False, [COPY, NORMAL, NORMAL, NORMAL]) == 1


def test_one_real_color_target_makes_it_visible(tmp_path):
    exe = build(tmp_path)
    assert run(exe, 0x00FF, False, False, [COPY, NORMAL, NORMAL, NORMAL]) == 0


def test_alpha_never_is_invisible(tmp_path):
    exe = build(tmp_path)
    assert run(exe, 0x000F, True, False, [NORMAL, NORMAL, NORMAL, NORMAL]) == 1


def test_any_depth_or_stencil_write_keeps_it(tmp_path):
    exe = build(tmp_path)
    assert run(exe, 0x000F, True, True, [COPY, COPY, COPY, COPY]) == 0
    assert run(exe, 0x000F, False, True, [COPY, COPY, COPY, COPY]) == 0


def test_no_color_target_writes_is_invisible(tmp_path):
    exe = build(tmp_path)
    assert run(exe, 0x0000, False, False, [NORMAL, NORMAL, NORMAL, NORMAL]) == 1


def test_copy_needs_add_and_zero_one(tmp_path):
    exe = build(tmp_path)
    subtract = (0, 1, 1, 0, 1, 0)
    assert run(exe, 0x000F, False, False, [subtract, NORMAL, NORMAL, NORMAL]) == 0
    one_zero = (1, 0, 0, 0, 1, 0)
    assert run(exe, 0x000F, False, False, [one_zero, NORMAL, NORMAL, NORMAL]) == 0
