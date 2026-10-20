#pragma once
// spirv-opt nos shaders traduzidos (F2.2 do plano de 60 FPS, T35).
//
// O SpirvShaderTranslator do SDK (herdado do Xenia) gera SPIR-V verboso e o SDK desliga o
// otimizador (disableOptimizer=true no command_processor). Aqui cada módulo traduzido passa pelo
// spvtools::Optimizer antes de virar VkShaderModule. Se a validação ou a otimização falhar, o
// módulo original é usado e o shader conta como fallback: nenhum shader deixa de existir por causa
// deste passo.
//
// A contagem de instruções anda pelo stream (wordCount no byte alto de cada instrução), que é o
// que o `spirv-dis --offsets` também usa. A parte pura compila e é testada sem GPU; o otimizador
// vem do SPIRV-Tools-opt construído no T34 (F2.1).

#include <cstddef>
#include <cstdint>
#include <vector>

#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/optimizer.hpp>

namespace gh3::native::spirv_opt {

// Número de instruções de um módulo SPIR-V (não de palavras).
inline uint32_t CountInstructions(const std::vector<uint32_t>& binary) {
  uint32_t count = 0;
  size_t i = 0;
  while (i < binary.size()) {
    const uint32_t words = binary[i] >> 16;
    if (!words) {
      break;
    }
    ++count;
    i += words;
  }
  return count;
}

// Ambiente do validador/otimizador a partir da versão no cabeçalho (palavra 1: major nos bits
// 0-7, minor nos 8-15). O tradutor emite de 1.0 a 1.5 conforme o que a GPU do host anuncia; validar
// como Vulkan 1.0 um módulo 1.5 faria o shader cair em fallback a toa.
inline spv_target_env TargetEnvFor(const std::vector<uint32_t>& binary) {
  const uint32_t version = binary.size() > 1 ? binary[1] : 0;
  const uint32_t minor = (version >> 8) & 0xFF;
  if (minor >= 4) {
    return SPV_ENV_VULKAN_1_2;
  }
  if (minor >= 3) {
    return SPV_ENV_VULKAN_1_1;
  }
  return SPV_ENV_VULKAN_1_0;
}

struct Outcome {
  enum class Fallback {
    kNone,
    kTooSmall,
    kInvalidInput,
    kOptimizerFailed,
    kInvalidOutput,
  };
  bool optimized = false;
  bool fallback = false;
  Fallback reason = Fallback::kNone;
  uint32_t before = 0;
  uint32_t after = 0;
};

// Otimiza o módulo no lugar. Devolve o resultado e, em fallback, deixa `binary` exatamente como
// veio (o otimizador escreve numa cópia). O trabalho é pesado (dezenas de ms por shader), então
// roda uma vez por tradução, nunca por quadro.
inline Outcome Optimize(std::vector<uint32_t>& binary) {
  Outcome outcome;
  outcome.before = CountInstructions(binary);
  outcome.after = outcome.before;
  if (binary.size() < 5) {
    outcome.fallback = true;
    outcome.reason = Outcome::Fallback::kTooSmall;
    return outcome;
  }
  const spv_target_env env = TargetEnvFor(binary);
  // O tradutor do Xenia gera o XeSystemConstants com membros fora dos offsets de 16 do layout
  // padrão (arrays de vec4 em offset 28): o SDK pede VK_EXT_scalar_block_layout no dispositivo
  // (vulkan_device.cpp, features_1_2.scalarBlockLayout), então o validador tem que checar com essa
  // feature ligada. Sem isso todo shader cai em fallback.
  spvtools::ValidatorOptions validator_options;
  validator_options.SetScalarBlockLayout(true);
  spvtools::SpirvTools tools(env);
  if (!tools.IsValid() || !tools.Validate(binary.data(), binary.size(), validator_options)) {
    outcome.fallback = true;
    outcome.reason = Outcome::Fallback::kInvalidInput;
    return outcome;
  }
  spvtools::Optimizer optimizer(env);
  // Lista do RegisterPerformancePasses: inline, SSA rewrite, CCP, simplify, dead code, copy prop,
  // eliminate local (e o resto do pacote de desempenho do SPIRV-Tools).
  optimizer.RegisterPerformancePasses();
  std::vector<uint32_t> optimized;
  spv_optimizer_options optimizer_options = spvOptimizerOptionsCreate();
  spvOptimizerOptionsSetValidatorOptions(optimizer_options, validator_options);
  const bool run_ok =
      optimizer.Run(binary.data(), binary.size(), &optimized, optimizer_options);
  spvOptimizerOptionsDestroy(optimizer_options);
  if (!run_ok || optimized.empty()) {
    outcome.fallback = true;
    outcome.reason = Outcome::Fallback::kOptimizerFailed;
    return outcome;
  }
  spvtools::SpirvTools output_tools(env);
  if (!output_tools.IsValid() ||
      !output_tools.Validate(optimized.data(), optimized.size(), validator_options)) {
    outcome.fallback = true;
    outcome.reason = Outcome::Fallback::kInvalidOutput;
    return outcome;
  }
  outcome.optimized = true;
  outcome.after = CountInstructions(optimized);
  binary.swap(optimized);
  return outcome;
}

// Acumulador do que o log de 10 s resume. Um shader = um módulo (VS ou PS).
struct Stats {
  uint64_t shaders = 0;
  uint64_t instructions_before = 0;
  uint64_t instructions_after = 0;
  uint64_t fallbacks = 0;

  void Add(const Outcome& outcome) {
    ++shaders;
    instructions_before += outcome.before;
    instructions_after += outcome.after;
    if (outcome.fallback) {
      ++fallbacks;
    }
  }

  double FewerPercent() const {
    if (!instructions_before) {
      return 0.0;
    }
    return 100.0 * (double(instructions_before) - double(instructions_after)) /
           double(instructions_before);
  }
};

}  // namespace gh3::native::spirv_opt
