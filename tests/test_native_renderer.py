import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]


def test_native_system_logs_one_present_per_swap():
    # tools/boot_test.py conta "XELOG_GPU PRESENT"; o nativo precisa escrever essa forma a cada swap.
    src = (ROOT / "src/native/gh3_native_system.cpp").read_text()
    assert 'REXGPU_DEBUG("XELOG_GPU PRESENT native")' in src


def test_app_installs_native_graphics_when_cvar_asks():
    app = (ROOT / "src/gh3_app.h").read_text()
    assert re.search(r"if \(gh3::native::Enabled\(\)\)\s*\{\s*config\.graphics = gh3::native::CreateGraphicsSystem\(\);", app)
    assert 'REXCVAR_DEFINE_STRING(gh3_renderer, "xenos"' in (ROOT / "src/native/gh3_native_system.cpp").read_text()


def test_native_sources_are_built():
    cmake = (ROOT / "CMakeLists.txt").read_text()
    for f in ("gh3_native_system.cpp", "native_renderer.cpp", "native_textures.cpp", "native_geometry_shaders.cpp"):
        assert f"src/native/{f}" in cmake


def test_no_thps_names_left_in_native_sources():
    for f in (ROOT / "src/native").glob("*.[ch]*"):
        assert not re.search(r"\bthps_renderer\b|namespace thps\b", f.read_text()), f
