import struct, subprocess, sys

def nro(tmp_path, magic=b"NRO0", size=0x1000, tail=b""):
    p = tmp_path / "x.nro"
    head = bytearray(0x80); head[0x10:0x14] = magic; struct.pack_into("<I", head, 0x18, size)
    p.write_bytes(bytes(head) + b"\0" * (size - 0x80) + tail); return p

def run(path):
    return subprocess.run([sys.executable, "tools/check_nro.py", str(path)], capture_output=True, text=True)

def test_valid_nro(tmp_path):
    assert subprocess.run([sys.executable, "tools/check_nro.py", str(nro(tmp_path))]).returncode == 0

def test_bad_magic(tmp_path):
    assert subprocess.run([sys.executable, "tools/check_nro.py", str(nro(tmp_path, b"XXXX"))]).returncode == 1

def test_truncated_nro(tmp_path):
    p = nro(tmp_path)
    p.write_bytes(p.read_bytes()[:0x800])
    assert run(p).returncode == 1

def test_asset_section_after_size(tmp_path):
    aset = b"ASET" + b"\0" * 0x3c
    assert run(nro(tmp_path, tail=aset)).returncode == 0

def test_garbage_after_size(tmp_path):
    # o que vem depois do NRO só pode ser a asset section (ícone, NACP, RomFS)
    assert run(nro(tmp_path, tail=b"lixo" * 16)).returncode == 1

def test_missing_file(tmp_path):
    assert run(tmp_path / "nao-existe.nro").returncode == 1
