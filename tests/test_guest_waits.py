"""T13: as esperas ativas do jogo cedem o núcleo em vez de girar (src/guest_waits.h).

Três conferências:
  1. o detector puro (src/guest_waits_spin.h) compilado sozinho, sem SDK;
  2. os endereços hookados são os mesmos que tools/llamadas_diretas.py mantém fora da reescrita;
  3. no gerado, os accessors nesses endereços continuam sendo as folhas que leem os contadores.
"""
import glob
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from llamadas_diretas import HOOKED  # noqa: E402

MAIN = r"""
#include "guest_waits_spin.h"
#include <cstdio>
#include <cstdlib>
using namespace gh3::guest_waits;
int main(int argc, char** argv) {
  const unsigned after = std::atoi(argv[1]);
  SpinDetector d;
  for (int i = 2; i + 1 < argc; i += 2) {
    const unsigned long long v = std::strtoull(argv[i], nullptr, 10);
    const int n = std::atoi(argv[i + 1]);
    for (int k = 0; k < n; ++k) std::printf("%d", d.OnValue(v, after) ? 1 : 0);
  }
  std::printf("\n");
}
"""


def build(tmp_path):
    inc = tmp_path / "inc"; inc.mkdir()
    shutil.copy(ROOT / "src" / "guest_waits_spin.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "spin"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)],
                   check=True)
    return exe


def run(exe, *args):
    return subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True,
                          check=True).stdout.strip()


def test_yields_on_the_nth_identical_read(tmp_path):
    # after=4: a 1a leitura fixa o valor; a 4a repetição (5a leitura) cede. depois recomeça.
    assert run(build(tmp_path), 4, 42, 6) == "000010"


def test_a_different_value_resets_the_count(tmp_path):
    # 43 no meio zera a repetição; as 42 seguintes recomeçam do zero.
    assert run(build(tmp_path), 4, 42, 3, 43, 1, 42, 3) == "0000000"


def test_after_one_yields_on_every_repeat(tmp_path):
    assert run(build(tmp_path), 1, 7, 4) == "0111"


def test_hooked_addresses_match_the_header():
    text = (ROOT / "src" / "guest_waits.h").read_text()
    swap = int(re.search(r"kSwapCounter = 0x([0-9A-Fa-f]+)", text).group(1), 16)
    vblank = int(re.search(r"kVblankCounter = 0x([0-9A-Fa-f]+)", text).group(1), 16)
    assert {swap, vblank} == HOOKED


def _bodies():
    out = {}
    for f in glob.glob(str(ROOT / "generated" / "default" / "gh3_recomp.*.cpp")):
        text = Path(f).read_text()
        for addr, off in ((0x82172718, 9512), (0x82172758, 9504)):
            m = re.search(rf"DEFINE_REX_FUNC\(sub_{addr:08X}\) \{{(.*?)\n\}}", text, re.S)
            if m:
                out[addr] = (m.group(1), off)
    return out


def test_generated_accessors_are_the_counter_leaves():
    bodies = _bodies()
    if not bodies:
        return  # checkout sem codegen
    for addr, (body, off) in bodies.items():
        # lis r11,-32082 (0x82AE0000) + ld r3,<off>(r11) + blr: a folha que lê o contador
        assert "-32082" in body, f"sub_{addr:08X} nao usa a base 0x82AE0000"
        assert f"REX_LOAD_U64(ctx.r11.u32 + {off})" in body, \
            f"sub_{addr:08X} nao le o offset {off}"


def test_install_logs_the_acceptance_marker():
    # o aceite do T13 faz grep de "guest waits: installed" no log da captura
    assert '"guest waits: installed' in (ROOT / "src" / "guest_waits.h").read_text()
