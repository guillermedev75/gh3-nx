import shutil, subprocess

import pytest

# Resolve de profundidade sem cópia (F3.3 do plano de 60 FPS): quando o alvo inteiro é resolvido, o
# destino fica com a imagem do alvo e o alvo ganha uma imagem nova; o conteúdo volta por cópia antes
# de o alvo ser escrito de novo. A decisão e os contadores em src/native/native_resolve_ownership.h,
# compilados sozinhos, sem SDK nem GPU.

MAIN = r"""
#include "native_resolve_ownership.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace gh3::native::ownership;

static Rect R(int x0, int y0, int x1, int y1) { Rect r; r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1; return r; }

int main(int argc, char** argv) {
  const char* what = argv[1];
  if (!std::strcmp(what, "decide")) {
    // x0 y0 x1 y1 | src_w src_h src_samples src_depth src_content src_format | dst_w dst_h dst_format
    Rect r = R(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]));
    Source s;
    s.width = std::atoi(argv[6]); s.height = std::atoi(argv[7]);
    s.samples = std::atoi(argv[8]); s.depth = std::atoi(argv[9]) != 0;
    s.holds_content = std::atoi(argv[10]) != 0;
    uint32_t src_format = std::atoi(argv[11]);
    Destination d;
    d.width = std::atoi(argv[12]); d.height = std::atoi(argv[13]); d.format = std::atoi(argv[14]);
    std::printf("%s\n", Name(Decide(r, src_format, s, d)));
    return 0;
  }
  if (!std::strcmp(what, "whole")) {
    std::printf("%d\n", int(CoversWholeTarget(R(std::atoi(argv[2]), std::atoi(argv[3]),
                                               std::atoi(argv[4]), std::atoi(argv[5])),
                                              std::atoi(argv[6]), std::atoi(argv[7]))));
    return 0;
  }
  if (!std::strcmp(what, "restore")) {
    std::printf("%d\n", int(RestoreBeforeWrite(std::atoi(argv[2]) != 0, std::atoi(argv[3]) != 0)));
    return 0;
  }
  if (!std::strcmp(what, "stats")) {
    Stats s;
    std::printf("[%s]\n", Format(s).c_str());
    Count(s, Decision::kHandOff, 1280u * 720u * 4u);
    Count(s, Decision::kHandOff, 1280u * 720u * 4u);
    Count(s, Decision::kRestoreFirst, 1280u * 720u * 4u);
    Count(s, Decision::kCopy, 0);
    Count(s, Decision::kNotWholeTarget, 0);
    Count(s, Decision::kNotWholeTarget, 0);
    Count(s, Decision::kNotWholeTarget, 0);
    std::printf("%s\n", Format(s).c_str());
    return 0;
  }
  return 2;
}
"""


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("ownership")
    inc = tmp / "inc"
    inc.mkdir()
    shutil.copy("src/native/native_resolve_ownership.h", inc)
    (tmp / "main.cpp").write_text(MAIN)
    out = tmp / "ownership"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp / "main.cpp"), "-o", str(out)], check=True)
    return out


def run(exe, *args):
    r = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True)
    return r.stdout.strip()


def decide(exe, rect=(0, 0, 1280, 720), src=(1280, 720, 1, 1, 1, 100), dst=(1280, 720, 100)):
    return run(exe, "decide", *rect, *src, *dst)


def test_whole_depth_target_is_handed_off_without_copy(exe):
    # O resolve de profundidade da cena: 1280x720 inteiro, 1 amostra, mesmo formato e tamanho do destino
    assert decide(exe) == "handoff"


def test_partial_depth_target_is_still_copied(exe):
    # O mapa de sombra resolve 1024x1024 de um alvo de 1040 de largura: não é o alvo inteiro
    assert decide(exe, rect=(0, 0, 1024, 1024), src=(1040, 1024, 1, 1, 1, 100), dst=(1040, 1024, 100)) == \
        "not the whole target"
    assert decide(exe, rect=(16, 0, 1280, 720)) == "not the whole target"
    assert decide(exe, rect=(0, 0, 1280, 704)) == "not the whole target"


def test_a_target_taller_than_the_resolve_is_still_copied(exe):
    # O alvo de profundidade da cena cresce até 2048 linhas (scissor default); o resolve cobre 720.
    # Entregar a imagem inteira faria a textura (720 linhas no fetch) amostrar a imagem errada.
    assert decide(exe, rect=(0, 0, 1280, 720), src=(1280, 2048, 1, 1, 1, 100),
                  dst=(1280, 720, 100)) == "not the whole target"


def test_color_is_out_until_parity(exe):
    # O jogo reusa o alvo de cor sem limpar: o restore pagaria a cópia que economiza
    assert decide(exe, src=(1280, 720, 1, 0, 1, 100)) == "not depth"


def test_multisampled_targets_are_copied(exe):
    assert decide(exe, src=(1280, 720, 2, 1, 1, 100)) == "multisampled"


def test_size_and_format_have_to_match(exe):
    assert decide(exe, dst=(1440, 720, 100)) == "size mismatch"
    assert decide(exe, dst=(1280, 720, 101)) == "format mismatch"
    # O alvo pode ser mais fundo que o resolve? Não: a imagem inteira muda de dono.
    assert decide(exe, src=(1280, 720, 1, 1, 1, 100), dst=(1280, 704, 100)) == "size mismatch"


def test_content_already_handed_off_is_restored_before_the_next_handoff(exe):
    assert decide(exe, src=(1280, 720, 1, 1, 0, 100)) == "restore first"


def test_whole_clear_does_not_need_the_content_back(exe):
    assert run(exe, "restore", 1, 1) == "0"
    assert run(exe, "restore", 1, 0) == "1"
    assert run(exe, "restore", 0, 0) == "0"


def test_whole_target_edges(exe):
    assert run(exe, "whole", 0, 0, 1280, 720, 1280, 720) == "1"
    assert run(exe, "whole", 0, 0, 1296, 736, 1280, 720) == "1"
    assert run(exe, "whole", 0, 0, 1279, 720, 1280, 720) == "0"
    assert run(exe, "whole", 0, 0, 1280, 719, 1280, 720) == "0"


def test_report_is_empty_until_something_is_decided_and_names_the_reasons(exe):
    empty, line = run(exe, "stats").splitlines()
    assert empty == "[]"
    assert line.startswith("resolve ownership: 2 without copy (7.0 MB), 1 restores (3.5 MB), 1 copied")
    assert "not the whole target:3" in line
    assert "handoff:" not in line and "copy:" not in line


def test_the_renderer_wires_the_handoff_behind_the_cvar():
    renderer = open("src/native/native_renderer.cpp").read()
    assert "gh3_native_resolve_swap" in renderer
    assert "BindBorrowedDepth" in renderer
    assert "resolve ownership" in renderer or "TakeResolveOwnershipReport" in renderer
    textures = open("src/native/native_textures.cpp").read()
    assert "BindBorrowedDepth" in textures
