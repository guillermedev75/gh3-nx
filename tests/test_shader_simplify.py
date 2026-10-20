"""F2.4 (T37): simplificação dos shaders traduzidos antes do driver.

A parte pura de src/native/native_shader_simplify.h (max(a,a) como cópia, queries de tamanho de
textura repetidas viram cópia e branches redundantes fundidos) é exercitada num programa C++
compilado sozinho, ligado às libs do T34. O aceite também abre o jogo; aqui ficam os testes baratos.
"""

import pathlib
import shutil
import subprocess

import pytest

RAIZ = pathlib.Path(__file__).resolve().parents[1]
INCLUDE = RAIZ / "out" / "sdk-install" / "include"
LIB = RAIZ / "out" / "sdk-install" / "lib"

# Um fragment shader viavel que tem os tres padroes de uma vez: FMax(a,a), SMin(a,a), duas
# OpImageQuerySizeLod iguais no mesmo bloco e uma selecao cujo braco e' um bloco vazio que so
# salta para o merge (o "if (p0)" que o XenosRecomp emite por instrucao).
kShader = r"""OpCapability Shader
OpCapability ImageQuery
%glsl = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %out
OpExecutionMode %main OriginUpperLeft
OpName %main "main"
OpDecorate %out Location 0
OpDecorate %image DescriptorSet 0
OpDecorate %image Binding 0
%void = OpTypeVoid
%fn = OpTypeFunction %void
%bool = OpTypeBool
%float = OpTypeFloat 32
%int = OpTypeInt 32 1
%v2int = OpTypeVector %int 2
%v4float = OpTypeVector %float 4
%ptrout = OpTypePointer Output %v4float
%out = OpVariable %ptrout Output
%tex = OpTypeImage %float 2D 0 0 0 1 Unknown
%pimg = OpTypePointer UniformConstant %tex
%image = OpVariable %pimg UniformConstant
%cond = OpConstantTrue %bool
%zero = OpConstant %int 0
%one = OpConstant %float 1
%main = OpFunction %void None %fn
%entry = OpLabel
%img = OpLoad %tex %image
%maxcopy = OpExtInst %float %glsl FMax %one %one
%mincopy = OpExtInst %int %glsl SMin %zero %zero
%size1 = OpImageQuerySizeLod %v2int %img %zero
%size2 = OpImageQuerySizeLod %v2int %img %zero
OpSelectionMerge %sel None
OpBranchConditional %cond %then %sel
%then = OpLabel
OpBranch %sel
%sel = OpLabel
OpReturn
OpFunctionEnd
"""

MAIN = r"""
#include "native_shader_simplify.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace gh3::native::shader_simplify;

static std::vector<uint32_t> Assemble(const char* text) {
  spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_0);
  std::vector<uint32_t> binary;
  if (!tools.Assemble(text, &binary)) std::printf("assemble failed\n");
  return binary;
}

static void Count(const std::vector<uint32_t>& binary, const char* which) {
  uint32_t ext = 0, query = 0, cond = 0, branch = 0, label = 0, copy = 0;
  size_t i = 5;
  while (i < binary.size()) {
    const uint32_t wc = binary[i] >> 16;
    const uint32_t op = binary[i] & 0xFFFF;
    if (!wc) break;
    if (op == 12 && wc >= 7) ++ext;
    if (op >= 103 && op <= 107) ++query;
    if (op == 250) ++cond;
    if (op == 249) ++branch;
    if (op == 248) ++label;
    if (op == 83) ++copy;
    i += wc;
  }
  std::printf("%s ext=%u query=%u cond=%u branch=%u label=%u copy=%u\n", which, ext, query,
              cond, branch, label, copy);
}

int main(int argc, char** argv) {
  if (!std::strcmp(argv[1], "simplify")) {
    std::vector<uint32_t> binary = Assemble(R"SHADER(%%SHADER%%)SHADER");
    const size_t words_before = binary.size();
    std::string messages;
    int input_valid = 0;
    {
      spvtools::SpirvTools check(SPV_ENV_VULKAN_1_0);
      check.SetMessageConsumer([&](spv_message_level_t, const char*, const spv_position_t&,
                                   const char* message) {
        messages += message;
        messages += " | ";
      });
      spvtools::ValidatorOptions options;
      options.SetScalarBlockLayout(true);
      input_valid = int(check.Validate(binary.data(), binary.size(), options));
    }
    Outcome outcome = Simplify(binary);
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_0);
    spvtools::ValidatorOptions validator_options;
    validator_options.SetScalarBlockLayout(true);
    const int valid = int(tools.Validate(binary.data(), binary.size(), validator_options));
    std::printf("%d %d %u %u %u %u %u %d %zu %zu\n", int(outcome.modified), int(outcome.fallback),
                outcome.max_min, outcome.texture_queries, outcome.branches, outcome.instructions_before,
                outcome.instructions_after, valid, words_before, binary.size());
    Count(binary, "out");
    std::printf("input-valid=%d %s\n", input_valid, messages.c_str());
    return 0;
  }
  if (!std::strcmp(argv[1], "garbage")) {
    std::vector<uint32_t> binary = {0xDEADBEEFu, 0u, 1u, 2u, 3u};
    const std::vector<uint32_t> original = binary;
    Outcome outcome = Simplify(binary);
    std::printf("%d %d %d\n", int(outcome.modified), int(outcome.fallback),
                int(binary == original));
    return 0;
  }
  if (!std::strcmp(argv[1], "plain")) {
    // Sem nenhum padrao: nao marca modificado (o renderer cai no modulo original do SDK).
    const char* text = R"(OpCapability Shader
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
    std::vector<uint32_t> binary = Assemble(text);
    Outcome outcome = Simplify(binary);
    std::printf("%d %d\n", int(outcome.modified), int(outcome.fallback));
    return 0;
  }
  return 2;
}
"""


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    if not (INCLUDE / "spirv-tools" / "optimizer.hpp").is_file():
        pytest.fail("falta out/sdk-install/include/spirv-tools (T34/tools/build_spirv_tools.sh)")
    tmp = tmp_path_factory.mktemp("shader_simplify")
    inc = tmp / "inc"
    inc.mkdir()
    for header in ("native_shader_simplify.h", "native_spirv_opt.h"):
        shutil.copy(RAIZ / "src" / "native" / header, inc)
    source = MAIN.replace("%%SHADER%%", kShader)
    (tmp / "main.cpp").write_text(source)
    out = tmp / "shader_simplify"
    libs = [str(LIB / name) for name in
            ("libSPIRV-Tools-opt.a", "libSPIRV-Tools-link.a", "libSPIRV-Tools.a")]
    for lib in libs:
        assert pathlib.Path(lib).is_file(), f"falta {lib} (T34/tools/build_spirv_tools.sh)"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", f"-I{INCLUDE}", str(tmp / "main.cpp"),
                    *libs, "-o", str(out)], check=True)
    return out


