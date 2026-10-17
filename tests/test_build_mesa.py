import subprocess


def test_patches_are_listed_in_order():
    out = subprocess.run(["tools/build_mesa.sh", "--list"], capture_output=True, text=True, check=True).stdout.split()
    assert out[0] == "0001-nvk-switch-resolve-set-ubo-descriptors.patch"
    assert out[-1] == "0004-switch-build-script.patch" and len(out) == 4


def test_check_rejects_missing_driver_sdk(tmp_path):
    r = subprocess.run(["tools/build_mesa.sh", "--check", "--out", str(tmp_path)], capture_output=True)
    assert r.returncode != 0


def test_check_rejects_archive_without_vulkan(tmp_path):
    (tmp_path / "lib").mkdir()
    (tmp_path / "lib" / "libvulkan.a").write_bytes(b"!<arch>\n")
    (tmp_path / "lib" / "libnak_rs.a").write_bytes(b"!<arch>\n")
    r = subprocess.run(["tools/build_mesa.sh", "--check", "--out", str(tmp_path)], capture_output=True)
    assert r.returncode != 0
