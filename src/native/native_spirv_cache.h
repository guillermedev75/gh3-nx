// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Cache em disco do SPIR-V já simplificado/otimizado (2026-10-23).
//
// No Switch o spirv-opt é obrigatório (sem ele o NAK leva segundos por pipeline), mas roda a cada abertura
// em ~160 shaders junto com o pré-aquecimento: ~40 s com um núcleo a 100 %, intro lenta e áudio cortando.
// Aqui o resultado fica guardado no SD (ao lado do cache de pipelines do driver), um arquivo por shader:
// chave = hash do SPIR-V que o tradutor gerou + os passes ligados. A partir da segunda abertura o trabalho
// vira leitura de arquivo. Arquivo inválido ou truncado é ignorado (o shader é otimizado de novo).

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace gh3::native::spirv_cache {

// Sobe quando o formato ou o que os passes produzem mudar (invalida o cache inteiro).
inline constexpr uint32_t kFormat = 1;
inline constexpr uint32_t kSpirvMagic = 0x07230203;

// FNV-1a 64 sobre as palavras do módulo de entrada e os passes ligados.
inline uint64_t Key(const std::vector<uint32_t>& input, bool simplify, bool optimize) {
  uint64_t h = 0xcbf29ce484222325ull;
  auto mix = [&h](uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      h ^= (v >> (i * 8)) & 0xFF;
      h *= 0x100000001b3ull;
    }
  };
  mix(kFormat);
  mix((simplify ? 1u : 0u) | (optimize ? 2u : 0u));
  mix(uint32_t(input.size()));
  for (uint32_t w : input) mix(w);
  return h;
}

inline std::string FileName(uint64_t key) {
  char name[32];
  std::snprintf(name, sizeof(name), "%016llx.spv", static_cast<unsigned long long>(key));
  return name;
}

// Um módulo SPIR-V plausível: cabeçalho de 5 palavras e a magia.
inline bool Valid(const std::vector<uint32_t>& binary) {
  return binary.size() >= 5 && binary[0] == kSpirvMagic;
}

inline bool Load(const std::filesystem::path& dir, uint64_t key, std::vector<uint32_t>* out) {
  std::ifstream file(dir / FileName(key), std::ios::binary | std::ios::ate);
  if (!file) return false;
  const std::streamoff size = file.tellg();
  if (size <= 0 || size % 4) return false;
  std::vector<uint32_t> data(size_t(size) / 4);
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(data.data()), size)) return false;
  if (!Valid(data)) return false;
  *out = std::move(data);
  return true;
}

// Grava em .tmp e renomeia: um arquivo pela metade nunca fica com o nome final. No Horizon o rename não
// sobrescreve, então o destino antigo sai antes.
inline bool Store(const std::filesystem::path& dir, uint64_t key, const std::vector<uint32_t>& binary) {
  if (!Valid(binary)) return false;
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  const auto path = dir / FileName(key);
  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(binary.data()), std::streamsize(binary.size() * 4));
    if (!file) return false;
  }
  std::filesystem::remove(path, error);
  std::filesystem::rename(temporary, path, error);
  return !error;
}

}  // namespace gh3::native::spirv_cache
