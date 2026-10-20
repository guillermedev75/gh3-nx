"""F4.1 do plano 60 FPS: caches por draw no anel (IM_LOAD, sampler, query, zero log).

O consumidor do anel roda num thread só e cada microssegundo dele é CPU que o jogo espera
(playbook §8). Do nfsmw-nx vêm quatro cortes de CPU, e aqui eles ficam presos por testes:

- **IM_LOAD_IMMEDIATE**: o caminho normal troca os bytes do microcódigo inteiro e calcula o
  XXH3 para identificar o shader a cada pacote. A cache escolhe uma via por uma assinatura
  barata de 4 palavras e só aceita se o tamanho e **todas** as palavras forem iguais
  (memcmp): colisão de assinatura custa uma comparação, nunca um shader errado.
- **sampler**: a chave do sampler (constante de fetch + binding do shader) é pura e a cache
  do `TextureCache` vale entre quadros: um sampler criado uma vez é reusado.
- **query**: o perfil de GPU (`gh3_native_gpu_profile`) limpava 512 queries por quadro
  perfilado; agora limpa só as que o mesmo slot usou da última vez (e, se o quadro usar mais,
  em blocos antes de cada escrita).
- **zero log**: os probes `[T9]` (com `static std::unordered_set` por draw/resolve) saíram do
  caminho quente; o log do anel fica só no relatório periódico.

A política é pura e compila sem SDK nem GPU (como `native_present_ring.h`).
"""
import pathlib
import shutil
import subprocess

RAIZ = pathlib.Path(__file__).resolve().parent.parent
NATIVE = RAIZ / "src" / "native"
RENDERER = NATIVE / "native_renderer.cpp"
TEXTURES = NATIVE / "native_textures.cpp"
SISTEMA = NATIVE / "gh3_native_system.cpp"

