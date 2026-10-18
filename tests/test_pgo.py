"""PGO (GH3_PGO=gerar/usar) no build do Switch.

O perfil tem que casar entre os dois builds: mesmas flags, mesmos caminhos, LTO desligado na
gravação. Estes testes travam as escolhas que o nfsmw pagou caro para descobrir.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CMAKE = (ROOT / "cmake" / "switch.cmake").read_text()
PGO = (ROOT / "src" / "pgo.cpp").read_text()


def test_gerar_desliga_lto_e_grava_o_perfil():
    assert "GH3_PGO STREQUAL \"gerar\"" in CMAKE
    assert "-fprofile-generate=${GH3_PGO_DIR}" in CMAKE
    assert "-fprofile-update=single" in CMAKE
    assert "-fno-profile-values" in CMAKE          # tpidr_el0 e' 0 no Horizon
    assert "-Wl,--wrap=fopen" in CMAKE
    # LTO fica de fora justamente no build de gravacao
    assert "if(NOT GH3_PGO STREQUAL \"gerar\")" in CMAKE


def test_usar_relê_o_perfil_e_corrige_contadores():
    assert "-fprofile-use=${GH3_PGO_DIR}" in CMAKE
    assert "-fprofile-partial-training" in CMAKE
    assert "-fprofile-correction" in CMAKE         # contadores nao atomicos ficam negativos


def test_app_desvia_os_gcda_para_o_sd():
    # GCC embute <GH3_PGO_DIR>/...#arquivo.gcda, que nao existe no Switch.
    assert "GH3_PGO_DIR" in PGO
    assert "__wrap_fopen" in PGO
    assert "__real_fopen" in PGO
    assert '"sdmc:/switch/gh3-nx/pgo/"' in PGO


def test_dump_periodico_para_sobreviver_ao_home_menu():
    # O HOME mata o NRO; esperar o exit perde a sessao.
    assert "__gcov_dump" in PGO
    assert "__gcov_reset" in PGO
    assert "svcSleepThread" in PGO
