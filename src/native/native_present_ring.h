// Política pura do anel de saída (present) do F3.2 — compila sem SDK e sem GPU.
//
// O present do gameplay é feito pelo presenter do SDK: `PaintContext::kSubmissionCount == 3`
// submissões de paint com uma imagem de guest output por vez no mailbox de 3, uma por quadro. A
// cada quadro o presenter grava no slot `submission % 3` e só espera o fence da submissão que vai
// ser reutilizada (`submission - 3`), nunca todas. Esse anel é o de *output*; o anel de *work* do
// renderer (`kSubmissionSlots`) é outro e continua com 4 slots — o plano proíbe um quarto work
// slot.
#pragma once

#include <cstdint>
#include <string>

namespace gh3::native::present_ring {

// Um submit por quadro e três slots: nunca mais que 3, nunca o quarto slot.
inline constexpr uint32_t kOutputSlots = 3;

struct Plan {
  // Slot que este quadro grava.
  uint32_t slot = 0;
  // Esperar o fence da submissão `await` antes de gravar neste slot.
  bool wait = false;
  // Submissão cujo fence precisa ter sinalizado (a do mesmo slot, 3 quadros antes).
  uint64_t await = 0;
};

// Mesma conta do presenter: `current >= count` espera `current - count`; o slot é `current % count`.
constexpr Plan PlanSubmission(uint64_t submission, uint32_t slots = kOutputSlots) {
  Plan plan;
  if (!slots) {
    return plan;
  }
  plan.slot = uint32_t(submission % slots);
  plan.wait = submission >= slots;
  plan.await = plan.wait ? submission - slots : 0;
  return plan;
}

// Linha única na inicialização: deixa explícito qual anel é o de saída e qual é o de trabalho.
inline std::string Format(uint32_t output_slots, uint32_t mailbox_images, uint32_t work_slots,
                          bool immediate_allowed) {
  return "present ring: " + std::to_string(output_slots) + " output slots (" +
         std::to_string(mailbox_images) + " mailbox images, 1 submit/frame, " +
         (immediate_allowed ? "immediate allowed" : "immediate off") + "), " +
         std::to_string(work_slots) + " work slots";
}

}  // namespace gh3::native::present_ring
