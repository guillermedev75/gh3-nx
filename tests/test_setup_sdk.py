import subprocess


def test_check_reports_unpatched_tree(tmp_path):
    # árvore git vazia: --check precisa falhar
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    r = subprocess.run(["tools/setup_sdk.sh", "--check", "--sdk", str(tmp_path)], capture_output=True)
    assert r.returncode != 0


def test_series_is_read_in_order():
    out = subprocess.run(["tools/setup_sdk.sh", "--list"], capture_output=True, text=True, check=True).stdout.split()
    assert out[0] == "0001-thp8-runtime-stack.patch" and out[-1] == "0016-switch-runtime-tuning.patch" and len(out) == 16
