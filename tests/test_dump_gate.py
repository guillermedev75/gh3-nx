import hashlib, shutil, subprocess
from pathlib import Path

# A decisão da trava (src/dump_gate.h) compilada sozinha, com uma tabela de teste no lugar do
# src/supported_dumps.h gerado: sha256 e tamanho de um "default.xex" de mentira.
FAKE = b"XEX2 de mentira"
TABLE = f"""#pragma once
#include <cstdint>
#include <string_view>
namespace gh3::identify {{
struct SupportedDump {{ std::string_view sha256; std::string_view title_id; std::string_view release; uint64_t xex_size; }};
inline constexpr SupportedDump kSupportedDumps[] = {{
    {{"{hashlib.sha256(FAKE).hexdigest()}", "0x415607F7", "teste", {len(FAKE)}}},
}};
}}
"""
MAIN = r"""
#include "dump_gate.h"
#include <cstdio>
int main(int, char** argv) {
  auto v = dump_gate::Classify(argv[1], std::stoull(argv[2]));
  std::printf("%d\n", static_cast<int>(v));
}
"""

def build(tmp_path):
    inc = tmp_path / "inc"; inc.mkdir()
    shutil.copy("src/dump_gate.h", inc)
    (inc / "supported_dumps.h").write_text(TABLE)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "gate"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)], check=True)
    return exe

def verdict(exe, digest, size):
    return int(subprocess.run([str(exe), digest, str(size)], capture_output=True, text=True, check=True).stdout)

def test_classify(tmp_path):
    exe = build(tmp_path)
    ok, wrong_build, unknown = 0, 4, 5  # Verdict::kOk, kWrongBuild, kUnknown
    good = hashlib.sha256(FAKE).hexdigest()
    assert verdict(exe, good, len(FAKE)) == ok
    assert verdict(exe, "0" * 64, len(FAKE)) == wrong_build
    assert verdict(exe, "0" * 64, 123) == unknown

def test_app_enforces_gate_before_runtime():
    app = Path("src/gh3_app.h").read_text()
    assert "void OnConfigurePaths(rex::PathConfig& paths) override" in app
    assert "dump_gate::Enforce(paths.game_data_root)" in app
    # sem flag que desligue a trava
    assert "skip_dump" not in app

def test_committed_table_has_one_gh3_row():
    h = Path("src/supported_dumps.h").read_text()
    assert "namespace gh3::identify" in h and h.count('"0x415607F7"') >= 1
