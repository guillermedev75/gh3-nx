import re
from tools.split_register import split_register

def fake(n):
    body = "\n".join(f"  registrar->SetFunction(0x{0x82000000 + 4*i:08X}, sub_{0x82000000 + 4*i:08X});" for i in range(n))
    return ('// header\n#include "gh3_init.h"\n\n\nvoid gh3_RegisterFunctions(rex::runtime::IModuleRegistrar* registrar) {\n\n'
            + body + "\n}\n\n\n")

def calls(text):
    return re.findall(r"registrar->SetFunction\((0x[0-9A-F]+), (sub_[0-9A-F]+)\);", text)

def test_split_keeps_every_registration_in_order():
    src = fake(5000)
    out = split_register(src, chunk=2000)
    assert calls(out) == calls(src)
    assert out.count("static void gh3_RegisterFunctions_part") == 3
    assert "void gh3_RegisterFunctions(rex::runtime::IModuleRegistrar* registrar) {" in out

def test_each_part_is_called_once_in_order():
    out = split_register(fake(5000), chunk=2000)
    main = out[out.index("void gh3_RegisterFunctions(rex::runtime::IModuleRegistrar* registrar) {"):]
    assert re.findall(r"gh3_RegisterFunctions_part(\d+)\(registrar\);", main) == ["0", "1", "2"]

def test_idempotent():
    once = split_register(fake(5000), chunk=2000)
    assert split_register(once, chunk=2000) == once

def test_small_file_untouched():
    src = fake(10)
    assert split_register(src, chunk=2000) == src