MAIN = r"""
#include "native_ring_caches.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace gh3::native::ring_caches;

static std::vector<uint32_t> Microcode(uint32_t count, uint32_t middle) {
  std::vector<uint32_t> words(count);
  for (uint32_t i = 0; i < count; ++i) words[i] = 0x1000u + i;  // posicoes amostradas distintas
  words[0] = 0xA1B2C3D4u;
  words[count / 3] = 0x11223344u;
  words[count * 2 / 3] = 0x55667788u;
  words[count - 1] = 0x99AABBCCu;
  words[5] = middle;
  return words;
}

static int ImHit() {
  ImLoadCache cache;
  std::vector<uint32_t> words = Microcode(12, 1);
  int payload = 42;
  if (cache.Lookup(0, words.data(), 12) != nullptr) return 1;
  cache.Insert(0, words.data(), 12, &payload);
  if (cache.Lookup(0, words.data(), 12) != &payload) return 2;
  if (cache.stats().hits != 1 || cache.stats().misses != 1) return 3;
  // Um tipo diferente e outro shader (mais uma falta).
  if (cache.Lookup(1, words.data(), 12) != nullptr) return 4;
  if (cache.stats().misses != 2) return 5;
  return 0;
}

static int ImSignature() {
  ImLoadCache cache;
  std::vector<uint32_t> a = Microcode(12, 1);
  std::vector<uint32_t> b = Microcode(12, 2);  // mesma assinatura (palavra 5 nao e amostrada)
  int payload = 7;
  if (cache.Lookup(0, a.data(), 12) != nullptr) return 1;
  cache.Insert(0, a.data(), 12, &payload);
  if (cache.Lookup(0, a.data(), 12) != &payload) return 2;
  if (cache.Lookup(0, b.data(), 12) != nullptr) return 3;  // memcmp, nao a assinatura
  if (cache.stats().hits != 1) return 4;
  return 0;
}

static int ImEvict() {
  ImLoadCache cache;
  int payloads[6] = {0};
  for (uint32_t i = 1; i <= ImLoadCache::kWays; ++i) {
    std::vector<uint32_t> words = Microcode(12, i);  // todas no mesmo conjunto
    cache.Insert(0, words.data(), 12, &payloads[i]);
  }
  // Toca a via 1 para ela virar a mais recente.
  std::vector<uint32_t> one = Microcode(12, 1);
  if (cache.Lookup(0, one.data(), 12) != &payloads[1]) return 1;
  std::vector<uint32_t> five = Microcode(12, 5);
  cache.Insert(0, five.data(), 12, &payloads[5]);  // expulsa a 2 (menos recente)
  if (cache.Lookup(0, one.data(), 12) != &payloads[1]) return 2;
  std::vector<uint32_t> two = Microcode(12, 2);
  if (cache.Lookup(0, two.data(), 12) != nullptr) return 3;
  if (cache.stats().evictions != 1) return 4;
  return 0;
}

static uint32_t Key(uint8_t clamp_x, uint8_t clamp_y, uint8_t clamp_z, uint8_t border,
                    bool mag, bool min, bool mip, uint8_t aniso, uint8_t mip_min, bool base) {
  SamplerParams p;
  p.clamp_x = clamp_x;
  p.clamp_y = clamp_y;
  p.clamp_z = clamp_z;
  p.border = border;
  p.mag_linear = mag;
  p.min_linear = min;
  p.mip_linear = mip;
  p.aniso = aniso;
  p.mip_min_level = mip_min;
  p.mip_base_map = base;
  return SamplerKey(p);
}

static int Sampler() {
  const uint32_t base = Key(1, 2, 3, 1, true, false, true, 4, 5, false);
  if (base != Key(1, 2, 3, 1, true, false, true, 4, 5, false)) return 1;  // estavel
  // Cada campo tem sua faixa: mudar qualquer um muda a chave.
  if (base == Key(2, 2, 3, 1, true, false, true, 4, 5, false)) return 2;
  if (base == Key(1, 3, 3, 1, true, false, true, 4, 5, false)) return 3;
  if (base == Key(1, 2, 4, 1, true, false, true, 4, 5, false)) return 4;
  if (base == Key(1, 2, 3, 2, true, false, true, 4, 5, false)) return 5;
  if (base == Key(1, 2, 3, 1, false, false, true, 4, 5, false)) return 6;
  if (base == Key(1, 2, 3, 1, true, true, true, 4, 5, false)) return 7;
  if (base == Key(1, 2, 3, 1, true, false, false, 4, 5, false)) return 8;
  if (base == Key(1, 2, 3, 1, true, false, true, 5, 5, false)) return 9;
  if (base == Key(1, 2, 3, 1, true, false, true, 4, 6, false)) return 10;
  if (base == Key(1, 2, 3, 1, true, false, true, 4, 5, true)) return 11;
  return 0;
}

static int Query() {
  SelectiveQueryReset q(512);
  SelectiveQueryReset::Plan plan = q.Begin(11);  // 10 escritas + o carimbo de fechamento
  if (plan.first != 0 || plan.count != 11) return 1;
  SelectiveQueryReset::Plan growth;
  for (uint32_t i = 0; i < 11; ++i) {
    if (q.NeedReset(i, &growth)) return 2;
    q.OnWrite(i);
  }
  if (q.Used() != 11) return 3;
  if (!q.NeedReset(11, &growth)) return 4;
  if (growth.first != 11 || growth.count != SelectiveQueryReset::kChunk) return 5;
  if (q.NeedReset(11, &growth)) return 6;  // ja coberto pelo bloco
  q.OnWrite(11);
  if (q.Used() != 12) return 7;
  return 0;
}

static int QueryEdges() {
  SelectiveQueryReset q(8);
  SelectiveQueryReset::Plan plan = q.Begin(0);  // primeira vez no slot
  if (plan.first != 0 || plan.count != 1) return 1;
  SelectiveQueryReset::Plan growth;
  if (q.NeedReset(0, &growth)) return 2;
  if (!q.NeedReset(3, &growth)) return 3;
  if (growth.first != 1 || growth.count != 7) return 4;  // limitado pela capacidade
  if (q.NeedReset(7, &growth)) return 5;
  if (q.Used() != 0) return 6;
  SelectiveQueryReset::Plan again = q.Begin(20);
  if (again.first != 0 || again.count != 8) return 7;
  return 0;
}

int main(int argc, char** argv) {
  std::string mode = argv[1];
  int r = 99;
  if (mode == "im_hit") r = ImHit();
  else if (mode == "im_signature") r = ImSignature();
  else if (mode == "im_evict") r = ImEvict();
  else if (mode == "sampler") r = Sampler();
  else if (mode == "query") r = Query();
  else if (mode == "query_edges") r = QueryEdges();
  std::printf("%d\n", r);
  return r;
}
"""


