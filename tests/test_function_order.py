"""F4.2 (T43): ordenar as funções quentes no começo do `.text` do NRO do Switch.

O A57 tem 48 KB de I-cache e uma TLB pequena; com ~53 MB de código, as funções mais chamadas
espalhadas pela imagem obrigam a andar na tabela de páginas a cada salto distante. O nfsmw-nx
agrupa as funções quentes no começo do `.text` com `--section-ordering-file`
(`app/orden_funciones.ld`, ver docs/toolchain.md §Function ordering). Aqui o mesmo mecanismo:

- `switch/orden_funciones.ld` lista as funções quentes, começando pelo `KEEP (*(.crt0))` — sem
  isso o `_start` sai do endereço 0 e o NRO não inicia;
- `tools/calientes.py` regenera a lista a partir do perfil de pilha (`rex_perfil.log`) e do ELF
  do mesmo build.

Estes testes rodam sem GPU: prendem o mecanismo (arquivo + fiação no cmake), a invariante do
`_start` no ELF já compilado (se houver) e a lógica endereço→função do gerador.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import calientes  # noqa: E402

CMAKE = (ROOT / "cmake" / "switch.cmake").read_text()
ORDEM = ROOT / "switch" / "orden_funciones.ld"
ELF = ROOT / "build" / "switch" / "gh3"

ENTRADA = re.compile(r"\*\(\.text\.([A-Za-z_][A-Za-z0-9_.]*)\)")


def _nm():
    """nm do sistema (compatível com llvm-nm) lê o ELF do Switch; None se não der."""
    exe = shutil.which("nm") or shutil.which("llvm-nm")
    if exe is None or not ELF.exists():
        return None
    out = subprocess.run([exe, str(ELF)], capture_output=True, text=True)
    if out.returncode != 0:
        return None
    syms = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                syms[parts[-1]] = int(parts[0], 16)
            except ValueError:
                pass
    return syms


# --- o arquivo de ordem -------------------------------------------------------

def _diretivas():
    """Linhas do script sem comentários/blocos e em branco."""
    texto = re.sub(r"/\*.*?\*/", "", ORDEM.read_text(), flags=re.S)
    return [l.strip() for l in texto.splitlines() if l.strip()]


def test_arquivo_existe_e_abre_o_bloco_text():
    assert ORDEM.exists(), "switch/orden_funciones.ld precisa existir"
    d = _diretivas()
    assert d[0] == ".text : {"
    assert d[-1] == "}"


def test_crt0_vem_primeiro():
    # O switch.ld do libnx põe o crt0 no começo do .text; o _start tem que continuar no
    # endereço 0 (nm ... | grep " _start$" = 0). Qualquer coisa antes do crt0 move o _start.
    d = _diretivas()
    i = d.index(".text : {")
    assert d[i + 1] == "KEEP (*(.crt0))"


def test_lista_as_funcoes_quentes_do_perfil():
    texto = ORDEM.read_text()
    # As funções mais quentes do perfil do Switch (playbook §8): o laço do ring (sub_824D0070)
    # e o cálculo em float (sub_8272C7C0).
    assert ".text.sub_824D0070" in texto
    assert ".text.sub_8272C7C0" in texto
    assert len(ENTRADA.findall(texto)) >= 5


# --- a fiação no build do Switch ---------------------------------------------

def test_cmake_liga_o_gerador_de_ordem():
    assert re.search(r"^option\(GH3_ORDEN_FUNCIONES\b[^\n]*\bON\)\s*$", CMAKE, re.M)
    assert "if(GH3_ORDEN_FUNCIONES)" in CMAKE
    assert "-Wl,--section-ordering-file,${CMAKE_SOURCE_DIR}/switch/orden_funciones.ld" in CMAKE
    assert 'set_property(TARGET gh3 APPEND PROPERTY LINK_DEPENDS' in CMAKE
    assert "switch/orden_funciones.ld" in CMAKE


def test_cmake_tem_o_ab_de_o2_do_codigo_traduzido():
    # -O3 engorda o código (o A57 tem 48 KB de I-cache): A/B opcional só no gh3_recomp, como
    # o NFSMW_RECOMP_O2 do nfsmw-nx. Padrão desligado.
    assert re.search(r"^option\(GH3_RECOMP_O2\b[^\n]*\bOFF\)\s*$", CMAKE, re.M)
    assert re.search(r"if\(GH3_RECOMP_O2 AND TARGET gh3_recomp\)", CMAKE)
    assert re.search(r"target_compile_options\(gh3_recomp PRIVATE -O2\)", CMAKE)


# --- o binário (quando existe) ------------------------------------------------

def test_start_continua_no_endereco_zero():
    syms = _nm()
    if syms is None:
        return  # sem ELF do Switch nesta máquina: nada a conferir
    assert "_start" in syms, "o ELF precisa ter _start"
    assert syms["_start"] == 0, "o _start tem que ficar no endereço 0"


def test_entradas_da_lista_existem_no_elf():
    syms = _nm()
    if syms is None:
        return
    faltando = sorted({n for n in ENTRADA.findall(ORDEM.read_text()) if n not in syms})
    assert not faltando, f"nomes no orden_funciones.ld que não existem no ELF: {faltando}"


# --- a lógica do gerador (pura) -----------------------------------------------

PERFIL = """\
imagen 0x1000, codigo hasta 0x4000
==== 10.0 s | muestras 100
-- hilo 1 "Main": CPU 50.0%
   100 muestras, 0% esperando en el kernel
    60.0%  pc imagen+0x1100
    30.0%  pc imagen+0x1200
    10.0%  lr imagen+0x1200 imagen+0x1100
"""

SIMBOLOS = """\
0000000000001000 T sub_AAA
00000000000010f0 T sub_BBB
00000000000011f0 T sub_CCC
0000000000002000 T sub_DDD
"""


def test_perfil_soma_as_amostras_por_endereco():
    peso = calientes.parse_profile(PERFIL)
    # pc 0x1100 = 60%, pc 0x1200 = 30%, e o lr distribui 10% entre 0x1200 e 0x1100
    assert peso[0x1100] == 65.0
    assert peso[0x1200] == 35.0
    assert peso[0x2000] == 0.0


def test_resolve_endereco_para_a_funcao_que_o_contem():
    nomes = calientes.resolve(calientes.parse_profile(PERFIL),
                              calientes.parse_symbols(SIMBOLOS))
    assert nomes[:2] == ["sub_BBB", "sub_CCC"]


def test_gera_o_ld_com_crt0_primeiro_e_quentes_na_ordem():
    ld = calientes.format_ordering(["sub_BBB", "sub_CCC"])
    d = [l.strip() for l in re.sub(r"/\*.*?\*/", "", ld, flags=re.S).splitlines() if l.strip()]
    assert d[0] == ".text : {"
    assert d[1] == "KEEP (*(.crt0))"
    assert d[2] == "*(.text.sub_BBB)"
    assert d[3] == "*(.text.sub_CCC)"
    # o .text.sub_BBB aparece antes do .text.sub_CCC no texto (ordem quente primeiro)
    assert ld.index("(.text.sub_BBB)") < ld.index("(.text.sub_CCC)")
