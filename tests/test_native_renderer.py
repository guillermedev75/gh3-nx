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


def test_renderer_expands_primitives_without_geometry_shaders():
    # MoltenVK não tem geometryShader: ret/quad/ponto passam por native_primitive_expand.h
    src = (ROOT / "src/native/native_renderer.cpp").read_text()
    assert '#include "native_primitive_expand.h"' in src
    assert "expand::Choose(expand_primitive, properties.geometryShader != VK_FALSE)" in src


def test_renderer_draws_msaa_surfaces_as_edram_samples():
    # T9: o GH3 limpa o alvo 1x por um alias 4x de meio pitch; draws, clears e resolves passam por
    # native_edram_samples.h para cair no mesmo alvo (o Switch tem gh3_native_msaa=false e o mesmo problema)
    src = (ROOT / "src/native/native_renderer.cpp").read_text()
    assert '#include "native_edram_samples.h"' in src
    assert 'REXCVAR_DEFINE_BOOL(gh3_native_msaa_as_samples, true' in src
    assert src.count("SurfaceFor(") >= 4  # draw, resolve, clear e as constantes de sistema


def test_renderer_aliases_one_sample_msaa_surfaces_onto_the_1x_target():
    # T15: com gh3_native_msaa_as_samples=false e 1 amostra, o palco 4x (pitch P) é desenhado em 1 amostra
    # e sobe (blit nearest) para o alvo 1x de pitch 2P quando este é usado (draw, resolve, clear)
    src = (ROOT / "src/native/native_renderer.cpp").read_text()
    assert "edram::AliasBlitExtent(" in src
    assert "SyncAliasTarget(" in src
    assert "alias_msaa" in src
    assert "uint64_t alias_seq" in src
    assert "alias_msaa = AliasMsaa(" in src


def test_switch_performance_mode_skips_shadow_and_bloom_passes():
    # Medido no Switch: limitado pela GPU; sem sombra e bloom, FPS 34 -> 45 e o branco do T9 some.
    src = (ROOT / "src/native/native_renderer.cpp").read_text()
    assert re.search(r"gh3_native_skip_shadows\).*base_tiles == 0 && c0->key\.pitch == 160", src)
    assert re.search(r"gh3_native_skip_bloom\).*base_tiles == 1440 && c0->key\.pitch == 320", src)
    # padrão desligado no código (Mac e testes de paridade), ligado só no pacote do Switch
    assert 'REXCVAR_DEFINE_BOOL(gh3_native_skip_shadows, false' in src
    assert 'REXCVAR_DEFINE_BOOL(gh3_native_skip_bloom, false' in src
    toml = (ROOT / "switch/gh3.toml").read_text()
    assert re.search(r"^gh3_native_skip_shadows = true$", toml, re.M)
    assert re.search(r"^gh3_native_skip_bloom = true$", toml, re.M)


def test_vblank_late_no_wait_is_off_everywhere():
    # Medido no Switch sem ganho: os quadros lentos não são quantização de vblank.
    assert "REXCVAR_DEFINE_BOOL(gh3_vblank_late_no_wait, true" not in (ROOT / "src/guest_waits.h").read_text()
    assert re.search(r"^gh3_vblank_late_no_wait = false$", (ROOT / "switch/gh3.toml").read_text(), re.M)
