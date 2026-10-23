import shutil
import subprocess

import pytest

# Cache em disco do SPIR-V otimizado (2026-10-23): no Switch o spirv-opt é obrigatório, mas refazê-lo a cada
# abertura prendia um núcleo por ~40 s (intro lenta, áudio cortando). src/native/native_spirv_cache.h.

MAIN = r"""
#include "native_spirv_cache.h"
#include <cstdio>
#include <cstring>
using namespace gh3::native::spirv_cache;
int main(int argc, char** argv) {
  std::vector<uint32_t> a = {kSpirvMagic, 0x10000, 0, 8, 0, 0x00020011, 1};
  std::vector<uint32_t> b = a; b.back() = 2;
  if (!std::strcmp(argv[1], "key")) {
    std::printf("%d %d %d %d\n", int(Key(a, true, true) == Key(a, true, true)),
                int(Key(a, true, true) != Key(b, true, true)), int(Key(a, true, true) != Key(a, true, false)),
                int(Key(a, false, true) != Key(a, true, true)));
    return 0;
  }
  std::filesystem::path dir = argv[2];
  if (!std::strcmp(argv[1], "roundtrip")) {
    std::vector<uint32_t> out;
    const uint64_t k = Key(a, true, true);
    int before = Load(dir, k, &out);
    int stored = Store(dir, k, a);
    int loaded = Load(dir, k, &out);
    int same = out == a;
    int again = Store(dir, k, b) && Load(dir, k, &out) && out == b;  // sobrescreve
    std::printf("%d %d %d %d %d\n", before, stored, loaded, same, again);
    return 0;
  }
  if (!std::strcmp(argv[1], "invalid")) {
    std::vector<uint32_t> out;
    std::vector<uint32_t> junk = {1, 2, 3, 4, 5};
    int refuse = !Store(dir, 7, junk);
    // arquivo truncado (tamanho não múltiplo de 4) e magia errada no disco: ignorados
    { std::ofstream f(dir / FileName(9), std::ios::binary); f << "abc"; }
    { std::ofstream f(dir / FileName(10), std::ios::binary); f.write((const char*)junk.data(), 20); }
    std::printf("%d %d %d %d\n", refuse, !Load(dir, 9, &out), !Load(dir, 10, &out), !Load(dir, 11, &out));
    return 0;
  }
  return 2;
}
"""


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("spvcache")
    inc = tmp / "inc"; inc.mkdir()
    shutil.copy("src/native/native_spirv_cache.h", inc)
    (tmp / "m.cpp").write_text(MAIN)
    out = tmp / "m"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp / "m.cpp"), "-o", str(out)], check=True)
    return out


def run(exe, *a):
    return subprocess.run([str(exe), *map(str, a)], capture_output=True, text=True, check=True).stdout.split()


def test_key_depends_on_the_module_and_the_passes(exe):
    assert run(exe, "key") == ["1", "1", "1", "1"]


def test_store_then_load_returns_the_same_module_and_overwrites(exe, tmp_path):
    assert run(exe, "roundtrip", tmp_path / "c") == ["0", "1", "1", "1", "1"]
    assert not list((tmp_path / "c").glob("*.tmp"))


def test_invalid_or_truncated_files_are_ignored(exe, tmp_path):
    assert run(exe, "invalid", tmp_path) == ["1", "1", "1", "1"]


def test_renderer_reads_the_cache_before_optimizing_and_stores_after():
    src = open("src/native/native_renderer.cpp").read()
    body = src[src.find("VkShaderModule Renderer::Impl::ShaderModuleFor("):]
    body = body[:body.find("\nVkShaderModule Renderer::Impl::", 10) if "\nVkShaderModule Renderer::Impl::" in body[10:] else 6000]
    assert body.find("spirv_cache::Load(") < body.find("shader_simplify::Simplify(")
    assert "spirv_cache::Store(" in body
    assert 'spirv_cache_dir = cache_root / "shaders" / "spirv"' in src.replace("im.", "")
    assert "spirv cache:" in src
