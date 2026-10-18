"""T11: opções de codegen que põem registradores PowerPC em variáveis locais."""
import glob
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def test_flags_no_arquivo_de_includes():
    # As opções só valem no arquivo incluído pelo manifesto, não no próprio manifesto.
    cfg = (ROOT / "config" / "gh3_functions.toml").read_text()
    for flag in ("ctr_as_local", "xer_as_local", "cr_as_local"):
        assert re.search(rf"^{flag}\s*=\s*true\b", cfg, re.M), flag
    # non_volatile_as_local derruba o jogo (violação de acesso em 0x84): precisa ficar desligado.
    assert re.search(r"^non_volatile_as_local\s*=\s*false\b", cfg, re.M)


def test_codigo_gerado_sem_ctx_xer_ctr():
    files = glob.glob(str(ROOT / "generated" / "default" / "gh3_recomp.*.cpp"))
    if not files:
        return  # checkout sem codegen
    n = sum(len(re.findall(r"ctx\.(xer|ctr)\b", Path(f).read_text())) for f in files)
    assert n == 0
