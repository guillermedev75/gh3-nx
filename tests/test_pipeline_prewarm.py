"""F2.5 (T38): prewarm de pipelines e cache versionado.

A parte pura de src/native/native_pipeline_prewarm.h (versão do cache, formato do arquivo de
prewarm, leitura validada e a linha de log) é exercitada num programa C++ compilado sozinho, ligado
ao xxhash do SDK. O aceite também abre o jogo; aqui ficam os testes baratos.
"""

import pathlib
import shutil
import subprocess

RAIZ = pathlib.Path(__file__).resolve().parents[1]
INCLUDE = RAIZ / "out" / "sdk-install" / "include"
LIB = RAIZ / "out" / "sdk-install" / "lib"

MAIN = r"""
#include "native_pipeline_prewarm.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace gh3::native::prewarm;

static Entry MakeEntry(uint64_t hash, uint8_t fill, uint32_t ps) {
  Entry e;
  e.hash = hash;
  e.key.assign(96, fill);
  e.vs_ucode.assign(40, 0xDEADBEEFu ^ uint32_t(hash));
  if (ps) e.ps_ucode.assign(ps, 0x12345678u);
  return e;
}

static int Fail(const char* what) {
  std::printf("FAIL %s\n", what);
  return 1;
}

int main(int argc, char** argv) {
  const std::string mode = argv[1];
  const uint32_t version = CacheVersion(true, true);
  if (mode == "version") {
    std::printf("base=%u opt=%u simplify=%u both=%u none=%u\n", CacheVersion(false, false),
                CacheVersion(true, false), CacheVersion(false, true),
                CacheVersion(true, true), version);
    std::printf("file=%s\n", CacheFileName(version).c_str());
    return 0;
  }
  if (mode == "stats") {
    Stats s;
    s.version = version;
    s.loaded = 97;
    s.created = 95;
    s.failed = 1;
    s.skipped = 1;
    std::printf("%s\n", s.Line().c_str());
    return 0;
  }
  if (mode == "roundtrip") {
    Manifest m;
    m.version = version;
    m.entries.push_back(MakeEntry(0x1111222233334444ull, 0xAA, 0));
    m.entries.push_back(MakeEntry(0x5555666677778888ull, 0xBB, 24));
    std::vector<uint8_t> bytes = Serialize(m);
    Manifest back;
    if (!Parse(bytes.data(), bytes.size(), version, &back)) return Fail("parse");
    if (back.version != version) return Fail("version");
    if (back.entries.size() != 2) return Fail("count");
    for (size_t i = 0; i < m.entries.size(); ++i) {
      const Entry& a = m.entries[i];
      const Entry& b = back.entries[i];
      if (a.hash != b.hash || a.key != b.key || a.vs_ucode != b.vs_ucode ||
          a.ps_ucode != b.ps_ucode) {
        return Fail("entry");
      }
    }
    std::printf("ok key=%zu vs=%zu ps=%zu\n", back.entries[0].key.size(),
                back.entries[0].vs_ucode.size(), back.entries[1].ps_ucode.size());
    return 0;
  }
  if (mode == "bad") {
    Manifest m;
    m.version = version;
    m.entries.push_back(MakeEntry(0x1111ull, 0xAA, 0));
    std::vector<uint8_t> bytes = Serialize(m);
    Manifest back;
    if (Parse(bytes.data(), bytes.size(), version + 1, &back)) return Fail("other version accepted");
    if (Parse(bytes.data(), bytes.size() / 2, version, &back)) return Fail("truncated accepted");
    if (Parse(nullptr, 0, version, &back)) return Fail("empty accepted");
    std::vector<uint8_t> corrupted = bytes;
    corrupted[corrupted.size() - 1] ^= 0xFF;
    if (Parse(corrupted.data(), corrupted.size(), version, &back)) return Fail("corrupted accepted");
    std::vector<uint8_t> extra = bytes;
    extra.push_back(0);
    if (Parse(extra.data(), extra.size(), version, &back)) return Fail("trailing bytes accepted");
    std::vector<uint8_t> bad_magic = bytes;
    bad_magic[0] ^= 0xFF;
    if (Parse(bad_magic.data(), bad_magic.size(), version, &back)) return Fail("magic accepted");
    std::printf("ok\n");
    return 0;
  }
  if (mode == "duplicates") {
    Manifest m;
    m.version = version;
    m.entries.push_back(MakeEntry(0x42ull, 0xAA, 0));
    m.entries.push_back(MakeEntry(0x42ull, 0xBB, 8));
    m.entries.push_back(MakeEntry(0x43ull, 0xCC, 0));
    std::vector<uint8_t> bytes = Serialize(m);
    Manifest back;
    if (!Parse(bytes.data(), bytes.size(), version, &back)) return Fail("parse");
    if (back.entries.size() != 2) return Fail("dedupe");
    if (back.entries[0].ps_ucode.size() != 0) return Fail("first kept");
    std::printf("ok\n");
    return 0;
  }
  if (mode == "cap") {
    // Contagem acima do teto no cabeçalho: recusa sem estourar memória.
    Manifest m;
    m.version = version;
    m.entries.push_back(MakeEntry(0x42ull, 0xAA, 0));
    std::vector<uint8_t> bytes = Serialize(m);
    uint64_t count = uint64_t(kMaxEntries) + 1;
    std::memcpy(bytes.data() + 16, &count, sizeof(count));
    // O hash do corpo também muda; recalcule para o teste chegar na checagem do teto.
    uint64_t hash = XXH3_64bits(bytes.data() + kHeaderBytes, bytes.size() - kHeaderBytes);
    std::memcpy(bytes.data() + 24, &hash, sizeof(hash));
    Manifest back;
    if (Parse(bytes.data(), bytes.size(), version, &back)) return Fail("cap accepted");
    // Uma chave maior que o teto no registro: também recusa.
    Manifest big;
    big.version = version;
    Entry e = MakeEntry(0x44ull, 0xAA, 0);
    e.key.assign(kMaxKeyBytes + 1, 0x11);
    big.entries.push_back(e);
    bytes = Serialize(big);
    if (Parse(bytes.data(), bytes.size(), version, &back)) return Fail("big key accepted");
    std::printf("ok\n");
    return 0;
  }
  return Fail("unknown mode");
}
"""


