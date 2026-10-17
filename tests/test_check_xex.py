import struct
from tools.check_xex import parse_xex, check_bytes

def fake_xex(entry=0x82136AC8, base=0x82000000, title=0x415607F7, media=0x43048167):
    headers = [(0x00010100, entry), (0x00010201, base), (0x00040006, 0x100)]
    b = bytearray(0x200)
    b[0:4] = b"XEX2"
    struct.pack_into(">I", b, 0x14, len(headers))
    for i, (k, v) in enumerate(headers):
        struct.pack_into(">II", b, 0x18 + 8 * i, k, v)
    struct.pack_into(">IIIIII", b, 0x100, media, 0, 0, title, 0, 0)
    return bytes(b)

def test_parse_fields():
    info = parse_xex(fake_xex())
    assert info == {"entry_point": 0x82136AC8, "image_base": 0x82000000, "title_id": 0x415607F7, "media_id": 0x43048167}

def test_gh3_rev1_accepted():
    assert check_bytes(fake_xex()) == []

def test_other_game_refused():
    problems = check_bytes(fake_xex(title=0x12345678))
    assert any("title_id" in p for p in problems)

def test_not_xex_refused():
    assert check_bytes(b"\x00" * 64) == ["não é um XEX2"]
