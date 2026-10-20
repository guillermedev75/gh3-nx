#pragma once
// Simplificação dos shaders traduzidos (F2.4 do plano de 60 FPS, T37).
//
// O SpirvShaderTranslator do SDK (herdado do Xenia) emite padrões que o driver não limpa de graça
// na GPU Maxwell/NVK — o nfsmw-nx mediu isso (`docs/shaders.md`):
//
//  - `max(a, a)` como cópia: o Xbox 360 não tem instrução de cópia, então a cópia vira `MAX dst, a, a`
//    e chega ao GPU como uma multiplicação real (`fcanonicalize` → `fmul(a, 1.0)`). Aqui o
//    `GLSL.std.450` FMax/FMin/UMax/UMin/SMax/SMin com os dois operandos iguais vira OpCopyObject, que
//    o backend resolve como movimentação de registrador (e o spirv-opt remove quando está ligado).
//  - queries de tamanho de textura repetidas: a mesma `OpImageQuerySizeLod` (e as variantes Size,
//    Levels, Samples e QueryLod) com os mesmos operandos, no mesmo bloco, é respondida uma vez; as
//    seguintes viram cópia do primeiro resultado. Nenhuma leitura muda de tamanho dentro do bloco.
//  - branches redundantes: um `OpBranchConditional` cujos dois destinos são o mesmo vira `OpBranch`
//    (com o `OpSelectionMerge` do bloco removido, senão o cabeçalho fica inválido) e um bloco que só
//    existe para saltar para outro (o `if (p0)` que o XenosRecomp emite por instrução) é fundido no
//    predecessor, quando tem um único predecessor, não é alvo de merge/phi/switch e não é o entry.
//
// O módulo é validado antes e depois; se qualquer coisa falhar, o original é devolvido intacto e o
// shader conta como fallback. A parte pura compila e é testada sem GPU, mas usa o validador e as
// constantes de operando do SPIRV-Tools (já construído no T34/F2.1).

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spirv-tools/libspirv.hpp>

#include "native_spirv_opt.h"