def build(tmp_path):
    inc = tmp_path / "inc"
    inc.mkdir()
    shutil.copy(RAIZ / "src" / "native" / "native_pipeline_prewarm.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "prewarm"
    subprocess.run(
        ["c++", "-std=c++20", f"-I{inc}", f"-I{INCLUDE}", str(tmp_path / "main.cpp"),
         str(LIB / "libxxhash.a"), "-o", str(exe)],
        check=True,
    )
    return exe


def run(exe, mode):
    result = subprocess.run([str(exe), mode], capture_output=True, text=True, check=True)
    return result.stdout.strip()


def test_cache_version_covers_the_shader_passes(tmp_path):
    first = run(build(tmp_path), "version").splitlines()[0]
    values = dict(item.split("=") for item in first.split())
    assert int(values["base"]) != int(values["opt"])
    assert int(values["base"]) != int(values["simplify"])
    assert int(values["opt"]) != int(values["simplify"])
    assert int(values["both"]) == int(values["opt"]) | int(values["simplify"])


def test_cache_version_is_stable_for_the_same_passes(tmp_path):
    exe = build(tmp_path)
    first = run(exe, "version")
    assert first == run(exe, "version")
    assert "file=pipelines-" in first


def test_roundtrip_keeps_keys_and_shader_code(tmp_path):
    out = run(build(tmp_path), "roundtrip")
    assert out.startswith("ok")
    assert "key=96" in out
    assert "vs=40" in out
    assert "ps=24" in out


def test_parse_rejects_other_version_truncated_corrupted_and_trailing(tmp_path):
    assert run(build(tmp_path), "bad") == "ok"


def test_duplicate_pipelines_keep_the_first_only(tmp_path):
    assert run(build(tmp_path), "duplicates") == "ok"


def test_caps_are_enforced(tmp_path):
    assert run(build(tmp_path), "cap") == "ok"


def test_report_line_names_the_cache(tmp_path):
    line = run(build(tmp_path), "stats")
    assert line.startswith("pipeline prewarm: ")
    assert "95 of 97 pipelines from the cache" in line
    assert "1 failed, 1 skipped" in line
    assert "version 0x" in line


def test_renderer_records_and_prewarms_pipelines():
    src = (RAIZ / "src" / "native" / "native_renderer.cpp").read_text()
    # Registra a chave + microcódigo de cada pipeline compilado.
    assert "RecordPipeline(" in src
    assert "prewarm::Serialize(" in src
    # Recria na inicialização, com a versão dos passes ligados.
    assert "prewarm::Parse(" in src
    assert "prewarm::CacheVersion(" in src
    assert "prewarm::CacheFileName(" in src
    # A linha do aceite sai do renderer.
    assert 'REXLOG_INFO("[native] {}", stats.Line())' in src
    # Cvar para desligar (padrão ligado: o custo é de inicialização, não de gameplay).
    assert "REXCVAR_DEFINE_BOOL(gh3_native_pipeline_prewarm, true" in src
    # O cache do driver também é versionado pelos passes.
    assert "prewarm::CacheVersion(" in src.split("InitializeDriverCache", 1)[1]


def test_system_reports_pipelines_created_after_the_prewarm():
    src = (RAIZ / "src" / "native" / "gh3_native_system.cpp").read_text()
    assert "TakePrewarmReport()" in src
    renderer = (RAIZ / "src" / "native" / "native_renderer.cpp").read_text()
    assert "std::string Renderer::TakePrewarmReport()" in renderer
    header = (RAIZ / "src" / "native" / "native_renderer.h").read_text()
    assert "TakePrewarmReport" in header
