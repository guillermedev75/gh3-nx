"""Confere se um default.xex é o Guitar Hero III USA Rev 1 suportado por este port."""
import struct
import sys

EXPECTED = {"title_id": 0x415607F7, "media_id": 0x43048167, "image_base": 0x82000000, "entry_point": 0x82136AC8}


def parse_xex(data: bytes) -> dict:
    """Lê do cabeçalho XEX2 (big-endian) o entry point, a base da imagem, o title id e o media id."""
    if data[:4] != b"XEX2":
        raise ValueError("não é um XEX2")
    (count,) = struct.unpack_from(">I", data, 0x14)
    info = {}
    for i in range(count):
        key, value = struct.unpack_from(">II", data, 0x18 + 8 * i)
        if key == 0x00010100:
            info["entry_point"] = value
        elif key == 0x00010201:
            info["image_base"] = value
        elif key == 0x00040006:  # execution info: media_id, version, base_version, title_id, ...
            media, _, _, title = struct.unpack_from(">IIII", data, value)
            info["media_id"], info["title_id"] = media, title
    return info


def check_bytes(data: bytes) -> list:
    try:
        info = parse_xex(data)
    except ValueError as e:
        return [str(e)]
    return [f"{k}: esperado 0x{v:08X}, encontrado 0x{info.get(k, 0):08X}"
            for k, v in EXPECTED.items() if info.get(k) != v]


def check(path) -> list:
    with open(path, "rb") as f:
        return check_bytes(f.read(0x10000))


if __name__ == "__main__":
    problems = check(sys.argv[1])
    print("OK: Guitar Hero III USA Rev 1" if not problems else "\n".join(problems))
    sys.exit(1 if problems else 0)