def build(tmp_path):
    inc = tmp_path / "inc"
    inc.mkdir(exist_ok=True)
    shutil.copy(NATIVE / "native_ring_caches.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "ring_caches"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o",
                    str(exe)], check=True)
    return exe


def run(tmp_path, mode):
    result = subprocess.run([str(build(tmp_path)), mode], capture_output=True, text=True)
    return result.returncode, result.stdout.strip(), result.stderr.strip()


def test_im_load_cache_hits_an_exact_repeat(tmp_path):
    code, out, err = run(tmp_path, "im_hit")
    assert code == 0, err


def test_im_load_cache_never_trusts_the_signature_alone(tmp_path):
    code, out, err = run(tmp_path, "im_signature")
    assert code == 0, err


def test_im_load_cache_evicts_the_least_recently_used_way(tmp_path):
    code, out, err = run(tmp_path, "im_evict")
    assert code == 0, err


def test_sampler_key_is_stable_and_covers_every_field(tmp_path):
    code, out, err = run(tmp_path, "sampler")
    assert code == 0, err


def test_query_reset_only_touches_what_the_slot_used_last_time(tmp_path):
    code, out, err = run(tmp_path, "query")
    assert code == 0, err


def test_query_reset_first_use_chunks_and_capacity(tmp_path):
    code, out, err = run(tmp_path, "query_edges")
    assert code == 0, err


def test_renderer_uses_the_caches_on_the_ring():
    src = RENDERER.read_text(encoding="utf-8")
    assert '#include "native_ring_caches.h"' in src
    # IM_LOAD_IMMEDIATE (guest_address == 0): cache antes de trocar bytes e hashear.
    assert "im_load_cache" in src
    assert "ring_caches::ImLoadCache" in src
    assert "guest_address == 0" in src or "!guest_address" in src
    # Queries do perfil: limpeza seletiva, nunca o pool inteiro por quadro.
    assert "ring_caches::SelectiveQueryReset" in src
    assert "profile_queries" in src
    assert "vkCmdResetQueryPool(s.setup, profile_pool, slot * kProfileQueries,\n" \
           "                                  kProfileQueries);" not in src
    # Relatório periódico (fora do anel), não log por draw.
    assert "ring caches:" in src


def test_textures_reuse_samplers_across_frames():
    src = TEXTURES.read_text(encoding="utf-8")
    assert '#include "native_ring_caches.h"' in src
    assert "ring_caches::SamplerKey" in src
    assert "ring_caches::SamplerParams" in src
    # A cache é membro do Impl (vive a sessão toda), não local de GetSampler.
    assert "std::unordered_map<uint32_t, VkSampler> samplers;" in src
    assert "auto it = im.samplers.find(key);" in src
    assert "im.samplers.emplace(key, sampler);" in src
    # Contador no relatório prova o reuso: samplers criados, não por draw.
    assert "samplers_created" in src


def test_no_diagnostic_logging_left_on_the_ring():
    src = RENDERER.read_text(encoding="utf-8")
    assert "[T9]" not in src
    assert "static std::unordered_set<uint64_t> seen;" not in src


def test_report_logs_the_ring_caches():
    src = SISTEMA.read_text(encoding="utf-8")
    assert "TakeRingCacheReport()" in src
    assert "ring caches:" in RENDERER.read_text(encoding="utf-8")
