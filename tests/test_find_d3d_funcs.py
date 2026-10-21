import json
import subprocess
import sys

SCRIPT = "tools/find_d3d_funcs.py"


def gen(tmp_path):
    g = tmp_path / "gen"
    g.mkdir()
    (g / "gh3_recomp.0.cpp").write_text(
        "DEFINE_REX_FUNC(sub_824B2A70) {\n  __imp__VdSwap(ctx, base);\n}\n"
        "DEFINE_REX_FUNC(sub_824D0E20) {\n  __imp__VdInitializeRingBuffer(ctx, base);\n}\n"
        "DEFINE_REX_FUNC(sub_82100000) {\n  sub_824B2A70(ctx, base);\n}\n")
    (g / "gh3_recomp.1.cpp").write_text(
        "DEFINE_REX_FUNC(sub_824D91F0) {\n  __imp__VdSetDisplayMode(ctx, base);\n}\n")
    names = tmp_path / "names.toml"
    names.write_text('[functions]\n0x8264DF28 = { name = "Direct3D_CreateDevice" }\n'
                     '0x82287590 = { name = "D3DDevice_Swap" }\n'
                     '0x826567C8 = { name = "D3DDevice_DrawIndexedVertices" }\n')
    return g, names


def run(tmp_path, g, names):
    out = tmp_path / "d.json"
    r = subprocess.run([sys.executable, SCRIPT, "--gen", str(g), "--names", str(names), "--out", str(out)],
                       capture_output=True, text=True)
    return r, out


def test_anchors_name_the_functions_that_call_d3d_only_imports(tmp_path):
    g, names = gen(tmp_path)
    r, out = run(tmp_path, g, names)
    assert r.returncode == 0, r.stderr
    d = json.load(open(out))
    assert d["found"] == {"D3DDevice_Swap": "0x824B2A70", "Direct3D_CreateDevice": "0x824D0E20"}
    assert d["missing"] == ["D3DDevice_DrawIndexedVertices"]
    assert d["d3d_range"] == ["0x824B2A70", "0x824D91F0"]
    assert d["anchors"]["VdSwap"] == ["sub_824B2A70"]  # quem só chama o Swap não é anchor


def test_ambiguous_anchor_is_not_a_finding(tmp_path):
    g, names = gen(tmp_path)
    (g / "gh3_recomp.2.cpp").write_text("DEFINE_REX_FUNC(sub_824C0000) {\n  __imp__VdSwap(ctx, base);\n}\n")
    r, out = run(tmp_path, g, names)
    d = json.load(open(out))
    assert "D3DDevice_Swap" not in d["found"] and len(d["anchors"]["VdSwap"]) == 2


def test_fails_without_generated_code(tmp_path):
    r, _ = run(tmp_path, tmp_path / "nada", tmp_path / "x.toml")
    assert r.returncode == 1
