"""SPIRV-Tools/spirv-opt construído a partir do SDK patcheado (F2.1).

O SDK fixado só expõe os headers do spirv-tools (spirv-tools-headers): o
``spvtools::Optimizer`` mora em SPIRV-Tools-opt, que precisa de SPIRV-Tools e
SPIRV-Tools-link. A série local de patches (patches/rexglue-local) liga o subprojeto no
``sdk/thirdparty/CMakeLists.txt``; o build do SDK instala as libs em
``out/sdk-install``.
"""

import subprocess
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
SERIE = RAIZ / "patches" / "rexglue-local" / "series"
SDK_TERCEIROS = RAIZ / "sdk" / "thirdparty" / "CMakeLists.txt"
LIB_OPT = RAIZ / "out" / "sdk-install" / "lib" / "libSPIRV-Tools-opt.a"


def test_serie_local_liga_o_spirv_tools_no_sdk():
    nomes = SERIE.read_text(encoding="utf-8").split()
    assert nomes, "patches/rexglue-local/series está vazia"
    texto = "\n".join((SERIE.parent / nome).read_text(encoding="utf-8") for nome in nomes)
    assert "add_subdirectory(spirv-tools" in texto
    # Biblioteca e não executáveis/testes: o devkitA64 e o Mac só precisam das libs.
    assert "SPIRV_SKIP_EXECUTABLES ON" in texto
    # O spirv-tools procura external/spirv-headers; o subprojeto do SDK está ao lado.
    assert 'SPIRV-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spirv-headers"' in texto


def test_sdk_check_inclui_a_serie_local_e_a_arvore_esta_aplicada():
    r = subprocess.run([str(RAIZ / "tools" / "setup_sdk.sh"), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "add_subdirectory(spirv-tools" in SDK_TERCEIROS.read_text(encoding="utf-8")


def test_libspirv_tools_opt_instalada_com_o_optimizer():
    assert LIB_OPT.is_file(), f"falta {LIB_OPT} (tools/build_spirv_tools.sh)"
    nm = subprocess.run(["nm", str(LIB_OPT)], capture_output=True, text=True)
    assert nm.returncode == 0, nm.stderr
    # nm sem -C: símbolo mangled de spvtools::Optimizer.
    assert "8spvtools9Optimizer" in nm.stdout


def test_script_de_build_ve_o_estado_pronto():
    r = subprocess.run([str(RAIZ / "tools" / "build_spirv_tools.sh"), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
