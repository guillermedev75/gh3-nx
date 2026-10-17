"""Confere se um default.xex é uma revisão do Guitar Hero III suportada por este port."""
import struct
import sys

# Revisões conhecidas do GH3 de Xbox 360 (title id 0x415607F7). Cada uma tem endereços próprios no codegen.
REVISIONS = {
    "USA/Europa (disco multi 5 idiomas)": {"title_id": 0x415607F7, "media_id": 0x2C5FAD73, "image_base": 0x82000000, "entry_point": 0x82136950},
    "USA Rev 1": {"title_id": 0x415607F7, "media_id": 0x43048167, "image_base": 0x82000000, "entry_point": 0x82136AC8},
}


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


def identify(info: dict):
    """Nome da revisão conhecida que bate com o cabeçalho, ou None."""
    return next((name for name, exp in REVISIONS.items() if all(info.get(k) == v for k, v in exp.items())), None)


def check_bytes(data: bytes) -> list:
    """Lista de problemas; vazia quando o XEX é uma revisão suportada."""
    try:
        info = parse_xex(data)
    except ValueError as e:
        return [str(e)]
    if identify(info):
        return []
    if info.get("title_id") != 0x415607F7:
        return [f"title_id: 0x{info.get('title_id', 0):08X} não é o Guitar Hero III (0x415607F7)"]
    return [f"revisão desconhecida do GH3: media_id 0x{info.get('media_id', 0):08X}, entry_point 0x{info.get('entry_point', 0):08X}"]


def check(path) -> list:
    with open(path, "rb") as f:
        return check_bytes(f.read(0x10000))


if __name__ == "__main__":
    problems = check(sys.argv[1])
    if not problems:
        with open(sys.argv[1], "rb") as f:
            print("OK: Guitar Hero III —", identify(parse_xex(f.read(0x10000))))
    else:
        print("\n".join(problems))
    sys.exit(1 if problems else 0)
