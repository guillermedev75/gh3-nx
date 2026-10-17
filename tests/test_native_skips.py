import subprocess, sys

PREFIX = "[2026-10-17 06:21:35.197] [info] [core] [t635368] [native] skipped by reason:"

def log(tmp_path, *lines):
    p = tmp_path / "boot.log"
    p.write_text("".join(l + "\n" for l in ("[info] boot",) + lines))
    return p

def run(path, *args):
    return subprocess.run([sys.executable, "tools/native_skips.py", str(path), *args],
                          capture_output=True, text=True)

def test_reason_over_max_fails(tmp_path):
    p = log(tmp_path, PREFIX + " 5:61137 7:7163")
    r = run(p, "--reason", "5", "--max", "0")
    assert r.returncode == 1
    assert "61137" in r.stdout + r.stderr

def test_reason_absent_passes(tmp_path):
    p = log(tmp_path, PREFIX + " 7:7163", PREFIX + " 7:12")
    assert run(p, "--reason", "5", "--max", "0").returncode == 0

def test_sums_every_interval(tmp_path):
    # cada linha é o delta de 10 s; o total da corrida é a soma
    p = log(tmp_path, PREFIX + " 5:3 7:1", PREFIX + "", PREFIX + " 5:4")
    assert run(p, "--reason", "5", "--max", "7").returncode == 0
    assert run(p, "--reason", "5", "--max", "6").returncode == 1

def test_other_reason_does_not_count(tmp_path):
    p = log(tmp_path, PREFIX + " 7:100 15:2")
    assert run(p, "--reason", "7", "--max", "99").returncode == 1
    assert run(p, "--reason", "15", "--max", "2").returncode == 0

def test_no_skip_line_fails(tmp_path):
    # sem a linha não dá para afirmar nada (renderer nativo não rodou ou a corrida foi curta)
    assert run(log(tmp_path), "--reason", "5", "--max", "0").returncode == 1

def test_missing_file(tmp_path):
    assert run(tmp_path / "nao-existe.log", "--reason", "5", "--max", "0").returncode == 1
