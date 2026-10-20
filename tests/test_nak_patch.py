"""NAK do GM20B no driver do Switch (F2.3 do plano 60 FPS).

O nfsmw-nx mostrou que o NAK do NVK ordena cada leitura de textura/memória como
se custasse 32 ciclos; no Tegra X1 a memória é LPDDR4 compartilhada com a CPU e
uma falha de cache custa centenas. A série do project8-sw fixada (patch 0002)
traz a latência 200, o ``nir_opt_peephole_select`` com limite 8 (o 0 só achata
``if``s com os dois lados vazios) e uma revisão no cache de shaders para não
reusar binários compilados pelo driver antigo. O teste confere que a árvore que
o build do NVK compila e o ``--check`` do driver carregam esse caminho.
"""

import os
import shutil
import subprocess
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
BUILD_MESA = RAIZ / "tools" / "build_mesa.sh"
MESA = RAIZ / "out" / "mesa"

RUST_SCHED = Path("src/nouveau/compiler/nak/opt_instr_sched_common.rs")
NIR = Path("src/nouveau/compiler/nak_nir.c")
SHADER = Path("src/nouveau/vulkan/nvk_shader.c")

LATENCIA = "if sm.sm() < 70 { 200 } else { 32 }"
PEEPHOLE = ".limit = nak->sm < 70 ? 8 : 0"
REVISAO = "(UINT64_C(1) << 24)"


def _project8() -> Path:
    """Mesmo $P8 de tools/build_mesa.sh: ambiente, ~/gh3-refs, clone do setup."""
    for candidato in (
        os.environ.get("P8"),
        Path.home() / "gh3-refs" / "project8-sw",
        RAIZ / "out" / "refs" / "project8-sw",
    ):
        if candidato and (Path(candidato) / "patches" / "mesa-switch" / "README.md").is_file():
            return Path(candidato)
    raise AssertionError("sem checkout do project8-sw para ler a série do Mesa")


def _arvore_falsa(tmp_path: Path) -> Path:
    """Cópia mínima da árvore do Mesa para exercitar o --check sem Docker."""
    for rel in (RUST_SCHED, NIR, SHADER):
        destino = tmp_path / rel
        destino.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(MESA / rel, destino)
    return tmp_path


def test_serie_do_project8_traz_o_nak_do_nfsmw():
    patch = (_project8() / "patches" / "mesa-switch"
             / "0002-nak-gm20b-latency-and-flattening.patch")
    texto = patch.read_text(encoding="utf-8")
    # Duas ocorrências: ops de textura e de memória global (Ipa/CCtl/... caem no mesmo braço).
    assert texto.count(LATENCIA) == 2
    assert PEEPHOLE in texto
    assert REVISAO in texto


def test_arvore_do_mesa_do_build_esta_com_o_nak_aplicado():
    rust = (MESA / RUST_SCHED).read_text(encoding="utf-8")
    assert rust.count(LATENCIA) == 2
    assert PEEPHOLE in (MESA / NIR).read_text(encoding="utf-8")
    assert REVISAO in (MESA / SHADER).read_text(encoding="utf-8")


def test_check_do_build_mesa_confere_o_nak():
    r = subprocess.run([str(BUILD_MESA), "--check"], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "NAK" in r.stdout


def test_check_reprova_latencia_32(tmp_path):
    mesa = _arvore_falsa(tmp_path)
    rust = mesa / RUST_SCHED
    texto = rust.read_text(encoding="utf-8")
    assert texto.count(LATENCIA) == 2
    rust.write_text(texto.replace(LATENCIA, "if sm.sm() < 70 { 32 } else { 32 }"),
                    encoding="utf-8")
    r = subprocess.run([str(BUILD_MESA), "--check", "--mesa", str(mesa),
                        "--out", str(tmp_path / "sem-driver")],
                       capture_output=True, text=True)
    assert r.returncode != 0
    assert "NAK" in r.stdout + r.stderr


def test_check_reprova_peephole_zero(tmp_path):
    mesa = _arvore_falsa(tmp_path)
    nir = mesa / NIR
    texto = nir.read_text(encoding="utf-8")
    assert PEEPHOLE in texto
    nir.write_text(texto.replace(PEEPHOLE, ".limit = 0"), encoding="utf-8")
    r = subprocess.run([str(BUILD_MESA), "--check", "--mesa", str(mesa),
                        "--out", str(tmp_path / "sem-driver")],
                       capture_output=True, text=True)
    assert r.returncode != 0
    assert "NAK" in r.stdout + r.stderr
