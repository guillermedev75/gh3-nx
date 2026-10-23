// SPDX-License-Identifier: GPL-3.0-or-later
//
// F2.5 do plano de 60 FPS: prewarm de pipelines e cache versionado.
//
// O driver leva de 59 a 160 ms para compilar um pipeline novo no Switch. O renderer já compila fora
// do thread do anel (gh3_native_async_pipelines) e não trava o quadro, mas os draws que precisam do
// pipeline ficam sem desenhar até ele existir — o primeiro uso de cada estado de pipeline aparece
// como um objeto faltando por um quadro ou dois. Aqui o renderer guarda, junto do cache do driver,
// as chaves de pipeline que já usou com o microcódigo dos shaders; numa execução seguinte ele recria
// esses pipelines na inicialização (antes da gameplay), então o primeiro uso no jogo já encontra o
// pipeline no mapa.
//
// O arquivo é um cache local da máquina, ao lado do cache do driver (ambos derivam do jogo e nunca
// vão para o repositório nem para o pacote do SD). Este header é a parte pura (formato do arquivo e
// da versão), testável sem SDK nem GPU.
//
// A versão cobre tudo o que muda o binário que o driver compila: o formato da chave de pipeline do
// renderer e os passes de shader que alteram o SPIR-V antes do driver (spirv-opt do F2.2 e a
// simplificação do F2.4). Um cache de outra versão não é lido.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <xxhash.h>

namespace gh3::native::prewarm {

// Subir quando o layout da chave de pipeline (PipelineKey) ou deste arquivo mudar.
constexpr uint32_t kFormatVersion = 1;
constexpr uint32_t kPassSpirvOpt = 1u << 0;
constexpr uint32_t kPassShaderSimplify = 1u << 1;
// Teto do arquivo: um jogo inteiro viu ~100 pipelines; o teto protege contra um arquivo corrompido
// ou um cache que cresceu sem limite.
constexpr uint32_t kMaxEntries = 4096;
constexpr size_t kMaxKeyBytes = 256;
constexpr size_t kMaxUcodeDwords = 32 * 1024;
constexpr uint64_t kManifestMagic = 0x314D525750334748ULL;  // "HG3PWRM1"
constexpr size_t kHeaderBytes = 4 * sizeof(uint64_t);

// kFormatVersion nos bits altos, os passes nos baixos: a troca de um passe muda o nome do cache do
// driver e a versão do arquivo de prewarm, então nada antigo é reaproveitado.
inline uint32_t CacheVersion(bool spirv_opt, bool shader_simplify) {
  uint32_t version = kFormatVersion << 8;
  if (spirv_opt) version |= kPassSpirvOpt;
  if (shader_simplify) version |= kPassShaderSimplify;
  return version;
}

inline std::string HexVersion(uint32_t version) {
  static const char* const kDigits = "0123456789ABCDEF";
  std::string text;
  bool started = false;
  for (int shift = 28; shift >= 0; shift -= 4) {
    uint32_t digit = (version >> shift) & 0xF;
    if (digit || started || shift == 0) {
      text.push_back(kDigits[digit]);
      started = true;
    }
  }
  return text;
}

inline std::string CacheFileName(uint32_t version) {
  return "pipelines-" + HexVersion(version) + ".bin";
}

// Uma chave de pipeline que já foi compilada, com o microcódigo dos shaders que ela usa (o
// microcódigo é guardado no formato nativo do host, como o Shader do SDK).
struct Entry {
  uint64_t hash = 0;
  std::vector<uint8_t> key;        // PipelineKey, memória plana
  std::vector<uint32_t> vs_ucode;  // vertex shader
  std::vector<uint32_t> ps_ucode;  // pixel shader; vazio quando não há
};

struct Manifest {
  uint32_t version = 0;
  std::vector<Entry> entries;
};

// Cabeçalho: magic, versão, número de entradas e XXH3 do corpo.
inline std::vector<uint8_t> Serialize(const Manifest& manifest) {
  std::vector<uint8_t> body;
  body.reserve(manifest.entries.size() * 64);
  auto append = [&body](const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    body.insert(body.end(), bytes, bytes + size);
  };
  for (const Entry& entry : manifest.entries) {
    const uint32_t key_bytes = uint32_t(entry.key.size());
    const uint32_t vs_dwords = uint32_t(entry.vs_ucode.size());
    const uint32_t ps_dwords = uint32_t(entry.ps_ucode.size());
    const uint32_t reserved = 0;
    append(&entry.hash, sizeof(entry.hash));
    append(&key_bytes, sizeof(key_bytes));
    append(&vs_dwords, sizeof(vs_dwords));
    append(&ps_dwords, sizeof(ps_dwords));
    append(&reserved, sizeof(reserved));
    if (key_bytes) append(entry.key.data(), key_bytes);
    if (vs_dwords) append(entry.vs_ucode.data(), size_t(vs_dwords) * sizeof(uint32_t));
    if (ps_dwords) append(entry.ps_ucode.data(), size_t(ps_dwords) * sizeof(uint32_t));
  }
  std::vector<uint8_t> bytes;
  bytes.reserve(kHeaderBytes + body.size());
  const uint64_t header[4] = {kManifestMagic, uint64_t(manifest.version),
                              uint64_t(manifest.entries.size()),
                              XXH3_64bits(body.data(), body.size())};
  auto append_bytes = [&bytes](const void* data, size_t size) {
    const uint8_t* source = static_cast<const uint8_t*>(data);
    bytes.insert(bytes.end(), source, source + size);
  };
  append_bytes(header, sizeof(header));
  append_bytes(body.data(), body.size());
  return bytes;
}

// Lê e valida o arquivo. Um arquivo vazio, truncado, de outra versão ou com hash diferente é
// recusado (o renderer segue sem prewarm). Entradas repetidas (mesma chave) ficam só com a primeira.
inline bool Parse(const uint8_t* data, size_t size, uint32_t expected_version, Manifest* out) {
  if (!out) return false;
  out->version = expected_version;
  out->entries.clear();
  if (!data || size < kHeaderBytes) return false;
  uint64_t header[4];
  std::memcpy(header, data, sizeof(header));
  if (header[0] != kManifestMagic) return false;
  if (header[1] != uint64_t(expected_version)) return false;
  if (header[2] > uint64_t(kMaxEntries)) return false;
  size_t body_bytes = size - kHeaderBytes;
  if (header[3] != uint64_t(XXH3_64bits(data + kHeaderBytes, body_bytes))) return false;
  out->entries.reserve(size_t(header[2]));
  size_t offset = kHeaderBytes;
  for (uint64_t i = 0; i < header[2]; ++i) {
    if (body_bytes - (offset - kHeaderBytes) < 24) return false;
    Entry entry;
    uint32_t key_bytes = 0;
    uint32_t vs_dwords = 0;
    uint32_t ps_dwords = 0;
    uint32_t reserved = 0;
    std::memcpy(&entry.hash, data + offset, sizeof(entry.hash));
    std::memcpy(&key_bytes, data + offset + 8, sizeof(key_bytes));
    std::memcpy(&vs_dwords, data + offset + 12, sizeof(vs_dwords));
    std::memcpy(&ps_dwords, data + offset + 16, sizeof(ps_dwords));
    std::memcpy(&reserved, data + offset + 20, sizeof(reserved));
    offset += 24;
    if (!key_bytes || key_bytes > kMaxKeyBytes) return false;
    if (!vs_dwords || vs_dwords > kMaxUcodeDwords) return false;
    if (ps_dwords > kMaxUcodeDwords) return false;
    size_t bytes = size_t(key_bytes) + (size_t(vs_dwords) + size_t(ps_dwords)) * sizeof(uint32_t);
    if (body_bytes - (offset - kHeaderBytes) < bytes) return false;
    entry.key.assign(data + offset, data + offset + key_bytes);
    offset += key_bytes;
    entry.vs_ucode.resize(vs_dwords);
    if (vs_dwords) std::memcpy(entry.vs_ucode.data(), data + offset, size_t(vs_dwords) * 4);
    offset += size_t(vs_dwords) * 4;
    if (ps_dwords) {
      entry.ps_ucode.resize(ps_dwords);
      std::memcpy(entry.ps_ucode.data(), data + offset, size_t(ps_dwords) * 4);
      offset += size_t(ps_dwords) * 4;
    }
    bool duplicate = false;
    for (const Entry& known : out->entries) {
      if (known.hash == entry.hash) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) out->entries.push_back(std::move(entry));
  }
  if (offset != size) return false;
  return true;
}

// Quanto o prewarm pode fazer num quadro sem segurar o jogo: o custo de uma entrada é medido das
// que já passaram (média móvel) e o plano leva o que cabe no orçamento do quadro, com o mínimo de
// uma entrada para haver progresso e o máximo de `max_batch` por quadro. Uma entrada mais cara que
// o orçamento inteiro ainda é levada: parar por causa dela significaria nunca terminar.
class Budget {
 public:
  Budget(double budget_ms, uint32_t max_batch) : budget_ms_(budget_ms), max_batch_(max_batch) {}

