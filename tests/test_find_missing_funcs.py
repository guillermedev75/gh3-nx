import struct

from tools.find_missing_funcs import IMAGE_BASE, find_candidates, function_sizes

CPP = """DEFINE_REX_FUNC(sub_82000010) {
\tREX_FUNC_PROLOGUE();
\t// addi r3,r3,2288
\tctx.r3.s64 = ctx.r3.s64 + 2288;
\t// b 0x823b4bd8
\tsub_823B4BD8(ctx, base);
\treturn;
}

PPC_FUNC_IMPL(__imp__sub_82000030) {
\t// blr
\treturn;
}
"""


def image_with(words: dict, size=0x80) -> bytes:
    buf = bytearray(size)
    for addr, value in words.items():
        struct.pack_into(">I", buf, addr - IMAGE_BASE, value)
    return bytes(buf)


def test_function_sizes_counts_commented_instructions():
    assert function_sizes([CPP]) == {0x82000010: 8, 0x82000030: 4}


def test_gap_with_code_is_a_candidate_and_padding_is_not():
    sizes = {0x82000010: 8, 0x82000030: 4}
    # 0x82000018..0x8200002F: nop, depois código real (li r3,0 / blr) em 0x82000020
    img = image_with({0x82000018: 0x60000000, 0x8200001C: 0, 0x82000020: 0x38600000, 0x82000024: 0x4E800020,
                      0x82000060: 0x82000020})  # ponteiro para a função escondida
    assert find_candidates(img, sizes, 0x82000034) == [(0x82000020, 16, True)]


def test_only_padding_between_functions_gives_nothing():
    sizes = {0x82000010: 8, 0x82000030: 4}
    img = image_with({0x82000018: 0x60000000, 0x8200001C: 0x60000000})
    assert find_candidates(img, sizes, 0x82000034) == []


def test_unreferenced_gap_is_reported_but_flagged():
    sizes = {0x82000010: 8, 0x82000030: 4}
    img = image_with({0x82000018: 0x38600001, 0x8200001C: 0x4E800020})
    assert find_candidates(img, sizes, 0x82000034) == [(0x82000018, 24, False)]


def test_split_candidates_flags_entries_inside_unresolved_branch():
    from tools.find_missing_funcs import split_candidates
    log = "Unresolved conditional branch to 0x820E23C8 from 0x820E2564\nUnresolved conditional branch to 0x82250FAC from 0x82250FC4\n"
    cands = [0x820E2540, 0x820E2600, 0x82250FB0, 0x82300000]
    assert split_candidates(log, cands) == {0x820E2540, 0x82250FB0}
