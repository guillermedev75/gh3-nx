import pathlib, subprocess

def test_rexglue_cli_runs():
    exe = next(p for p in pathlib.Path("tools/rexglue").rglob("rexglue") if p.is_file())
    out = subprocess.run([str(exe), "--version"], capture_output=True, text=True, timeout=60)
    assert "0.10.0" in out.stdout + out.stderr
