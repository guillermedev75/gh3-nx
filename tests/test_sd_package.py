import hashlib, struct, subprocess, sys

PKG = "switch/gh3-nx"

def fake_nro(tmp_path, size=0x1000):
    p = tmp_path / "gh3.nro"
    head = bytearray(0x80); head[0x10:0x14] = b"NRO0"; struct.pack_into("<I", head, 0x18, size)
    p.write_bytes(bytes(head) + b"\0" * (size - 0x80)); return p

def fake_xex(media_id=0x2C5FAD73, title_id=0x415607F7, entry=0x82136950, pad=b"codigo"):
    # cabeçalho XEX2 mínimo que o tools/check_xex.py entende (3 chaves), sem nada do jogo
    head = bytearray(0x100); head[:4] = b"XEX2"; struct.pack_into(">I", head, 0x14, 3)
    for i, (k, v) in enumerate([(0x00010100, entry), (0x00010201, 0x82000000), (0x00040006, 0x80)]):
        struct.pack_into(">II", head, 0x18 + 8 * i, k, v)
    struct.pack_into(">IIII", head, 0x80, media_id, 0, 0, title_id)
    return bytes(head) + pad

def package(tmp_path, nro):
    out = tmp_path / "sd"
    r = subprocess.run(["tools/make_sd_package.sh", "--nro", str(nro), "--out", str(out)],
                       capture_output=True, text=True)
    return r, out

def tree(root):
    return sorted(str(p.relative_to(root)) for p in root.rglob("*") if p.is_file())

def check(path):
    return subprocess.run(["tools/check_no_game_content.sh", str(path)], capture_output=True, text=True)

def test_package_tree_is_exact(tmp_path):
    nro = fake_nro(tmp_path)
    r, out = package(tmp_path, nro)
    assert r.returncode == 0, r.stdout + r.stderr
    assert tree(out) == [f"{PKG}/LICENSES.txt", f"{PKG}/gh3-nx.nro", f"{PKG}/gh3.toml"]
    assert (out / PKG / "gh3-nx.nro").read_bytes() == nro.read_bytes()
    assert not any(f.endswith((".xex", ".iso")) or "DATA" in f for f in tree(out))

def test_package_carries_both_licences(tmp_path):
    r, out = package(tmp_path, fake_nro(tmp_path))
    text = (out / PKG / "LICENSES.txt").read_text()
    assert "GNU GENERAL PUBLIC LICENSE" in text and "project8-sw" in text

def test_package_settings_pick_native_renderer(tmp_path):
    r, out = package(tmp_path, fake_nro(tmp_path))
    assert 'gh3_renderer = "native"' in (out / PKG / "gh3.toml").read_text()

def test_package_refuses_invalid_nro(tmp_path):
    bad = tmp_path / "gh3.nro"; bad.write_bytes(b"\0" * 0x100)
    r, out = package(tmp_path, bad)
    assert r.returncode != 0 and not (out / PKG / "gh3-nx.nro").exists()

def test_package_replaces_previous_contents(tmp_path):
    stale = tmp_path / "sd" / PKG / "default.xex"; stale.parent.mkdir(parents=True); stale.write_bytes(b"x")
    r, out = package(tmp_path, fake_nro(tmp_path))
    assert r.returncode == 0 and not stale.exists()

def test_check_accepts_clean_dir(tmp_path):
    (tmp_path / "gh3.toml").write_text("log_level = \"info\"\n")
    assert check(tmp_path).returncode == 0

def test_check_refuses_game_files_by_name(tmp_path):
    for name in ["default.xex", "game.iso", "DATA/x.pak.xen", "music.fsb.xen"]:
        d = tmp_path / name.replace("/", "_") ; d.mkdir()
        (d / name).parent.mkdir(parents=True, exist_ok=True); (d / name).write_bytes(b"x")
        assert check(d).returncode == 1, name

def test_check_refuses_renamed_xex(tmp_path):
    # pelo conteúdo, não só pelo nome: um XEX2 renomeado também é o executável do jogo
    (tmp_path / "inocente.bin").write_bytes(fake_xex())
    assert check(tmp_path).returncode == 1

def test_check_repo_tree_is_clean():
    r = subprocess.run(["tools/check_no_game_content.sh"], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout

def gen(*args):
    return subprocess.run([sys.executable, "tools/gen_supported_dumps.py", *map(str, args)],
                          capture_output=True, text=True)

def test_gen_writes_hash_and_size_only(tmp_path):
    xex = tmp_path / "default.xex"; xex.write_bytes(fake_xex(pad=b"SEGREDO-DO-JOGO" * 4))
    out = tmp_path / "supported_dumps.h"
    assert gen("--xex", xex, "--out", out).returncode == 0
    h = out.read_text()
    assert hashlib.sha256(xex.read_bytes()).hexdigest() in h
    assert str(xex.stat().st_size) in h and "0x415607F7" in h
    assert "SEGREDO" not in h

def test_gen_refuses_unknown_revision(tmp_path):
    xex = tmp_path / "default.xex"; xex.write_bytes(fake_xex(media_id=0x12345678))
    r = gen("--xex", xex, "--out", tmp_path / "h.h")
    assert r.returncode == 1 and not (tmp_path / "h.h").exists()

def test_gen_check_detects_stale_header(tmp_path):
    xex = tmp_path / "default.xex"; xex.write_bytes(fake_xex())
    out = tmp_path / "supported_dumps.h"
    gen("--xex", xex, "--out", out)
    assert gen("--xex", xex, "--out", out, "--check").returncode == 0
    xex.write_bytes(fake_xex(pad=b"outro build"))
    assert gen("--xex", xex, "--out", out, "--check").returncode == 1
