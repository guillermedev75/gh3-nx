// SPDX-License-Identifier: GPL-3.0-or-later
//
// Per-draw caches of the command ring (F4.1 do plano 60 FPS, do nfsmw-nx).
//
// O consumidor do anel roda no seu próprio thread e cada microssegundo dele é CPU que o jogo
// espera (playbook §8). As três políticas aqui são puras — compilam sem SDK e sem GPU — para
// que o caminho quente seja verificável em teste:
//
//   - ImLoadCache: cache exata de microcódigo de IM_LOAD_IMMEDIATE. O caminho normal troca os
//     bytes do pacote inteiro e calcula o XXH3 para identificar o shader em cada carga; a cache
//     escolhe a via por uma assinatura barata (tamanho + quatro palavras) e só aceita com o
//     tamanho igual e memcmp de todas as palavras. Uma colisão de assinatura custa uma
//     comparação, nunca um shader errado.
//   - SamplerKey: a chave do sampler a partir da constante de fetch e do binding do shader. A
//     cache que usa essa chave (TextureCache) é membro do renderer e vale a sessão toda, então
//     um sampler criado uma vez é reusado entre quadros.
//   - SelectiveQueryReset: limpa só as queries de timestamp que o mesmo slot de submissão usou
//     da última vez (e, se este quadro usar mais, em blocos antes de cada escrita), em vez das
//     512 do pool a cada quadro perfilado.
//
// No nfsmw-nx os mesmos cortes vieram de `nfsmw_microcodigo_versiones.h`, da cache de samplers
// válida entre fotogramas e de "far fewer query pool resets" (docs/performance-history.md §5).

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gh3::native::ring_caches {

// --- IM_LOAD_IMMEDIATE ---------------------------------------------------------------

struct ImLoadStats {
  uint64_t hits = 0;
  uint64_t misses = 0;
  uint64_t evictions = 0;
};

// Exact cache of the microcode that comes in the ring (guest_address == 0). The payload is
// opaque: the ring uses it on a hit (its VulkanShader) and the cache never dereferences it.
class ImLoadCache {
 public:
  static constexpr uint32_t kSetsBits = 5;
  static constexpr uint32_t kSets = uint32_t(1) << kSetsBits;
  static constexpr uint32_t kWays = 4;
  static constexpr uint32_t kTypes = 2;

  // The entry for this exact content, or null. Counts a hit or a miss.
  void* Lookup(uint32_t type, const uint32_t* words, uint32_t count) {
    if (type >= kTypes || !count || !words) {
      ++stats_.misses;
      return nullptr;
    }
    const uint64_t signature = Signature(words, count);
    Entry* set = Set(type, signature);
    for (uint32_t i = 0; i < kWays; ++i) {
      Entry& e = set[i];
      if (e.used && e.signature == signature && e.count == count &&
          std::memcmp(e.words.data(), words, size_t(count) * sizeof(uint32_t)) == 0) {
        e.use = ++use_;
        ++stats_.hits;
        return e.payload;
      }
    }
    ++stats_.misses;
    return nullptr;
  }

  // Stores this content (a miss the caller just resolved the regular way). Never called for a
  // lookup hit. Replaces the least recently used way of its set.
  void Insert(uint32_t type, const uint32_t* words, uint32_t count, void* payload) {
    if (type >= kTypes || !count || !words) return;
    const uint64_t signature = Signature(words, count);
    Entry* set = Set(type, signature);
    Entry* way = &set[0];
    bool empty = false;
    for (uint32_t i = 0; i < kWays; ++i) {
      if (!set[i].used) {
        way = &set[i];
        empty = true;
        break;
      }
      if (set[i].use < way->use) way = &set[i];
    }
    if (!empty) ++stats_.evictions;
    way->used = true;
    way->signature = signature;
    way->count = count;
    way->payload = payload;
    way->use = ++use_;
    way->words.assign(words, words + count);
  }

  const ImLoadStats& stats() const { return stats_; }

 private:
  struct Entry {
    bool used = false;
    uint64_t signature = 0;
    uint64_t use = 0;
    uint32_t count = 0;
    void* payload = nullptr;
    std::vector<uint32_t> words;
  };

  // Cheap signature of the packet: size plus four words spread through it. It only picks the
  // set; equality is the memcmp in Lookup.
  static uint64_t Signature(const uint32_t* words, uint32_t count) {
    const uint32_t positions[4] = {0, count / 3, count * 2 / 3, count - 1};
    uint64_t h = uint64_t(count) * 0x9E3779B97F4A7C15ull;
    for (const uint32_t p : positions) {
      h = (h ^ words[p]) * 0xBF58476D1CE4E5B9ull;
      h ^= h >> 29;
    }
    return h;
  }

  Entry* Set(uint32_t type, uint64_t signature) {
    return sets_[type][size_t(signature >> (64 - kSetsBits))];
  }

  Entry sets_[kTypes][kSets][kWays];
  uint64_t use_ = 0;
  ImLoadStats stats_;
};

// --- samplers ------------------------------------------------------------------------

// The fields of the sampler state: clamp modes and border from the fetch constant, the rest
// from the shader's binding (kUseFetchConst already resolved by the caller).
struct SamplerParams {
  uint8_t clamp_x = 0;
  uint8_t clamp_y = 0;
  uint8_t clamp_z = 0;
  uint8_t border = 0;
  bool mag_linear = false;
  bool min_linear = false;
  bool mip_linear = false;
  uint8_t aniso = 0;
  uint8_t mip_min_level = 0;
  bool mip_base_map = false;
};

// The key the sampler cache uses. Stable across frames for the same state, so samplers made
// earlier are reused instead of created per draw.
constexpr uint32_t SamplerKey(const SamplerParams& p) {
  return uint32_t(p.clamp_x) | (uint32_t(p.clamp_y) << 3) | (uint32_t(p.clamp_z) << 6) |
         (uint32_t(p.border) << 9) | (uint32_t(p.mag_linear) << 11) |
         (uint32_t(p.min_linear) << 12) | (uint32_t(p.mip_linear) << 13) |
         (uint32_t(p.aniso) << 14) | (uint32_t(p.mip_min_level) << 17) |
         (uint32_t(p.mip_base_map) << 21);
}

// --- GPU profile queries -------------------------------------------------------------

// One submission slot's profile queries. Begin() resets the queries that slot wrote last time
// (the only ones that can still hold results); if this frame writes past them, NeedReset()
// returns the next block to reset before that write. A query is never written without having
// been reset in this submission, and never reset when the slot does not need it.
class SelectiveQueryReset {
 public:
  struct Plan {
    uint32_t first = 0;
    uint32_t count = 0;
  };
  static constexpr uint32_t kChunk = 32;

  explicit SelectiveQueryReset(uint32_t capacity = 512) : capacity_(capacity) {}

  // At the beginning of recording: the range to reset for this slot.
  Plan Begin(uint32_t used_last_time) {
    used_ = 0;
    reset_ = std::min(std::max(used_last_time, 1u), capacity_);
    return {0, reset_};
  }

  // Before writing query `index`: true (and the range) if it must be reset first. Once reset it
  // stays covered for the rest of this submission.
  bool NeedReset(uint32_t index, Plan* plan) {
    if (index < reset_) return false;
    const uint32_t next = std::min(capacity_, std::max(index + 1, reset_ + kChunk));
    if (next <= reset_) return false;
    plan->first = reset_;
    plan->count = next - reset_;
    reset_ = next;
    return true;
  }

  void OnWrite(uint32_t index) { used_ = std::max(used_, index + 1); }

  // Queries written this submission, closing timestamp included: what next time's Begin resets.
  uint32_t Used() const { return used_; }
  uint32_t ResetCount() const { return reset_; }

 private:
  uint32_t capacity_;
  uint32_t reset_ = 0;
  uint32_t used_ = 0;
};

// --- report --------------------------------------------------------------------------

struct Report {
  uint64_t im_hits = 0;
  uint64_t im_misses = 0;
  uint64_t im_evictions = 0;
  uint64_t samplers_created = 0;
  uint64_t query_resets = 0;
  uint64_t query_capacity = 0;
};

// One line for the periodic report (never from the ring thread).
inline std::string Format(const Report& r) {
  const uint64_t loads = r.im_hits + r.im_misses;
  const uint64_t percent = loads ? (r.im_hits * 100 + loads / 2) / loads : 0;
  std::string line = "IM_LOAD " + std::to_string(r.im_hits) + " hits/" +
                     std::to_string(loads) + " (" + std::to_string(percent) + "%, " +
                     std::to_string(r.im_evictions) + " evictions), samplers " +
                     std::to_string(r.samplers_created) + " created";
  if (r.query_capacity) {
    line += ", profile queries " + std::to_string(r.query_resets) + " reset of " +
            std::to_string(r.query_capacity) + " per slot";
  }
  return line;
}

}  // namespace gh3::native::ring_caches
