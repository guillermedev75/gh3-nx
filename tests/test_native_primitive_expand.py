import shutil, subprocess

# A escolha e a expansão de primitivas sem geometry shader (src/native/native_primitive_expand.h)
# compiladas sozinhas, sem SDK nem GPU.
MAIN = r"""
#include "native_primitive_expand.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace gh3::native::expand;
int main(int argc, char** argv) {
  if (!std::strcmp(argv[1], "choose")) {
    Primitive p = Primitive(std::atoi(argv[2]));
    std::printf("%d\n", int(Choose(p, argv[3][0] == '1')));
  } else if (!std::strcmp(argv[1], "quads")) {
    uint32_t n = std::atoi(argv[2]);
    std::vector<uint32_t> out(QuadListIndexCount(n));
    QuadListToTriangleList(n, [](uint32_t i) { return 100 + i; },
                           [&](uint32_t i, uint32_t v) { out[i] = v; });
    for (uint32_t v : out) std::printf("%u ", v);
    std::printf("\n");
  } else if (!std::strcmp(argv[1], "strips")) {
    uint32_t n = std::atoi(argv[2]);
    std::vector<uint32_t> out(TwoTriangleStripIndexCount(n));
    WriteTwoTriangleStrips(n, out.data());
    for (uint32_t v : out) std::printf("%d ", v == 0xFFFFFFFFu ? -1 : int(v));
    std::printf("\n");
  } else if (!std::strcmp(argv[1], "prims")) {
    std::printf("%u\n", ExpandedPrimitiveCount(Primitive(std::atoi(argv[2])), std::atoi(argv[3])));
  }
}
"""

POINT, RECT, QUAD, OTHER = 0, 1, 2, 3
NATIVE, GS, QUAD_TRIS, VS_STRIP = 0, 1, 2, 3


def build(tmp_path):
    inc = tmp_path / "inc"; inc.mkdir()
    shutil.copy("src/native/native_primitive_expand.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "expand"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)], check=True)
    return exe


def out(exe, *args):
    return subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True).stdout.split()


def test_with_geometry_shaders_keeps_gs(tmp_path):
    # Switch (NVK tem geometryShader): continua no GS
    exe = build(tmp_path)
    for p in (POINT, RECT, QUAD):
        assert out(exe, "choose", p, 1) == [str(GS)]
    assert out(exe, "choose", OTHER, 1) == [str(NATIVE)]


def test_without_geometry_shaders(tmp_path):
    # MoltenVK: quad vira triângulos por índice; ret e ponto vão para a strip expandida no VS
    exe = build(tmp_path)
    assert out(exe, "choose", QUAD, 0) == [str(QUAD_TRIS)]
    assert out(exe, "choose", RECT, 0) == [str(VS_STRIP)]
    assert out(exe, "choose", POINT, 0) == [str(VS_STRIP)]
    assert out(exe, "choose", OTHER, 0) == [str(NATIVE)]


def test_quad_list_to_triangles(tmp_path):
    # mesma ordem do PrimitiveProcessor do SDK: (0 1 2) (0 2 3); quad incompleto no fim cai fora
    exe = build(tmp_path)
    assert out(exe, "quads", 9) == "100 101 102 100 102 103 104 105 106 104 106 107".split()
    assert out(exe, "quads", 3) == []


def test_two_triangle_strips(tmp_path):
    # 4 vértices por primitiva, restart entre elas (o VS tira primitiva = i >> 2, canto = i & 3)
    exe = build(tmp_path)
    assert out(exe, "strips", 1) == "0 1 2 3".split()
    assert out(exe, "strips", 3) == "0 1 2 3 -1 4 5 6 7 -1 8 9 10 11".split()


def test_expanded_primitive_count(tmp_path):
    exe = build(tmp_path)
    assert out(exe, "prims", RECT, 7) == ["2"]   # 3 vértices por retângulo
    assert out(exe, "prims", POINT, 7) == ["7"]
