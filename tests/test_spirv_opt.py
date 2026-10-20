"""F2.2 (T35): spirv-opt nos shaders traduzidos do renderer nativo.

A parte pura de src/native/native_spirv_opt.h (contagem de instruções, escolha do ambiente,
fallback ao original) é exercitada num programa C++ compilado sozinho, ligado às libs do T34.
O aceite também abre o jogo; aqui ficam os testes baratos.
"""

import pathlib
import shutil
import subprocess

import pytest

RAIZ = pathlib.Path(__file__).resolve().parents[1]
INCLUDE = RAIZ / "out" / "sdk-install" / "include"
LIB = RAIZ / "out" / "sdk-install" / "lib"

MAIN = r"""
#include "native_spirv_opt.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace gh3::native::spirv_opt;

static const char* kFragment = R"(OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out
OpExecutionMode %main OriginUpperLeft
OpDecorate %out Location 0
%void = OpTypeVoid
%fn = OpTypeFunction %void
%float = OpTypeFloat 32
%v4 = OpTypeVector %float 4
%ptr = OpTypePointer Output %v4
%out = OpVariable %ptr Output
%one = OpConstant %float 1
%v = OpConstantComposite %v4 %one %one %one %one
%main = OpFunction %void None %fn
%lbl = OpLabel
%sum = OpFAdd %v4 %v %v
OpStore %out %sum
OpReturn
OpFunctionEnd
)";

static std::vector<uint32_t> Assemble() {
  spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_0);
  std::vector<uint32_t> binary;
  if (!tools.Assemble(kFragment, &binary)) std::printf("assemble failed\n");
  return binary;
}

int main(int argc, char** argv) {
  if (!std::strcmp(argv[1], "optimize")) {
    std::vector<uint32_t> binary = Assemble();
    const uint32_t before = CountInstructions(binary);
    Outcome outcome = Optimize(binary);
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_0);
    std::printf("%d %d %u %u %u %d\n", int(outcome.optimized), int(outcome.fallback),
                outcome.before, outcome.after, before, int(tools.Validate(binary)));
    return 0;
  }
  if (!std::strcmp(argv[1], "stduniform")) {
    // Como o XeSystemConstants do tradutor: membro de 4 bytes em offset 28 (padrão de 16 do Vulkan).
    const char* text = R"(OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Vertex %main "main"
OpDecorate %block Block
OpMemberDecorate %block 0 Offset 0
OpMemberDecorate %block 1 Offset 28
OpDecorate %arr ArrayStride 16
OpDecorate %var DescriptorSet 1
OpDecorate %var Binding 0
%void = OpTypeVoid
%fn = OpTypeFunction %void
%uint = OpTypeInt 32 0
%v4uint = OpTypeVector %uint 4
%len = OpConstant %uint 6
%arr = OpTypeArray %v4uint %len
%block = OpTypeStruct %uint %arr
%ptr = OpTypePointer Uniform %block
%var = OpVariable %ptr Uniform
%main = OpFunction %void None %fn
%lbl = OpLabel
OpReturn
OpFunctionEnd
)";
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_0);
    std::vector<uint32_t> binary;
    if (!tools.Assemble(text, &binary)) { std::printf("0 0\n"); return 0; }
    Outcome outcome = Optimize(binary);
    std::printf("%d %d\n", int(outcome.optimized), int(outcome.fallback));
    return 0;
  }
  if (!std::strcmp(argv[1], "garbage")) {
    std::vector<uint32_t> binary = {0xDEADBEEFu, 0x00000000u, 1u, 2u, 3u};
    const std::vector<uint32_t> original = binary;
    Outcome outcome = Optimize(binary);
    std::printf("%d %d %u %u %d\n", int(outcome.optimized), int(outcome.fallback),
                outcome.before, outcome.after, int(binary == original));
    return 0;
  }
  if (!std::strcmp(argv[1], "env")) {
    // cabeçalho: magia, versão, gerador, bound, schema
    std::vector<uint32_t> binary = {0x07230203u, 0u, 0u, 1u, 0u};
    std::printf("%d %d %d\n", int(TargetEnvFor({0x07230203u, 0x00010000u, 0, 1, 0}) == SPV_ENV_VULKAN_1_0),
                int(TargetEnvFor({0x07230203u, 0x00010300u, 0, 1, 0}) == SPV_ENV_VULKAN_1_1),
                int(TargetEnvFor({0x07230203u, 0x00010500u, 0, 1, 0}) == SPV_ENV_VULKAN_1_2));
    (void)binary;
    return 0;
  }
  if (!std::strcmp(argv[1], "count")) {
    std::vector<uint32_t> binary = Assemble();
    std::printf("%u %zu\n", CountInstructions(binary), binary.size());
    return 0;
  }
  if (!std::strcmp(argv[1], "stats")) {
    Stats stats;
    stats.Add(Outcome{.optimized = true, .before = 100, .after = 80});
    stats.Add(Outcome{.fallback = true, .before = 40, .after = 40});
    std::printf("%llu %llu %llu %llu %.1f\n", (unsigned long long)stats.shaders,
                (unsigned long long)stats.instructions_before,
                (unsigned long long)stats.instructions_after,
                (unsigned long long)stats.fallbacks, stats.FewerPercent());
    return 0;
  }
  return 2;
}
"""


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    if not (INCLUDE / "spirv-tools" / "optimizer.hpp").is_file():
        pytest.fail("falta out/sdk-install/include/spirv-tools (T34/tools/build_spirv_tools.sh)")
    tmp = tmp_path_factory.mktemp("spirv_opt")
    inc = tmp / "inc"
    inc.mkdir()
    shutil.copy(RAIZ / "src" / "native" / "native_spirv_opt.h", inc)
    (tmp / "main.cpp").write_text(MAIN)
    out = tmp / "spirv_opt"
    libs = [str(LIB / name) for name in
            ("libSPIRV-Tools-opt.a", "libSPIRV-Tools-link.a", "libSPIRV-Tools.a")]
    for lib in libs:
        assert pathlib.Path(lib).is_file(), f"falta {lib} (T34/tools/build_spirv_tools.sh)"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", f"-I{INCLUDE}", str(tmp / "main.cpp"),
                    *libs, "-o", str(out)], check=True)
    return out


