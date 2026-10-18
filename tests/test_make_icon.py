import struct
import subprocess
import sys
import zlib

import pytest

from tools.make_icon import CROP, crop_command


def png(path, w=1280, h=720):
    rows = b"".join(b"\0" + bytes([x % 256, y % 256, 120]) * w for y in range(h) for x in [0])
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    path.write_bytes(data)


def test_crop_stays_inside_a_720p_capture():
    assert CROP["x"] + CROP["size"] <= 1280 and CROP["y"] + CROP["size"] <= 720


def test_crop_command_targets_256_jpeg():
    cmds = crop_command("a.png", "b.jpg")
    assert cmds[0][:4] == ["sips", "-c", str(CROP["size"]), str(CROP["size"])]
    assert "256" in cmds[1] and "jpeg" in cmds[1] and cmds[1][-1] == "b.jpg"


@pytest.mark.skipif(sys.platform != "darwin", reason="usa o sips do macOS")
def test_makes_a_256_jpeg_from_a_capture(tmp_path):
    src = tmp_path / "menu.png"
    png(src)
    out = tmp_path / "icon.jpg"
    subprocess.run([sys.executable, "tools/make_icon.py", "--from", str(src), "--out", str(out)], check=True)
    data = out.read_bytes()
    assert data[:2] == b"\xff\xd8"  # JPEG
    info = subprocess.run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", str(out)], capture_output=True, text=True).stdout
    assert "pixelWidth: 256" in info and "pixelHeight: 256" in info