def run(exe, *args):
    r = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True)
    return r.stdout.splitlines()


def test_max_and_min_with_the_same_operand_become_a_plain_copy(exe):
    lines = run(exe, "simplify")
    modified, fallback, max_min = map(int, lines[0].split()[:3])
    assert (modified, fallback, max_min) == (1, 0, 2)
    out = dict(part.split("=") for part in lines[1].split()[1:])
    # Nenhuma FMax/SMin chega ao driver: viraram OpCopyObject.
    assert int(out["ext"]) == 0
    assert int(out["copy"]) == 3  # 2 do max/min + 1 da query repetida


def test_repeated_texture_size_query_is_reused(exe):
    lines = run(exe, "simplify")
    query = int(lines[0].split()[3])
    out = dict(part.split("=") for part in lines[1].split()[1:])
    assert query == 1
    assert int(out["query"]) == 1


def test_redundant_branch_into_the_merge_is_merged(exe):
    lines = run(exe, "simplify")
    branches = int(lines[0].split()[4])
    out = dict(part.split("=") for part in lines[1].split()[1:])
    assert branches == 2  # o braco vazio foi fundido e o conditional virou OpBranch
    assert int(out["cond"]) == 0
    assert int(out["label"]) == 2  # so entry e merge


def test_the_result_is_valid_spirv_and_smaller(exe):
    fields = run(exe, "simplify")[0].split()
    instructions_before, instructions_after, valid, words_before, words_after = map(
        int, (fields[5], fields[6], fields[7], fields[8], fields[9]))
    assert valid == 1
    assert 0 < instructions_after < instructions_before
    assert words_after < words_before


def test_invalid_spirv_falls_back_to_the_original_bytes(exe):
    modified, fallback, unchanged = map(int, run(exe, "garbage")[0].split())
    assert (modified, fallback, unchanged) == (0, 1, 1)


def test_a_module_without_the_patterns_is_left_alone(exe):
    modified, fallback = map(int, run(exe, "plain")[0].split())
    assert (modified, fallback) == (0, 0)


def test_renderer_runs_the_pass_by_default_behind_a_cvar():
    src = (RAIZ / "src" / "native" / "native_renderer.cpp").read_text()
    assert '#include "native_shader_simplify.h"' in src
    assert "REXCVAR_DEFINE_BOOL(gh3_native_shader_simplify, true" in src
    assert "shader_simplify::Simplify(" in src
    assert "shader simplify:" in src
    system = (RAIZ / "src" / "native" / "gh3_native_system.cpp").read_text()
    assert "TakeShaderSimplifyReport()" in system
    # O passe vale mesmo com o spirv-opt desligado (Mac): e' ele que tira o max(a,a) do caminho.
    assert "gh3_native_spirv_opt" in src


def test_switch_package_keeps_the_pass_on():
    toml = (RAIZ / "switch" / "gh3.toml").read_text()
    assert "\ngh3_native_shader_simplify = true\n" in toml
