"""Brechas do check_no_game_content.sh achadas na revisão do marco C (2026-10-17)."""
import os
import subprocess

SCRIPT = "tools/check_no_game_content.sh"


def check(path):
    return subprocess.run(["bash", SCRIPT, str(path)], capture_output=True, text=True)


def put(root, rel, data=b"x"):
    p = root / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return p


def test_systemupdate_folder_is_refused(tmp_path):
    put(tmp_path, "$SYSTEMUPDATE/su20076000_00000000")
    assert check(tmp_path).returncode == 1


def test_trailing_slash_still_checks_content(tmp_path):
    put(tmp_path, "renamed.bin", b"XEX2" + b"\0" * 16)
    assert check(f"{tmp_path}/").returncode == 1


def test_uppercase_names_are_refused(tmp_path):
    for name in ["DEFAULT.XEX", "game.ISO", "songs/x.PAK.XEN", "data/thing"]:
        d = tmp_path / name.replace("/", "_").replace(".", "_")
        d.mkdir()
        put(d, name)
        assert check(d).returncode == 1, name


def test_xbox_container_magics_are_refused(tmp_path):
    for magic in [b"CON ", b"LIVE", b"PIRS"]:
        d = tmp_path / magic.decode().strip()
        d.mkdir()
        put(d, "blob", magic + b"\0" * 16)
        assert check(d).returncode == 1, magic


def test_symlink_to_game_file_is_followed(tmp_path):
    target = put(tmp_path / "outside", "default.xex", b"XEX2")
    pkg = tmp_path / "pkg"
    pkg.mkdir()
    os.symlink(target, pkg / "harmless")
    assert check(pkg).returncode == 1


def test_clean_package_passes(tmp_path):
    put(tmp_path, "gh3-nx.nro", b"\0" * 64)
    put(tmp_path, "gh3.toml", b"a = 1\n")
    assert check(tmp_path).returncode == 0