  uint32_t Take(uint32_t pending) const {
    if (!pending || max_batch_ == 0) return 0;
    uint32_t fits = max_batch_;
    if (samples_ && average_ms_ > 0.0) {
      const double budget = budget_ms_ > 0.0 ? budget_ms_ : 0.0;
      fits = uint32_t(budget / average_ms_);
      if (fits == 0) fits = 1;
    }
    return std::min(pending, std::min(fits, max_batch_));
  }

  void Record(double ms) {
    if (ms < 0.0) ms = 0.0;
    ++samples_;
    average_ms_ += (ms - average_ms_) / double(samples_);
  }

  double Average() const { return average_ms_; }
  uint32_t Samples() const { return samples_; }

 private:
  double budget_ms_;
  uint32_t max_batch_;
  double average_ms_ = 0.0;
  uint32_t samples_ = 0;
};

// O que aconteceu no prewarm de uma execução; a linha sai uma vez na inicialização e de novo no
// relatório de 10 s com os pipelines criados depois dela.
struct Stats {
  uint32_t version = 0;
  uint32_t loaded = 0;   // entradas lidas do arquivo
  uint32_t created = 0;  // pipelines recriados
  uint32_t failed = 0;   // entradas que não deram pipeline
  uint32_t skipped = 0;  // entradas descartadas antes de compilar (shader não confere, chave estranha)

  std::string Line() const {
    return "pipeline prewarm: " + std::to_string(created) + " of " + std::to_string(loaded) +
           " pipelines from the cache (version 0x" + HexVersion(version) + ", " +
           std::to_string(failed) + " failed, " + std::to_string(skipped) + " skipped)";
  }
};

}  // namespace gh3::native::prewarm