def run(exe, *args):
    r = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True)
    return r.stdout.split()


def test_a_translated_module_is_optimized_and_still_valid(exe):
    optimized, fallback, before, after, count_before, still_valid = map(int, run(exe, "optimize"))
    assert optimized == 1 and fallback == 0 and still_valid == 1
    assert before == count_before and before >= after > 0


def test_scalar_block_layout_is_allowed_as_on_the_device(exe):
    # O SDK liga VK_EXT_scalar_block_layout; o validador tem que concordar.
    assert [int(v) for v in run(exe, "stduniform")] == [1, 0]


def test_invalid_spirv_falls_back_to_the_original_bytes(exe):
    optimized, fallback, before, after, unchanged = map(int, run(exe, "garbage"))
    assert optimized == 0 and fallback == 1
    assert before == after and unchanged == 1


def test_target_environment_follows_the_header_version(exe):
    assert [int(v) for v in run(exe, "env")] == [1, 1, 1]


def test_instruction_count_matches_the_module_and_stats_aggregate(exe):
    count, words = map(int, run(exe, "count"))
    assert 0 < count <= words
    shaders, before, after, fallbacks, fewer = run(exe, "stats")
    assert int(shaders) == 2 and int(before) == 140 and int(after) == 120
    assert int(fallbacks) == 1 and abs(float(fewer) - 14.3) <= 0.1


def test_renderer_uses_the_optimizer_behind_a_cvar_off_by_default():
    src = (RAIZ / "src" / "native" / "native_renderer.cpp").read_text()
    assert '#include "native_spirv_opt.h"' in src
    assert "REXCVAR_DEFINE_BOOL(gh3_native_spirv_opt, false" in src
    assert "spirv_opt::Optimize(" in src
    # o módulo otimizado é o que vai para o pipeline; o original continua sendo o fallback
    assert "optimized_shader_modules" in src
    assert "spirv-opt:" in src
    assert "TakeSpirvOptReport()" in (RAIZ / "src" / "native" / "gh3_native_system.cpp").read_text()


def test_switch_package_turns_the_optimizer_on():
    toml = (RAIZ / "switch" / "gh3.toml").read_text()
    assert "\ngh3_native_spirv_opt = true\n" in toml


def test_build_links_the_optimizer_on_both_toolchains():
    host = (RAIZ / "CMakeLists.txt").read_text()
    assert "SPIRV-Tools-opt" in host
    switch = (RAIZ / "cmake" / "switch.cmake").read_text()
    assert "SPIRV-Tools-opt" in switch
