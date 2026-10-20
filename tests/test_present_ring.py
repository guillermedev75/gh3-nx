"""F3.2 do plano 60 FPS: anel de saída (present) com 3 slots.

O present do gameplay passa pelo presenter do SDK (`VulkanPresenter`), que já mantém **três**
imagens de guest output (mailbox) e **três** submissões de paint, uma por quadro, esperando só o
slot que vai ser reutilizado. O F3.2 depende do F3.1 (T39): no Mac a espera medida foi 0-2
esperas por janela de 10 s (~500 quadros), então nada de quarto slot — o anel de *work* do
renderer continua com 4 slots (não confundir, playbook §8).

Aqui a política do anel é pura (índice do slot e quando esperar) e compila sem SDK nem GPU; os
testes de fiação prendem o renderer ao `kSubmissionCount`/mailbox do SDK (se o SDK mudar o anel,
o build/teste acusa) e exigem o log `present ring: 3 output slots`.
"""
import pathlib
import shutil
import subprocess

RAIZ = pathlib.Path(__file__).resolve().parent.parent
PRESENTER_SDK = RAIZ / "sdk" / "include" / "rex" / "ui" / "vulkan" / "presenter.h"
RENDERER = RAIZ / "src" / "native" / "native_renderer.cpp"

MAIN = r"""
#include "native_present_ring.h"
#include <iostream>
using namespace gh3::native::present_ring;
int main(int argc, char** argv) {
  uint64_t submission = std::strtoull(argv[1], nullptr, 10);
  uint32_t slots = uint32_t(std::strtoul(argv[2], nullptr, 10));
  Plan p = PlanSubmission(submission, slots);
  std::cout << p.slot << " " << (p.wait ? 1 : 0) << " " << p.await << "\n";
  std::cout << Format(kOutputSlots, 3, 4, true) << "\n";
}
"""


def build(tmp_path):
    inc = tmp_path / "inc"
    inc.mkdir(exist_ok=True)
    shutil.copy(RAIZ / "src" / "native" / "native_present_ring.h", inc)
    (tmp_path / "main.cpp").write_text(MAIN)
    exe = tmp_path / "present_ring"
    subprocess.run(["c++", "-std=c++20", f"-I{inc}", str(tmp_path / "main.cpp"), "-o", str(exe)],
                   check=True)
    return exe


def run(exe, submission, slots):
    result = subprocess.run([str(exe), str(submission), str(slots)], capture_output=True,
                            text=True, check=True)
    plan, line = result.stdout.strip().splitlines()
    return [int(v) for v in plan.split()], line


def test_ring_has_three_output_slots(tmp_path):
    (slot, wait, _), _ = run(build(tmp_path), 0, 3)
    assert slot == 0 and wait == 0
    assert " 3 output slots" in run(build(tmp_path), 0, 3)[1]


def test_first_lap_uses_every_slot_without_waiting(tmp_path):
    exe = build(tmp_path)
    assert run(exe, 0, 3)[0] == [0, 0, 0]
    assert run(exe, 1, 3)[0] == [1, 0, 0]
    assert run(exe, 2, 3)[0] == [2, 0, 0]


def test_reusing_a_slot_waits_only_for_that_slot(tmp_path):
    exe = build(tmp_path)
    # Quadro 3 reusa o slot 0: espera a submissão 0, não as mais novas.
    assert run(exe, 3, 3)[0] == [0, 1, 0]
    assert run(exe, 4, 3)[0] == [1, 1, 1]
    assert run(exe, 10, 3)[0] == [1, 1, 7]


def test_one_slot_is_the_old_behaviour(tmp_path):
    exe = build(tmp_path)
    assert run(exe, 0, 1)[0] == [0, 0, 0]
    assert run(exe, 1, 1)[0] == [0, 1, 0]
    assert run(exe, 7, 1)[0] == [0, 1, 6]


def test_log_line_names_slots_mailbox_submits_and_work_slots(tmp_path):
    _, line = run(build(tmp_path), 0, 3)
    assert line == ("present ring: 3 output slots (3 mailbox images, 1 submit/frame, "
                    "immediate allowed), 4 work slots")


def test_sdk_presenter_keeps_a_three_slot_paint_ring(tmp_path):
    texto = PRESENTER_SDK.read_text(encoding="utf-8")
    assert "static constexpr uint32_t kSubmissionCount = 3;" in texto
    assert "static constexpr uint32_t kGuestOutputMailboxSize = 3;" in RAIZ.joinpath(
        "sdk", "include", "rex", "ui", "presenter.h").read_text(encoding="utf-8")
    # Uma submissão de paint por quadro, esperando o slot reutilizado (não todas).
    fonte = (RAIZ / "sdk" / "src" / "ui" / "vulkan" / "vulkan_presenter.cpp").read_text(
        encoding="utf-8")
    assert "current_paint_submission_index -" in fonte
    # A swapchain pede pelo menos tantas imagens quanto slots; IMMEDIATE vem antes de MAILBOX.
    assert "std::max(kSubmissionCount, surface_capabilities.minImageCount)" in fonte
    assert fonte.index("VK_PRESENT_MODE_IMMEDIATE_KHR") < fonte.index("VK_PRESENT_MODE_MAILBOX_KHR")


def test_renderer_logs_the_output_ring_separate_from_the_work_ring():
    src = RENDERER.read_text(encoding="utf-8")
    assert '#include "native_present_ring.h"' in src
    # O anel de saída é o do presenter; o kSubmissionSlots do renderer é o de work slots.
    assert "static_assert(rex::ui::vulkan::VulkanPresenter::kMaxActiveGuestOutputImageVersions ==" in src
    assert "present_ring::kOutputSlots" in src
    assert 'REXLOG_INFO("[native] {}",' in src
    assert "present_ring::Format(present_ring::kOutputSlots," in src
    assert 'REXLOG_INFO("[native] work ring: {} slots", kSubmissionSlots);' in src
    # Nunca um quarto work slot: o anel de trabalho continua com os 4 slots de antes.
    assert "constexpr uint32_t kSubmissionSlots = 4;" in src


def test_switch_asks_for_immediate_present_mode():
    toml = (RAIZ / "switch" / "gh3.toml").read_text(encoding="utf-8")
    assert "vulkan_allow_present_mode_immediate = true" in toml