namespace gh3::native::shader_simplify {

namespace op {
constexpr uint16_t kExtInstImport = 11;
constexpr uint16_t kExtInst = 12;
constexpr uint16_t kCopyObject = 83;
constexpr uint16_t kImageQuerySizeLod = 103;
constexpr uint16_t kImageQuerySize = 104;
constexpr uint16_t kImageQueryLod = 105;
constexpr uint16_t kImageQueryLevels = 106;
constexpr uint16_t kImageQuerySamples = 107;
constexpr uint16_t kPhi = 245;
constexpr uint16_t kLoopMerge = 246;
constexpr uint16_t kSelectionMerge = 247;
constexpr uint16_t kLabel = 248;
constexpr uint16_t kBranch = 249;
constexpr uint16_t kBranchConditional = 250;
constexpr uint16_t kSwitch = 251;
}  // namespace op

// GLSL.std.450: FMin=37, UMin=38, SMin=39, FMax=40, UMax=41, SMax=42. Todos são idempotentes:
// min(a, a) == max(a, a) == a, inclusive NaN e zero negativo.
inline bool IsIdempotentMinMax(uint32_t instruction) {
  return instruction >= 37 && instruction <= 42;
}

inline bool IsImageQuery(uint16_t opcode) {
  return opcode >= op::kImageQuerySizeLod && opcode <= op::kImageQuerySamples;
}

struct Outcome {
  enum class Fallback {
    kNone,
    kTooSmall,
    kInvalidInput,
    kInvalidOutput,
  };
  bool modified = false;
  bool fallback = false;
  Fallback reason = Fallback::kNone;
  uint32_t max_min = 0;
  uint32_t texture_queries = 0;
  uint32_t branches = 0;
  uint32_t instructions_before = 0;
  uint32_t instructions_after = 0;
  size_t words_before = 0;
  size_t words_after = 0;
};

namespace detail {

inline void PushInstruction(std::vector<uint32_t>& out, uint16_t opcode,
                            std::initializer_list<uint32_t> operands) {
  out.push_back((uint32_t(1 + operands.size()) << 16) | opcode);
  for (uint32_t operand : operands) {
    out.push_back(operand);
  }
}

inline void PushCopy(std::vector<uint32_t>& out, uint32_t type, uint32_t result, uint32_t value) {
  PushInstruction(out, op::kCopyObject, {type, result, value});
}

inline std::string ExtInstImportName(const std::vector<uint32_t>& binary, size_t index,
                                     uint32_t words) {
  std::string name;
  for (size_t word = index + 2; word < index + words; ++word) {
    for (int byte = 0; byte < 4; ++byte) {
      const char c = char((binary[word] >> (8 * byte)) & 0xFF);
      if (c) {
        name.push_back(c);
      }
    }
  }
  return name;
}

// Chave de uma query: opcode, tipo do resultado e operandos (imagem + lod/coordenada). Duas queries
// com a mesma chave no mesmo bloco devolvem o mesmo valor.
using QueryKey = std::vector<uint32_t>;

inline QueryKey QueryKeyFor(const std::vector<uint32_t>& binary, size_t index, uint32_t words,
                            uint16_t opcode) {
  QueryKey key;
  key.push_back(opcode);
  key.push_back(binary[index + 1]);  // tipo do resultado
  key.push_back(binary[index + 3]);  // imagem
  if ((opcode == op::kImageQuerySizeLod || opcode == op::kImageQueryLod) && words >= 5) {
    key.push_back(binary[index + 4]);  // lod / coordenada
  }
  return key;
}

inline bool IsValidModule(const std::vector<uint32_t>& binary) {
  spvtools::ValidatorOptions validator_options;
  validator_options.SetScalarBlockLayout(true);
  spvtools::SpirvTools tools(spirv_opt::TargetEnvFor(binary));
  return tools.IsValid() && tools.Validate(binary.data(), binary.size(), validator_options);
}

}  // namespace detail

// Reescreve o módulo no lugar. Em fallback, `binary` fica exatamente como veio.
inline Outcome Simplify(std::vector<uint32_t>& binary) {
  Outcome outcome;
  outcome.words_before = binary.size();
  outcome.words_after = binary.size();
  outcome.instructions_before = spirv_opt::CountInstructions(binary);
  outcome.instructions_after = outcome.instructions_before;
  if (binary.size() < 5) {
    outcome.fallback = true;
    outcome.reason = Outcome::Fallback::kTooSmall;
    return outcome;
  }
  if (!detail::IsValidModule(binary)) {
    outcome.fallback = true;
    outcome.reason = Outcome::Fallback::kInvalidInput;
    return outcome;
  }

  // Primeira passada: import do GLSL, alvos de merge/phi/switch, contagem de predecessores e
  // candidatos a bloco-saltinho (só OpLabel + OpBranch).
  uint32_t glsl_import = 0;
  std::unordered_map<uint32_t, uint32_t> predecessors;
  std::unordered_set<uint32_t> merge_targets;
  std::unordered_set<uint32_t> phi_parents;
  std::unordered_set<uint32_t> switch_words;
  std::unordered_map<uint32_t, uint32_t> trampoline_target;
  {
    uint32_t block = 0;
    uint32_t block_instructions = 0;
    uint16_t block_first_op = 0;
    uint32_t block_branch_target = 0;
    auto finish_block = [&]() {
      if (block && block_instructions == 1 && block_first_op == op::kBranch &&
          block_branch_target != block) {
        trampoline_target[block] = block_branch_target;
      }
      block = 0;
      block_instructions = 0;
      block_first_op = 0;
      block_branch_target = 0;
    };
    size_t i = 5;  // o cabeçalho do módulo não é instrução
    while (i < binary.size()) {
      const uint32_t words = binary[i] >> 16;
      const uint16_t opcode = uint16_t(binary[i] & 0xFFFF);
      if (!words) {
        break;
      }
      if (opcode == op::kLabel) {
        finish_block();
        block = binary[i + 1];
      } else if (block) {
        ++block_instructions;
        if (block_instructions == 1) {
          block_first_op = opcode;
        }
        if (opcode == op::kBranch && words >= 2) {
          block_branch_target = binary[i + 1];
        }
      }
      if (opcode == op::kExtInstImport && !glsl_import && words > 2 &&
          detail::ExtInstImportName(binary, i, words) == "GLSL.std.450") {
        glsl_import = binary[i + 1];
      }
      if (opcode == op::kSelectionMerge && words >= 2) {
        merge_targets.insert(binary[i + 1]);
      } else if (opcode == op::kLoopMerge && words >= 3) {
        merge_targets.insert(binary[i + 1]);
        merge_targets.insert(binary[i + 2]);
      } else if (opcode == op::kBranch && words >= 2) {
        ++predecessors[binary[i + 1]];
      } else if (opcode == op::kBranchConditional && words >= 4) {
        ++predecessors[binary[i + 2]];
        ++predecessors[binary[i + 3]];
      } else if (opcode == op::kSwitch && words >= 3) {
        // O literal do case tem largura variável (16/32/64 bits); contar todas as palavras só
        // superestima predecessores (seguro: impede a fusão) e marca os alvos para exclusão.
        for (size_t word = i + 2; word < i + words; ++word) {
          if (binary[word]) {
            ++predecessors[binary[word]];
            switch_words.insert(binary[word]);
          }
        }
      } else if (opcode == op::kPhi && words >= 5) {
        for (size_t word = i + 4; word < i + words; word += 2) {
          phi_parents.insert(binary[word]);
        }
      }
      i += words;
    }
    finish_block();
  }

  std::unordered_map<uint32_t, uint32_t> trampolines;
  for (const auto& [label, target] : trampoline_target) {
    if (predecessors[label] != 1 || merge_targets.count(label) || phi_parents.count(label) ||
        switch_words.count(label)) {
      continue;
    }
    trampolines[label] = target;
  }
  // Resolve cadeias de saltinhos, com guarda de ciclo.
  for (auto& [label, target] : trampolines) {
    std::unordered_set<uint32_t> seen;
    uint32_t current = target;
    while (trampolines.count(current) && !seen.count(current)) {
      seen.insert(current);
      current = trampolines[current];
    }
    target = current;
  }

  std::vector<uint32_t> out;
  out.reserve(binary.size());
  out.insert(out.end(), binary.begin(), binary.begin() + 5);
  std::map<detail::QueryKey, uint32_t> queries_in_block;
  uint32_t current_block = 0;
  bool skipping_block = false;
  size_t merge_position = SIZE_MAX;
  uint16_t merge_opcode = 0;
  auto resolve = [&](uint32_t label) {
    std::unordered_set<uint32_t> seen;
    uint32_t current = label;
    while (trampolines.count(current) && !seen.count(current)) {
      seen.insert(current);
      const uint32_t next = trampolines[current];
      if (next == current_block || next == current) {
        break;
      }
      current = next;
    }
    return current;
  };

  size_t i = 5;  // o cabeçalho já foi copiado
  while (i < binary.size()) {
    const uint32_t words = binary[i] >> 16;
    const uint16_t opcode = uint16_t(binary[i] & 0xFFFF);
    if (!words) {
      break;
    }
    if (opcode == op::kLabel) {
      current_block = binary[i + 1];
      merge_position = SIZE_MAX;
      merge_opcode = 0;
      queries_in_block.clear();
      if (!trampolines.count(current_block)) {
        skipping_block = false;
        for (uint32_t word = 0; word < words; ++word) {
          out.push_back(binary[i + word]);
        }
      } else {
        skipping_block = true;
      }
      i += words;
      continue;
    }
    if (skipping_block) {
      i += words;
      continue;
    }
    if (opcode == op::kSelectionMerge || opcode == op::kLoopMerge) {
      merge_position = out.size();
      merge_opcode = opcode;
      for (uint32_t word = 0; word < words; ++word) {
        out.push_back(binary[i + word]);
      }
    } else if (opcode == op::kExtInst && glsl_import && words >= 7 &&
               binary[i + 3] == glsl_import && IsIdempotentMinMax(binary[i + 4]) &&
               binary[i + 5] == binary[i + 6]) {
      detail::PushCopy(out, binary[i + 1], binary[i + 2], binary[i + 5]);
      ++outcome.max_min;
    } else if (IsImageQuery(opcode) && words >= 4) {
      const detail::QueryKey key = detail::QueryKeyFor(binary, i, words, opcode);
      auto found = queries_in_block.find(key);
      if (found == queries_in_block.end()) {
        queries_in_block.emplace(key, binary[i + 2]);
        for (uint32_t word = 0; word < words; ++word) {
          out.push_back(binary[i + word]);
        }
      } else {
        detail::PushCopy(out, binary[i + 1], binary[i + 2], found->second);
        ++outcome.texture_queries;
      }
    } else if (opcode == op::kBranch && words >= 2) {
      const uint32_t target = resolve(binary[i + 1]);
      if (target != binary[i + 1]) {
        ++outcome.branches;
      }
      detail::PushInstruction(out, op::kBranch, {target});
      merge_position = SIZE_MAX;
      merge_opcode = 0;
    } else if (opcode == op::kBranchConditional && words >= 4) {
      const uint32_t when_true = resolve(binary[i + 2]);
      const uint32_t when_false = resolve(binary[i + 3]);
      if (when_true != binary[i + 2] || when_false != binary[i + 3]) {
        ++outcome.branches;
      }
      if (when_true == when_false && merge_opcode != op::kLoopMerge) {
        if (merge_opcode == op::kSelectionMerge && merge_position != SIZE_MAX) {
          const auto begin = out.begin() + std::ptrdiff_t(merge_position);
          out.erase(begin, begin + 3);
        }
        detail::PushInstruction(out, op::kBranch, {when_true});
        ++outcome.branches;
      } else {
        const size_t operand_words = 3 + (words - 4);
        out.push_back((uint32_t(1 + operand_words) << 16) | op::kBranchConditional);
        out.push_back(binary[i + 1]);
        out.push_back(when_true);
        out.push_back(when_false);
        for (uint32_t word = 4; word < words; ++word) {
          out.push_back(binary[i + word]);
        }
      }
      merge_position = SIZE_MAX;
      merge_opcode = 0;
    } else {
      for (uint32_t word = 0; word < words; ++word) {
        out.push_back(binary[i + word]);
      }
    }
    i += words;
  }

  outcome.modified = outcome.max_min || outcome.texture_queries || outcome.branches;
  outcome.words_after = out.size();
  outcome.instructions_after = spirv_opt::CountInstructions(out);
  if (!outcome.modified) {
    outcome.words_after = outcome.words_before;
    outcome.instructions_after = outcome.instructions_before;
    return outcome;
  }
  if (out.empty() || !detail::IsValidModule(out)) {
    outcome.modified = false;
    outcome.fallback = true;
    outcome.reason = Outcome::Fallback::kInvalidOutput;
    outcome.words_after = outcome.words_before;
    outcome.instructions_after = outcome.instructions_before;
    return outcome;
  }
  binary.swap(out);
  return outcome;
}

// Acumulador do log de 10 s. Um shader = um módulo (VS ou PS).
struct Stats {
  uint64_t modules = 0;
  uint64_t max_min = 0;
  uint64_t texture_queries = 0;
  uint64_t branches = 0;
  uint64_t words_before = 0;
  uint64_t words_after = 0;
  uint64_t fallbacks = 0;

  void Add(const Outcome& outcome) {
    ++modules;
    max_min += outcome.max_min;
    texture_queries += outcome.texture_queries;
    branches += outcome.branches;
    words_before += outcome.words_before;
    words_after += outcome.words_after;
    if (outcome.fallback) {
      ++fallbacks;
    }
  }

  double FewerWordsPercent() const {
    if (!words_before) {
      return 0.0;
    }
    return 100.0 * (double(words_before) - double(words_after)) / double(words_before);
  }
};

}  // namespace gh3::native::shader_simplify
