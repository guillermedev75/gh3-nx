"""T12: chamadas `bl` entre funções traduzidas vão direto para __imp__sub_X (sem passar pelo wrapper fraco)."""
import glob
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from llamadas_diretas import rewrite  # noqa: E402


def test_bl_vira_chamada_direta():
    src = "\t// bl 0x820d3158\n\tctx.lr = 0x820D3580;\n\tsub_820D3158(ctx, base);\n"
    out = rewrite(src)
    assert "\t__imp__sub_820D3158(ctx, base);\n" in out
    assert "\tsub_820D3158(ctx, base);" not in out


def test_nao_mexe_em_definicao_nem_em_imp_nem_em_indireta():
    src = (
        "DEFINE_REX_FUNC(sub_820D10D0) {\n"
        "\t__imp__KeTlsGetValue(ctx, base);\n"
        "\tREX_CALL_INDIRECT_FUNC(ctr.u32);\n"
        "\t__imp__sub_820D3158(ctx, base);\n"
        "}\n"
    )
    assert rewrite(src) == src


def test_idempotente_e_indentacao_dupla():
    src = "\t\tsub_82000000(ctx, base);\n"
    once = rewrite(src)
    assert once == "\t\t__imp__sub_82000000(ctx, base);\n"
    assert rewrite(once) == once


def test_codigo_gerado_sem_chamada_via_wrapper():
    files = glob.glob(str(ROOT / "generated" / "default" / "gh3_recomp.*.cpp"))
    if not files:
        return  # checkout sem codegen
    pat = re.compile(r"^\s+sub_[0-9A-Fa-f]+\(ctx, base\);", re.M)
    assert sum(len(pat.findall(Path(f).read_text())) for f in files) == 0


def test_lto_ligado_no_switch():
    cm = (ROOT / "cmake" / "switch.cmake").read_text()
    assert re.search(r"-flto|INTERPROCEDURAL_OPTIMIZATION", cm)


def test_codegen_sh_roda_a_reescrita():
    assert "llamadas_diretas.py" in (ROOT / "tools" / "codegen.sh").read_text()
