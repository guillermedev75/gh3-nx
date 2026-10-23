// SPDX-License-Identifier: GPL-3.0-or-later
//
// Native renderer for the Switch port. See native_renderer.h for the design.
//
// Parts of this file are ports of ReXGlue's Vulkan backend (src/graphics/vulkan,
// itself from Xenia: Copyright Ben Vanik and the Xenia contributors, BSD-3-Clause,
// see LICENSES/): the system constants (VulkanCommandProcessor::
// UpdateSystemConstantValues), the dynamic state (UpdateDynamicState), the
// shader modifications and pipeline state (VulkanPipelineCache), and the resolve
// rectangle (draw_util::GetResolveInfo). They are what the translated shaders
// expect, so they are kept as close to the original as the native design allows;
// everything about EDRAM, fragment shader interlock and tessellation is gone.

#include "native_renderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include <fmt/format.h>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <xxhash.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/memory/utils.h>
#include <rex/string/buffer.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/vulkan/shader.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/system/xmemory.h>
#include <rex/system/xvideo.h>
#include <rex/ui/presenter.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/vulkan/provider.h>
#include <rex/ui/vulkan/instance.h>
#include <rex/ui/vulkan/util.h>

#if defined(__SWITCH__)
extern "C" {
#include <switch/kernel/svc.h>
}
#endif

#include "native_format_audit.h"
#include "native_format_log.h"
#include "native_geometry_shaders.h"
#include "native_swap_log.h"
#include "native_edram_samples.h"
#include "native_invisible.h"
#include "native_scene_scale.h"
#include "native_scene_dynamic.h"
#include "native_scene_cas.h"
#include "native_scene_upscale.h"
#include "native_spirv_opt.h"
#include "native_shader_simplify.h"
#include "native_pipeline_prewarm.h"
#include "native_present_ring.h"
#include "native_resolve_ownership.h"
#include "native_ring_caches.h"

// T32: o SPIR-V que o próprio SDK usa no presenter para CAS (FidelityFX), reaproveitado no blit que
// sobe a cena escalada. O VS desenha o retângulo em NDC e o PS lê 9 texels e aplica o CAS.
namespace shaders {
#include <guest_output_ffx_cas_resample_ps.h>
#include <guest_output_ffx_fsr_easu_ps.h>
#include <guest_output_triangle_strip_rect_vs.h>
}  // namespace shaders
#include "native_primitive_expand.h"
#include "native_hash.h"
#include "native_textures.h"

REXCVAR_DECLARE(bool, gpu_specialize_texture_signs);
REXCVAR_DECLARE(uint32_t, gh3_min_frame_vblanks);
REXCVAR_DEFINE_BOOL(gh3_native_depth24, false, "GH3",
                    "Store depth in 32 bits per pixel (24-bit depth with stencil, or float "
                    "without) instead of 32-bit float with stencil, which NVIDIA GPUs keep "
                    "in 64");
REXCVAR_DEFINE_BOOL(gh3_native_gpu_profile, false, "GH3",
                    "Log GPU time by render pass and resolve, measured on one frame in 16");
REXCVAR_DEFINE_BOOL(gh3_native_record_thread, false, "GH3",
                    "Make the Vulkan calls of draws on a second thread, so the command ring "
                    "thread prepares the next draw meanwhile");
REXCVAR_DEFINE_BOOL(gh3_native_msaa, false, "GH3",
                    "Render with the multisampling the game asks for; off (default) draws every "
                    "target with one sample, which costs the GPU much less memory bandwidth. With "
                    "gh3_native_msaa_as_samples=false and off, the stage's 4x surface is drawn with "
                    "one sample and blitted up to the 1x target that shares its EDRAM (T15); on "
                    "with samples-as-pixels off has no such alias and the stage comes out white");
REXCVAR_DEFINE_BOOL(gh3_native_msaa_as_samples, true, "GH3",
                    "Draw multisampled surfaces as their EDRAM samples, into the single-sampled "
                    "target that shares that EDRAM (4x at pitch P = 1x at pitch 2P), as the game "
                    "clears its targets; overrides gh3_native_msaa. Off: one sample per guest pixel "
                    "(gh3_native_msaa=false), a quarter of the pixels shaded");
REXCVAR_DEFINE_DOUBLE(gh3_native_scene_scale, 1.0, "GH3",
                      "Escala (0.25-1) da cena 3D em relação à HUD: o alvo 1280x720 usado com profundidade é "
                      "desenhado numa imagem de scale*1280 x scale*720 e sobe por blit linear quando a HUD "
                      "(sem profundidade) ou um resolve o usa. 1.0 = desligado (T16)");
REXCVAR_DEFINE_BOOL(gh3_native_scene_dynamic, false, "GH3",
                    "Controlador dinamico da escala da cena (T31): mede o intervalo entre swaps e sobe/desce "
                    "a escala do gh3_native_scene_scale em passos de 0.05 entre 0.65 e 1.0 para caber em "
                    "16.7 ms, com histerese de 3 quadros e 0.6 s no mesmo nivel. false = escala fixa");
REXCVAR_DEFINE_BOOL(gh3_native_scene_cas, false, "GH3",
                    "CAS (FidelityFX Contrast Adaptive Sharpening) no blit que sobe a cena escalada "
                    "para o alvo cheio, em vez do vkCmdBlitImage LINEAR (T32). Só vale com "
                    "gh3_native_scene_scale < 1 e alvo de cor R8G8B8A8_UNORM; o resto continua LINEAR");
REXCVAR_DEFINE_DOUBLE(gh3_native_scene_cas_sharpness, 0.2, "GH3",
                      "Sharpen do CAS no upscale da cena, 0-1 (0 desliga; o shader recebe o valor "
                      "pós-setup do FidelityFX)");
REXCVAR_DEFINE_STRING(gh3_native_scene_upscale, "linear", "GH3",
                      "Upscaler do blit que sobe a cena escalada para o alvo cheio: linear | cas | "
                      "fsr1 (etapa EASU do FSR1, upscale adaptativo de borda), todos só com "
                      "gh3_native_scene_scale < 1 e alvo de cor R8G8B8A8_UNORM (T47). O bool "
                      "gh3_native_scene_cas=true da T32 ainda seleciona o cas quando este é linear");
REXCVAR_DEFINE_BOOL(gh3_native_skip_shadows, false, "GH3",
                    "Experimento de FPS: pula os draws do mapa de sombra (alvo cor base 0, pitch 160); "
                    "a sombra fica com o conteudo do frame anterior");
REXCVAR_DEFINE_BOOL(gh3_native_skip_bloom, false, "GH3",
                    "Experimento de FPS: pula os draws do bloom (alvo cor base 1440, pitch 320); "
                    "o brilho fica com o conteudo do frame anterior");
REXCVAR_DEFINE_BOOL(gh3_native_zcull, false, "GH3",
                    "Clear the depth attachment on its first pass of the frame instead of loading it: "
                    "NVK only enables ZCULL (skip hidden pixels) when the depth loadOp is CLEAR");
REXCVAR_DEFINE_BOOL(gh3_native_alias_cap, true, "GH3",
                    "Cap a full-EDRAM 4x alias draw to half the EDRAM rows: the game clears its 1x "
                    "target through the alias, and the default 8192 scissor used to make the shared "
                    "1280 target 2048 rows tall");
REXCVAR_DEFINE_BOOL(gh3_native_async_pipelines, true, "GH3",
                    "Native renderer: compile new pipelines on a worker thread and skip the "
                    "draws that need them until they are ready, instead of stalling the frame");
REXCVAR_DEFINE_BOOL(gh3_native_swap_log, false, "GH3",
                    "Uma linha \"swap:\" por swap com frontbuffer, formato, clear, resolve, chave "
                    "do render target, alias_seq e espera de fence (F0.2 do plano de 60 FPS)");
REXCVAR_DEFINE_UINT32(gh3_native_rt_trace, 0, "GH3",
                      "DEPURACAO: loga passes, resolves e copias dos quadros [N, N+2) (0 = desligado)");
REXCVAR_DEFINE_BOOL(gh3_native_format_log, false, "GH3",
                    "Uma linha \"format:\" por alvo criado com o VkFormat, o formato do guest, "
                    "16F, gamma, clear inicial, scene scale e alias (F0.4 do plano de 60 FPS)");
REXCVAR_DEFINE_BOOL(gh3_native_spirv_opt, false, "GH3",
                    "Passa cada shader traduzido pelo spvtools::Optimizer (RegisterPerformancePasses) "
                    "antes de criar o VkShaderModule, validando a entrada e a saida; se qualquer "
                    "etapa falhar usa o original e conta fallback (F2.2 do plano de 60 FPS)");
REXCVAR_DEFINE_BOOL(gh3_native_shader_simplify, true, "GH3",
                    "Simplifica cada shader traduzido antes do driver: max(a,a)/min(a,a) vira copia, "
                    "query de tamanho de textura repetida e' reaproveitada e branch redundante e' "
                    "fundido; o modulo e' validado antes e depois e cai no original em falha "
                    "(F2.4 do plano de 60 FPS)");
REXCVAR_DEFINE_BOOL(gh3_native_pipeline_prewarm, true, "GH3",
                    "Guarda as chaves dos pipelines usados (com o microcodigo dos shaders) ao lado "
                    "do cache do driver e recria esses pipelines na inicializacao, antes do jogo: "
                    "o primeiro uso na gameplay nao espera a compilacao (F2.5 do plano de 60 FPS)");
REXCVAR_DEFINE_BOOL(gh3_native_resolve_swap, false, "GH3",
                    "Resolve de profundidade do alvo inteiro sem copiar (F3.3): o destino fica com a "
                    "imagem do alvo (amostrada como profundidade) e o alvo ganha uma imagem nova; o "
                    "conteudo volta por copia antes de o alvo ser escrito sem clear. A cor fica de "
                    "fora ate a paridade");

// A auditoria numera os VkFormat para compilar sem o SDK; se um valor mudar, o build acusa aqui.
static_assert(uint32_t(VK_FORMAT_R8G8B8A8_UNORM) == gh3::native::format_log::kR8G8B8A8Unorm);
static_assert(uint32_t(VK_FORMAT_A2B10G10R10_UNORM_PACK32) == gh3::native::format_log::kA2B10G10R10UnormPack32);
static_assert(uint32_t(VK_FORMAT_R16G16_SNORM) == gh3::native::format_log::kR16G16Snorm);
static_assert(uint32_t(VK_FORMAT_R16G16_SFLOAT) == gh3::native::format_log::kR16G16Sfloat);
static_assert(uint32_t(VK_FORMAT_R16G16B16A16_SNORM) == gh3::native::format_log::kR16G16B16A16Snorm);
static_assert(uint32_t(VK_FORMAT_R16G16B16A16_SFLOAT) == gh3::native::format_log::kR16G16B16A16Sfloat);
static_assert(uint32_t(VK_FORMAT_R32_SFLOAT) == gh3::native::format_log::kR32Sfloat);
static_assert(uint32_t(VK_FORMAT_R32G32_SFLOAT) == gh3::native::format_log::kR32G32Sfloat);
static_assert(uint32_t(VK_FORMAT_D32_SFLOAT) == gh3::native::format_log::kD32Sfloat);
static_assert(uint32_t(VK_FORMAT_D24_UNORM_S8_UINT) == gh3::native::format_log::kD24UnormS8Uint);
static_assert(uint32_t(VK_FORMAT_D32_SFLOAT_S8_UINT) == gh3::native::format_log::kD32SfloatS8Uint);
// F3.2: o anel de saída é o do presenter do SDK (3 imagens de guest output). Se o SDK mudar essa
// contagem, o log e a política pura (native_present_ring.h) ficam errados — o build avisa aqui.
static_assert(rex::ui::vulkan::VulkanPresenter::kMaxActiveGuestOutputImageVersions ==
              gh3::native::present_ring::kOutputSlots);

namespace gh3::native {
namespace shaders {
#include "shaders/vulkan_spirv/apply_gamma_table_cs.h"
#include "shaders/depth_resolve_cs.h"
#include "shaders/depth_to_float_cs.h"
#include "shaders/copy_color_cs.h"
}  // namespace shaders

namespace {

namespace xenos = rex::graphics::xenos;
namespace reg = rex::graphics::reg;
namespace draw_util = rex::graphics::draw_util;
using rex::graphics::RegisterFile;
using rex::graphics::Shader;
using rex::graphics::SpirvShader;
using rex::graphics::SpirvShaderTranslator;
using rex::graphics::vulkan::VulkanShader;
using rex::ui::vulkan::VulkanDevice;

// Four: with three, a single frame of heavy GPU work stalled the ring thread
// at once (30-49 ms waits for a free slot in frames that then dropped).
constexpr uint32_t kSubmissionSlots = 4;
// Vertex, index, constant and texture upload data of the submissions in flight.
constexpr VkDeviceSize kStreamSize = 160u << 20;
// Room at the end of the stream for the fixed ranges of constant bindings.
constexpr VkDeviceSize kStreamGuard = 16u << 10;
// Past the end of the stream, never allocated from but inside the range the
// shaders' vertex fetches may address: a fetch past its copied data (a guest
// index out of range) reads garbage there instead of faulting the GPU's MMU,
// which loses the device for good.
constexpr VkDeviceSize kStreamSlack = 16u << 20;
// Sizes of the constant buffer bindings. The float ones are bound with their
// maximum size and filled up to the shader's count.
constexpr VkDeviceSize kFloatConstantsBytes = 256 * 4 * sizeof(float);
constexpr VkDeviceSize kBoolLoopConstantsBytes = sizeof(uint32_t) * (8 + 32);
constexpr VkDeviceSize kFetchConstantsBytes = sizeof(uint32_t) * 6 * 32;

VkFormat ColorFormatToVk(xenos::ColorRenderTargetFormat format) {
  using F = xenos::ColorRenderTargetFormat;
  switch (format) {
    case F::k_8_8_8_8:
    case F::k_8_8_8_8_GAMMA:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case F::k_2_10_10_10:
    case F::k_2_10_10_10_AS_10_10_10_10:
      return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case F::k_2_10_10_10_FLOAT:
    case F::k_2_10_10_10_FLOAT_AS_16_16_16_16:
    case F::k_16_16_16_16_FLOAT:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case F::k_16_16:
      return VK_FORMAT_R16G16_SNORM;
    case F::k_16_16_16_16:
      return VK_FORMAT_R16G16B16A16_SNORM;
    case F::k_16_16_FLOAT:
      return VK_FORMAT_R16G16_SFLOAT;
    case F::k_32_FLOAT:
      return VK_FORMAT_R32_SFLOAT;
    case F::k_32_32_FLOAT:
      return VK_FORMAT_R32G32_SFLOAT;
    default:
      return VK_FORMAT_R8G8B8A8_UNORM;
  }
}

bool IsColorFormat64bpp(xenos::ColorRenderTargetFormat format) {
  using F = xenos::ColorRenderTargetFormat;
  return format == F::k_16_16_16_16 || format == F::k_16_16_16_16_FLOAT ||
         format == F::k_32_32_FLOAT;
}

// Depth buffers are D32_SFLOAT_S8_UINT unless gh3_native_depth24 stores the
// game's unorm 24-bit ones as D24_UNORM_S8_UINT and its float 24-bit ones as
// D32_SFLOAT (see DepthFormatFor). A resolve turns depth into the R32_SFLOAT
// the translated shaders sample k_24_8 as, and into the guest's memory words
// (see Resolve).
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

VkBlendFactor BlendFactorToVk(xenos::BlendFactor factor) {
  switch (uint32_t(factor)) {
    case 0:
      return VK_BLEND_FACTOR_ZERO;
    case 1:
      return VK_BLEND_FACTOR_ONE;
    case 4:
      return VK_BLEND_FACTOR_SRC_COLOR;
    case 5:
      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 6:
      return VK_BLEND_FACTOR_SRC_ALPHA;
    case 7:
      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 8:
      return VK_BLEND_FACTOR_DST_COLOR;
    case 9:
      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 10:
      return VK_BLEND_FACTOR_DST_ALPHA;
    case 11:
      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 12:
      return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 13:
      return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 14:
      return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 15:
      return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    case 16:
      return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default:
      return VK_BLEND_FACTOR_ZERO;
  }
}

VkBlendOp BlendOpToVk(xenos::BlendOp op) {
  switch (uint32_t(op)) {
    case 1:
      return VK_BLEND_OP_SUBTRACT;
    case 2:
      return VK_BLEND_OP_MIN;
    case 3:
      return VK_BLEND_OP_MAX;
    case 4:
      return VK_BLEND_OP_REVERSE_SUBTRACT;
    default:
      return VK_BLEND_OP_ADD;
  }
}

struct RenderTargetKey {
  uint32_t base_tiles;
  uint32_t pitch;
  uint32_t format;
  uint32_t msaa;
  uint32_t is_depth;
  // log2 of the guest sample count of a surface drawn with one sample whose EDRAM the 1x target at
  // pitch * scale shares (T15, AliasMsaa); 0 for every other target.
  uint32_t alias_msaa;
  // T16: 1 = a imagem escalada da cena (scene_scale) do alvo 1280 de mesmos base/formato.
  uint32_t scene_scaled;
  bool operator==(const RenderTargetKey& o) const { return !std::memcmp(this, &o, sizeof(o)); }
};
struct RenderTargetKeyHasher {
  size_t operator()(const RenderTargetKey& k) const { return size_t(XXH3_64bits(&k, sizeof(k))); }
};

struct RenderTarget {
  RenderTargetKey key;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  // Depth only, for the compute pass that resolves depth.
  VkImageView sample_view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t last_used_frame = 0;
  // T24: altura que os draws 1x explicitos usam neste alvo (0 = ainda nao visto).
  uint32_t known_1x_height = 0;
  // Frame in which the depth was last cleared by the pass (gh3_native_zcull).
  uint64_t cleared_frame = ~uint64_t(0);
  // T15: the 1-sample image of a multisampled surface counts its uses (alias_seq); the 1x target that
  // shares its EDRAM remembers the last one it was brought up to (synced_seq).
  uint64_t alias_seq = 0;
  uint64_t synced_seq = 0;
  // T16 (par imagem cheia F / escalada S): S.scene_dirty = último desenho em S; F.scene_touch = último
  // acesso a F (que pode escrevê-la); scene_seen = o que o outro lado já absorveu.
  uint64_t scene_dirty = 0;
  uint64_t scene_touch = 0;
  uint64_t scene_seen = 0;
  uint64_t checked_clock = ~uint64_t(0);
  // T31: escala com que a imagem escalada foi criada (só em key.scene_scaled). O controlador dinâmico
  // muda a escala; um alvo de outro tamanho precisa ser recriado, inclusive menor.
  float scale_used = 0.0f;
  // F3.3: endereço do destino que ficou com a imagem deste alvo (0 = o conteúdo está aqui). Enquanto
  // não for zero, o alvo tem uma imagem nova e vazia; o conteúdo volta por cópia antes de escrevê-lo.
  uint32_t content_address = 0;
};

// The attachments of a rendering scope: color 0-3 and depth.
struct PassTargets {
  RenderTarget* color[4] = {};
  RenderTarget* depth = nullptr;
  bool operator==(const PassTargets& o) const {
    return !std::memcmp(this, &o, sizeof(o));
  }
  bool Empty() const { return !color[0] && !color[1] && !color[2] && !color[3] && !depth; }
};

// Everything a graphics pipeline depends on, hashed as plain memory.
struct PipelineKey {
  uint64_t vs_hash;
  uint64_t vs_modification;
  uint64_t ps_hash;
  uint64_t ps_modification;
  uint32_t color_formats[4];
  uint32_t depth_format;
  uint32_t samples;
  uint32_t topology;
  uint32_t geometry_shader;
  uint32_t primitive_restart;
  uint32_t cull_mode;
  uint32_t front_face_clockwise;
  uint32_t polygon_line;
  uint32_t depth_clamp;
  uint32_t depth_test;
  uint32_t depth_write;
  uint32_t depth_compare;
  uint32_t stencil_test;
  uint32_t stencil_front[4];
  uint32_t stencil_back[4];
  uint32_t blend[4];
  uint32_t write_masks;
  uint32_t texture_signs[8];
  uint64_t layout;
};

struct PipelineLayout {
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout textures_vertex = VK_NULL_HANDLE;
  VkDescriptorSetLayout textures_pixel = VK_NULL_HANDLE;
};

struct VertexRangeKey {
  uint32_t address;
  uint32_t size;
  uint64_t hash;
  bool operator==(const VertexRangeKey& o) const {
    return address == o.address && size == o.size && hash == o.hash;
  }
};
struct VertexRangeKeyHasher {
  size_t operator()(const VertexRangeKey& k) const { return size_t(k.hash ^ k.address); }
};

}  // namespace

struct Renderer::Impl {
  Renderer* owner = nullptr;
  rex::ui::vulkan::VulkanProvider* provider = nullptr;
  const VulkanDevice* device = nullptr;
  VkDevice vk = VK_NULL_HANDLE;
  rex::memory::Memory* memory = nullptr;
  RegisterFile* regs = nullptr;

  // Shaders.
  std::unique_ptr<SpirvShaderTranslator> translator;
  rex::string::StringBuffer disasm;
  std::unordered_map<uint64_t, std::unique_ptr<VulkanShader>> shaders;
  struct KnownShader {
    VulkanShader* shader;
    uint64_t sample;  // of all of the code
    uint64_t verified_frame;
    uint64_t head;  // of the first 16 words
  };
  std::unordered_map<uint64_t, KnownShader> shaders_by_location;
  // F4.1: o microcódigo que vem no próprio anel (IM_LOAD_IMMEDIATE) era trocado e hasheado a
  // cada pacote. A cache é exata (memcmp) e guarda o shader resolvido.
  ring_caches::ImLoadCache im_load_cache;
  VulkanShader* active_vs = nullptr;
  VulkanShader* active_ps = nullptr;
  bool specialize_signs = false;

  // Layouts.
  VkDescriptorSetLayout layout_empty = VK_NULL_HANDLE;
  VkDescriptorSetLayout layout_shared = VK_NULL_HANDLE;
  VkDescriptorSetLayout layout_constants = VK_NULL_HANDLE;
  uint32_t shared_binding_count = 1;
  std::unordered_map<uint64_t, VkDescriptorSetLayout> texture_set_layouts;
  std::unordered_map<uint64_t, PipelineLayout> pipeline_layouts;

  // The stream: one host-visible buffer the shaders read vertices from (as
  // the translator's "shared memory"), with the indices, the constants and the
  // texture staging data in it too.
  VkBuffer stream_buffer = VK_NULL_HANDLE;
  VkDeviceMemory stream_memory = VK_NULL_HANDLE;
  uint8_t* stream_mapping = nullptr;
  uint64_t stream_head = 0;  // total bytes ever allocated
  VkDescriptorPool static_pool = VK_NULL_HANDLE;
  VkDescriptorSet set_shared = VK_NULL_HANDLE;
  VkDescriptorSet set_constants = VK_NULL_HANDLE;
  VkDeviceSize ubo_alignment = 256;

  // Submissions.
  struct Submission {
    VkCommandPool pool = VK_NULL_HANDLE;
    // The main command buffer's own: the setup one is recorded on the ring
    // thread while the recording thread records this one.
    VkCommandPool main_pool = VK_NULL_HANDLE;
    VkCommandBuffer setup = VK_NULL_HANDLE;
    VkCommandBuffer main = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool pending = false;
    uint64_t number = 0;
    uint64_t frame = 0;
    // Oldest stream position the submission reads.
    uint64_t stream_oldest = 0;
    std::vector<VkDescriptorPool> descriptor_pools;
    size_t descriptor_pool_used = 0;
    bool timed = false;
  };
  VkQueryPool timestamps = VK_NULL_HANDLE;
  float timestamp_period = 1.0f;
  Submission submissions[kSubmissionSlots];
  uint64_t submission_number = 0;  // the one being recorded, if recording
  bool recording = false;
  uint64_t frame = 1;
  uint64_t completed_frame = 0;
  // Whether render targets get the game's sample count (gh3_native_msaa).
  // The targets are sized in guest pixels whatever the sample count, so one
  // sample changes only the images, the pipelines and the resolves.
  bool multisample = true;
  // Multisampled surfaces as their samples (gh3_native_msaa_as_samples,
  // native_edram_samples.h): wins over multisample.
  bool msaa_as_samples = true;
  // gh3_native_scene_scale já limitada; 1.0 = desligado (native_scene_scale.h).
  float scene_scale = 1.0f;
  bool scene_logged = false;
  uint64_t scene_clock = 0;
  // T31: controlador dinâmico (gh3_native_scene_dynamic); ausente = escala fixa.
  std::unique_ptr<scene::DynamicScale> scene_dynamic;
  // T32: CAS no upscale da cena (gh3_native_scene_cas). Pipeline por formato do alvo; o descriptor
  // set é alocado por submissão (AllocateSet), como o copy de profundidade.
  bool scene_cas = false;
  float scene_cas_sharpness = 0.0f;
  VkDescriptorSetLayout scene_cas_set_layout = VK_NULL_HANDLE;
  VkPipelineLayout scene_cas_layout = VK_NULL_HANDLE;
  struct SceneCasPipeline {
    VkFormat format;
    VkPipeline pipeline;
  };
  std::vector<SceneCasPipeline> scene_cas_pipelines;
  bool scene_cas_failed = false;
  bool scene_cas_logged = false;
  // T47: upscaler escolhido (gh3_native_scene_upscale = linear|cas|fsr1). O FSR1 é a etapa EASU, com
  // pipeline e descriptor set próprios: o PS do EASU lê a imagem em binding 0 e um sampler
  // linear-clamp imutável em binding 1 (é o que o presenter do SDK liga no `guest_output_ffx_fsr_easu_ps`).
  scene::UpscaleMode scene_upscale_mode = scene::UpscaleMode::kLinear;
  VkSampler scene_easu_sampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout scene_easu_set_layout = VK_NULL_HANDLE;
  VkPipelineLayout scene_easu_layout = VK_NULL_HANDLE;
  struct SceneEasuPipeline {
    VkFormat format;
    VkPipeline pipeline;
  };
  std::vector<SceneEasuPipeline> scene_easu_pipelines;
  bool scene_easu_failed = false;
  bool scene_easu_logged = false;
  // Último extent logado (o EASU pode ver mais de um tamanho na mesma sessão).
  scene::SceneBlit scene_easu_last_extent{};
  // gh3_native_alias_cap (ver o cvar).
  bool alias_cap = true;
  // gh3_native_zcull (ver o cvar).
  bool zcull = false;
  // D24_UNORM_S8_UINT for the game's kD24S8 buffers (gh3_native_depth24).
  bool depth24 = false;
  // The float24 ones (here only shadow maps) become D32_SFLOAT without
  // stencil, 32 instead of 64 bits per pixel; a stencil test on them passes.
  VkFormat DepthFormatFor(uint32_t guest_format) const {
    if (!depth24) return kDepthFormat;
    return guest_format == uint32_t(xenos::DepthRenderTargetFormat::kD24S8)
               ? VK_FORMAT_D24_UNORM_S8_UINT
               : VK_FORMAT_D32_SFLOAT;
  }
  static bool HasStencil(VkFormat format) { return format != VK_FORMAT_D32_SFLOAT; }
  VkDescriptorSetLayout depth_convert_set_layout = VK_NULL_HANDLE;
  VkPipelineLayout depth_convert_layout = VK_NULL_HANDLE;
  VkPipeline depth_convert_pipeline = VK_NULL_HANDLE;
  VkDescriptorPool depth_convert_pool = VK_NULL_HANDLE;
  VkDescriptorSet depth_convert_set = VK_NULL_HANDLE;
  // Depth straight from the depth buffer into the R32_SFLOAT image, in one
  // compute pass (depth_to_float.cs.glsl).
  VkDescriptorSetLayout depth_copy_set_layout = VK_NULL_HANDLE;
  VkPipelineLayout depth_copy_layout = VK_NULL_HANDLE;
  VkPipeline depth_copy_pipeline = VK_NULL_HANDLE;
  // Color resolves by compute (copy_color.cs.glsl), same set layout.
  VkPipelineLayout copy_color_layout = VK_NULL_HANDLE;
  VkPipeline copy_color_pipeline = VK_NULL_HANDLE;
  bool ComputeColorCopy(VkCommandBuffer c, VkImageView source, VkImageView dest, int32_t sx,
                        int32_t sy, int32_t dx, int32_t dy, uint32_t width, uint32_t height);
  bool CreateDepthConversion();

  // GPU time by render pass and resolve (gh3_native_gpu_profile): on one
  // frame in 16, a timestamp is written where each pass or resolve begins,
  // and the time to the next one is charged to it.
  static constexpr uint32_t kProfileQueries = 512;
  VkQueryPool profile_pool = VK_NULL_HANDLE;
  bool profile_frame = false;
  struct ProfileSegment {
    uint64_t key;
    uint32_t draws;
  };
  std::vector<ProfileSegment> profile_segments[kSubmissionSlots];
  bool profile_closed[kSubmissionSlots] = {};
  // F4.1: só as queries que o slot usou da última vez são limpas (o pool inteiro era 512 por
  // quadro perfilado). profile_query_used guarda o que o slot escreveu na última vez.
  ring_caches::SelectiveQueryReset profile_queries[kSubmissionSlots];
  uint32_t profile_query_used[kSubmissionSlots] = {};
  uint64_t profile_query_reset_count = 0;
  struct ProfileTotal {
    double ns = 0;
    uint64_t count = 0;
    uint64_t draws = 0;
  };
  std::unordered_map<uint64_t, ProfileTotal> profile_totals;
  uint32_t profiled_frames = 0;
  double profiled_ns = 0;
  // Kinds, in the key's top 4 bits.
  enum : uint64_t { kProfileOther = 0, kProfilePass = 1, kProfileResolve = 2 };
  static uint64_t ProfileTargetBits(const RenderTarget* rt) {
    if (!rt) return 0xFFFFFF;
    return (uint64_t(rt->key.base_tiles & 0xFFF) << 12) | (uint64_t(rt->key.pitch & 0x7FF) << 1) |
           (rt->key.format & 1);
  }
  void ProfileMark(uint64_t key) {
    if (!profile_frame || !profile_pool || !recording) return;
    uint32_t slot = uint32_t(submission_number % kSubmissionSlots);
    auto& segments = profile_segments[slot];
    // The last query is kept for the closing timestamp.
    if (segments.size() + 1 >= kProfileQueries) return;
    const uint32_t index = uint32_t(segments.size());
    ring_caches::SelectiveQueryReset::Plan reset;
    if (profile_queries[slot].NeedReset(index, &reset)) {
      // Bloco além do que o slot usou da última vez: precisa ser limpo antes da escrita.
      dfn().vkCmdResetQueryPool(cmd(), profile_pool, slot * kProfileQueries + reset.first,
                                reset.count);
      profile_query_reset_count += reset.count;
    }
    dfn().vkCmdWriteTimestamp(cmd(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, profile_pool,
                              slot * kProfileQueries + index);
    profile_queries[slot].OnWrite(index);
    segments.push_back({key, 0});
  }
  void ProfileDraw() {
    if (!profile_frame) return;
    auto& segments = profile_segments[submission_number % kSubmissionSlots];
    if (!segments.empty()) ++segments.back().draws;
  }
  void ProfileCollect(uint32_t slot);
  struct TextureTimer {
    uint64_t& total;
    uint64_t start;
    explicit TextureTimer(uint64_t& t) : total(t), start(NowNs()) {}
    ~TextureTimer() { total += NowNs() - start; }
  };
  void ProfileReport();
  // The host target a guest surface lands on (native_edram_samples.h).
  edram::Surface SurfaceFor(reg::RB_SURFACE_INFO info) const {
    return edram::HostSurface(info.surface_pitch, uint32_t(info.msaa_samples), msaa_as_samples,
                              multisample);
  }
  bool null_images_initialized = false;

  // Render targets.
  std::unordered_map<RenderTargetKey, std::unique_ptr<RenderTarget>, RenderTargetKeyHasher>
      render_targets;
  bool in_pass = false;
  PassTargets pass;
  VkExtent2D pass_extent = {};

  // Pipelines.
  std::unordered_map<uint64_t, VkPipeline> pipelines;
  std::unordered_map<uint32_t, VkShaderModule> geometry_shaders;
  VkPipelineCache driver_cache = VK_NULL_HANDLE;
  std::filesystem::path driver_cache_path;
  // F2.5: versão do cache do driver (formato da chave + passes de shader), gravada no cabeçalho.
  uint32_t cache_version_ = 0;
  uint64_t pipelines_at_last_save = 0;
  std::chrono::steady_clock::time_point last_pipeline_time;
  size_t driver_cache_saved_size = 0;
  std::atomic<bool> driver_cache_writing{false};
  std::thread driver_cache_writer;

  // Frame time tracking.
  std::chrono::steady_clock::time_point last_swap_time;
  RendererStats at_last_swap;

  // What the main command buffer has bound.
  VkPipeline bound_pipeline = VK_NULL_HANDLE;
  VkPipelineLayout bound_layout = VK_NULL_HANDLE;
  bool bound_static_sets = false;
  VkViewport bound_viewport = {};
  VkRect2D bound_scissor = {};
  float bound_depth_bias[2] = {NAN, NAN};
  float bound_blend_constants[4] = {NAN, NAN, NAN, NAN};
  uint32_t bound_stencil[6] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u};

  // Vertex and index data already in the stream: stream position by the
  // range and its content.
  struct RangeCopy {
    VertexRangeKey key = {};
    uint64_t position = 0;
  };
  FlatMap64<RangeCopy> vertex_ranges;
  static uint64_t RangeSlot(const VertexRangeKey& k) {
    uint64_t h = k.hash ^ ((uint64_t(k.address) << 32) | k.size);
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 31;
    return h == ~uint64_t(0) ? 0 : h;
  }
  // The stream position of a copy of this range, or UINT64_MAX.
  uint64_t FindRange(const VertexRangeKey& k) {
    RangeCopy* c = vertex_ranges.find(RangeSlot(k));
    return c && c->key == k ? c->position : UINT64_MAX;
  }
  void AddRange(const VertexRangeKey& k, uint64_t position) {
    vertex_ranges[RangeSlot(k)] = {k, position};
  }

  TextureCache textures;
  UploadTarget upload;

  // Texture descriptor sets, by content: the game uses the same few hundred
  // combinations of textures and samplers every frame, and allocating and
  // writing a set per draw (then freeing them one by one on pool reset) was
  // an eighth of the renderer's time.
  std::unordered_map<uint64_t, VkDescriptorSet> texture_set_cache;
  std::vector<VkDescriptorPool> set_pools;
  size_t set_pool_used = 0;
  struct RetiredPools {
    uint64_t frame;
    std::vector<VkDescriptorPool> pools;
  };
  std::vector<RetiredPools> retired_set_pools;
  uint64_t texture_views_epoch = 0;

  // Vertex buffer content hashes, computed once per frame for each range: many
  // draws share one buffer.
  struct VertexHash {
    uint64_t frame = UINT64_MAX;
    uint64_t hash = 0;
    // Last real content hash, and how many checks in a row found it changed.
    uint64_t content = 0;
    uint32_t changes = 0;
    ChangeDetector detector;
  };
  // This frame's hash of a vertex or index range. Data the game rewrites
  // every frame (skinned meshes, particles) was hashed in full every frame
  // only to be copied anyway, as the hash never matched: once a range has
  // changed on 4 checks in a row it gets a hash unique to the frame without
  // reading it (still copied once per frame, shared by its draws), and is
  // checked for real every 16 frames to notice it settling.
  uint64_t RangeHash(VertexHash& known, const uint8_t* data, uint32_t bytes, uint32_t address) {
    if (known.frame == frame) return known.hash;
    known.frame = frame;
    if (known.changes >= 4 && (frame & 15)) {
      known.hash = (frame + 1) * 0xD1B54A32D192ED03ull ^ (uint64_t(address) << 20) ^ bytes;
      return known.hash;
    }
    uint64_t content = known.detector.Check(data, bytes, frame, address >> 4, vertex_hash_bytes);
    known.changes = content != known.content ? std::min(known.changes + 1, 8u) : 0;
    known.content = content;
    known.hash = content;
    return content;
  }
  uint64_t vertex_hash_bytes = 0;
  FlatMap64<VertexHash> vertex_hashes;

  // Swap.
  VkDescriptorSetLayout swap_layout_ramp = VK_NULL_HANDLE;
  VkDescriptorSetLayout swap_layout_source = VK_NULL_HANDLE;
  VkDescriptorSetLayout swap_layout_dest = VK_NULL_HANDLE;
  VkPipelineLayout swap_pipeline_layout = VK_NULL_HANDLE;
  VkPipeline swap_pipeline = VK_NULL_HANDLE;
  VkBuffer ramp_buffer = VK_NULL_HANDLE;
  VkDeviceMemory ramp_memory = VK_NULL_HANDLE;
  uint8_t* ramp_mapping = nullptr;
  VkBufferView ramp_views[kSubmissionSlots] = {};
  VkDescriptorPool swap_pool = VK_NULL_HANDLE;
  VkDescriptorSet swap_sets[kSubmissionSlots][3] = {};
  uint32_t gamma_ramp[256] = {};
  // F0.2: o que este quadro limpou e resolveu; zerado a cada swap.
  struct SwapFrame {
    bool has_clear_color = false;
    uint64_t clear_color = 0;
    uint32_t clear_color_target = 0;
    bool has_clear_depth = false;
    uint32_t clear_depth = 0;
    uint32_t clear_depth_target = 0;
    uint32_t resolve_count = 0;
    uint32_t resolve_src = 0;
    uint32_t resolve_src_format = 0;
    uint32_t resolve_dst = 0;
  };
  SwapFrame swap_frame;
  // F1.4: conta por formato (uma vez por conjunto de formatos, ver o Swap).
  format_audit::Audit format_audit;
  uint64_t format_audit_logged_targets = ~uint64_t(0);

  SpirvShaderTranslator::SystemConstants system_constants = {};

  struct DrawSetup {
    bool valid = false;
    uint64_t state_generation = 0;
    uint64_t fetch_generation = 0;
    uint64_t rt_generation = 0;
    VulkanShader* active_vs = nullptr;
    VulkanShader* active_ps = nullptr;
    uint32_t initiator_state = 0;
    uint32_t skip_reason = UINT32_MAX;
    VulkanShader* vs = nullptr;
    VulkanShader* ps = nullptr;
    bool primitive_polygonal = false;
    reg::RB_DEPTHCONTROL depth_control = {};
    bool convert_fan = false, convert_loop = false, strip = false, reset_enabled = false;
    // Without geometry shaders (native_primitive_expand.h).
    bool convert_quad = false;
    Shader::HostVertexShaderType host_vs_type = Shader::HostVertexShaderType::kVertex;
    draw_util::Scissor scissor = {};
    draw_util::ViewportInfo viewport = {};
    PassTargets targets;
    uint32_t needed_height = 0;
    bool scene_scaled = false;  // T16: viewport/scissor/alvos na imagem escalada
    const PipelineLayout* layout = nullptr;
    uint32_t counts[4] = {};
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkViewport vp = {};
    float bias_constant = 0.0f, bias_slope = 0.0f;
    float blend_constants[4] = {};
    uint32_t stencil[6] = {};
  };
  DrawSetup setup;
  std::unordered_map<uint64_t, DrawSetup> setup_cache;
  uint64_t setup_cache_rt_generation = 0;
  // Counts setup rebuilds, so derived caches can tell a setup apart.
  uint64_t setup_generation = 0;
  struct SystemInputs {
    uint64_t setup_generation = ~0ull;
    uint64_t fetch_generation = 0;
    uint64_t index_generation = 0;
    uint32_t used_mask = 0;
  };
  SystemInputs system_inputs;
  struct RetiredRenderTarget {
    uint64_t frame;
    VkImage image;
    VkImageView view;
    VkImageView sample_view;
    VkDeviceMemory memory;
  };
  std::vector<RetiredRenderTarget> retired_render_targets;
  // Staging for depth resolves (depth to buffer to color image).
  static constexpr VkDeviceSize kDepthScratchSize = 16u << 20;
  VkBuffer depth_scratch = VK_NULL_HANDLE;
  VkDeviceMemory depth_scratch_memory = VK_NULL_HANDLE;
  // Bumped whenever a render target is created, as setups hold pointers.
  uint64_t rt_generation = 1;
  bool BuildDrawSetup(uint32_t initiator_value);

  struct TextureBindings {
    bool valid = false;
    uint64_t frame = 0;
    uint64_t submission = 0;
    uint64_t views_epoch = 0;
    const VulkanShader* vs = nullptr;
    const VulkanShader* ps = nullptr;
    const PipelineLayout* layout = nullptr;
    uint32_t used_mask = 0;
    uint32_t slots = 0;
    VkDescriptorSet sets[2] = {};
    // Reusable in later frames through this one: no texture behind it is due
    // a content check before then. The textures are marked used on reuse.
    uint64_t valid_until = 0;
    uint64_t set_generation = 0;
    uint64_t answers_epoch = 0;
    // The views the sets hold, to check them alive when views_epoch moved.
    uint32_t view_count = 0;
    VkImageView views[24] = {};
    uint64_t view_ids[24] = {};
    uint32_t texture_count = 0;
    void* textures[24] = {};
  };
  FlatMap64<TextureBindings> tb_cache;

  // What each fetch slot last resolved to, this frame and submission: most
  // binding rebuilds change one or two slots of several.
  struct SlotViewEntry {
    uint64_t frame = 0;
    uint64_t submission = 0;
    uint64_t views_epoch = 0;
    uint32_t words[6] = {};
    uint32_t variant = 0;
    VkImageView view = VK_NULL_HANDLE;
    void* texture = nullptr;
    uint64_t valid_until = 0;
  };
  SlotViewEntry slot_views[32][8];
  // Set by SlotView: the texture behind its answer and how long it holds.
  void* slot_texture = nullptr;
  uint64_t slot_valid_until = 0;
  struct SlotSamplerEntry {
    bool valid = false;
    uint32_t words[6] = {};
    uint32_t binding_bits = 0;
    VkSampler sampler = VK_NULL_HANDLE;
  };
  SlotSamplerEntry slot_samplers[32][4];

  VkImageView SlotView(uint32_t slot, xenos::FetchOpDimension dimension, bool is_signed) {
    const uint32_t* words = &(*regs)[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
                                     slot * 6];
    uint32_t variant = uint32_t(dimension) | (uint32_t(is_signed) << 2);
    SlotViewEntry& e = slot_views[slot][variant & 7];
    if (e.frame == frame && e.submission == submission_number &&
        e.views_epoch == textures.views_epoch() && e.variant == variant &&
        !std::memcmp(e.words, words, sizeof(e.words))) {
      slot_texture = e.texture;
      slot_valid_until = e.valid_until;
      return e.view;
    }
    VkImageView view =
        textures.GetView(regs->GetTextureFetch(slot), dimension, is_signed, upload);
    e.texture = slot_texture = textures.last_texture();
    e.valid_until = slot_valid_until = textures.last_valid_until();
    e.frame = frame;
    e.submission = submission_number;
    e.views_epoch = textures.views_epoch();
    e.variant = variant;
    std::memcpy(e.words, words, sizeof(e.words));
    e.view = view;
    return view;
  }

  VkSampler SlotSampler(const SpirvShader::SamplerBinding& binding) {
    uint32_t slot = binding.fetch_constant;
    const uint32_t* words = &(*regs)[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
                                     slot * 6];
    uint32_t bits = uint32_t(binding.mag_filter) | (uint32_t(binding.min_filter) << 2) |
                    (uint32_t(binding.mip_filter) << 4) |
                    (uint32_t(binding.aniso_filter) << 6);
    SlotSamplerEntry* entries = slot_samplers[slot];
    for (uint32_t i = 0; i < 4; ++i) {
      SlotSamplerEntry& e = entries[i];
      if (e.valid && e.binding_bits == bits && !std::memcmp(e.words, words, sizeof(e.words))) {
        return e.sampler;
      }
    }
    VkSampler sampler = textures.GetSampler(regs->GetTextureFetch(slot), binding);
    // Samplers are never destroyed, so these stay valid across frames.
    SlotSamplerEntry& e = entries[(bits ^ words[0]) & 3];
    e.valid = true;
    e.binding_bits = bits;
    std::memcpy(e.words, words, sizeof(e.words));
    e.sampler = sampler;
    return sampler;
  }
  uint64_t tb_cache_frame = 0;

  // The fetch constants as the shaders get them: the guest's, with vertex
  // buffer addresses pointing into the stream.
  std::array<uint32_t, 6 * 32> fetch_words = {};
  std::array<uint64_t, 96> fetch_resolved_frame = {};
  uint64_t fetch_words_frame = 0;
  uint64_t fetch_words_submission = 0;
  uint32_t last_fetch_words[6 * 32] = {};

  uint32_t bound_offsets[SpirvShaderTranslator::kConstantBufferCount] = {};
  bool bound_offsets_valid = false;
  VkDescriptorSet bound_texture_sets[2] = {};
  VkDeviceSize bound_index_offset = 0;
  VkIndexType bound_index_type = VK_INDEX_TYPE_UINT16;
  bool bound_index_valid = false;

  // The last upload of each constant buffer, reused by the next draws while
  // the registers behind it are unchanged (the ring counts register writes)
  // and the stream still holds it (same submission).
  struct ReusedUpload {
    uint64_t submission = 0;
    uint64_t generation = 0;
    const void* shader = nullptr;
    uint64_t frame = 0;
    VkDeviceSize offset = 0;
  };
  ReusedUpload last_fetch, last_vs_float, last_ps_float, last_bool_loop;
  SpirvShaderTranslator::SystemConstants last_system_constants = {};
  ReusedUpload last_system;
  bool Reusable(const ReusedUpload& u, uint64_t generation, const void* shader,
                bool per_frame = false) const {
    return u.submission == submission_number && u.generation == generation &&
           u.shader == shader && (!per_frame || u.frame == frame);
  }

  const VulkanDevice::Functions& dfn() const { return device->functions(); }
  // Not in ReXGlue's device function table.
  PFN_vkCmdClearDepthStencilImage vkCmdClearDepthStencilImage = nullptr;
  PFN_vkCmdResolveImage vkCmdResolveImage = nullptr;
  PFN_vkCmdBlitImage vkCmdBlitImage = nullptr;
  PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
  Submission& current() { return submissions[submission_number % kSubmissionSlots]; }
  // Waits for queued draws first: the recording thread and this one never
  // record into the command buffer at the same time.
  VkCommandBuffer cmd() {
    DrainRecorder();
    return current().main;
  }

  // --- Recording thread ------------------------------------------------------------
  // A draw's Vulkan calls (binds, dynamic state, the draw), decided on the
  // ring thread and made on the recording thread (gh3_native_record_thread)
  // or straight away. In the busiest areas the driver's share of a draw was
  // about a quarter of the ring thread's time; on another core it overlaps
  // with the ring preparing the next draw. Everything else recorded into the
  // main command buffer goes through cmd(), which waits for the queue to
  // empty, and the texture uploads go into the setup command buffer, which
  // has a pool of its own so the two threads never share one.
  struct DrawCommand {
    VkCommandBuffer commands;
    uint32_t flags;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSet set_shared;
    VkDescriptorSet set_constants;
    uint32_t offsets[SpirvShaderTranslator::kConstantBufferCount];
    VkDescriptorSet texture_sets[2];
    VkViewport viewport;
    VkRect2D scissor;
    float bias_constant, bias_slope;
    float blend[4];
    uint32_t stencil[6];
    VkBuffer index_buffer;
    VkDeviceSize index_offset;
    VkIndexType index_type;
    uint32_t count;
  };
  enum : uint32_t {
    kDrawPipeline = 1u << 0,
    kDrawSharedSet = 1u << 1,
    kDrawConstants = 1u << 2,
    kDrawTexturesVertex = 1u << 3,
    kDrawTexturesPixel = 1u << 4,
    kDrawViewport = 1u << 5,
    kDrawScissor = 1u << 6,
    kDrawDepthBias = 1u << 7,
    kDrawBlend = 1u << 8,
    kDrawStencilCompare = 1u << 9,
    kDrawStencilWrite = 1u << 10,
    kDrawStencilReference = 1u << 11,
    kDrawIndexBuffer = 1u << 12,
    kDrawIndexed = 1u << 13,
  };
  void ExecuteDrawCommand(const DrawCommand& d) const {
    VkCommandBuffer c = d.commands;
    const auto& f = device->functions();
    if (d.flags & kDrawPipeline) {
      f.vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipeline);
    }
    if (d.flags & kDrawSharedSet) {
      f.vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, d.layout,
                                SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram, 1,
                                &d.set_shared, 0, nullptr);
    }
    if (d.flags & kDrawConstants) {
      f.vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, d.layout,
                                SpirvShaderTranslator::kDescriptorSetConstants, 1,
                                &d.set_constants, SpirvShaderTranslator::kConstantBufferCount,
                                d.offsets);
    }
    if (d.flags & kDrawTexturesVertex) {
      f.vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, d.layout,
                                SpirvShaderTranslator::kDescriptorSetTexturesVertex, 1,
                                &d.texture_sets[0], 0, nullptr);
    }
    if (d.flags & kDrawTexturesPixel) {
      f.vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, d.layout,
                                SpirvShaderTranslator::kDescriptorSetTexturesPixel, 1,
                                &d.texture_sets[1], 0, nullptr);
    }
    if (d.flags & kDrawViewport) f.vkCmdSetViewport(c, 0, 1, &d.viewport);
    if (d.flags & kDrawScissor) f.vkCmdSetScissor(c, 0, 1, &d.scissor);
    if (d.flags & kDrawDepthBias) f.vkCmdSetDepthBias(c, d.bias_constant, 0.0f, d.bias_slope);
    if (d.flags & kDrawBlend) f.vkCmdSetBlendConstants(c, d.blend);
    if (d.flags & kDrawStencilCompare) {
      f.vkCmdSetStencilCompareMask(c, VK_STENCIL_FACE_FRONT_BIT, d.stencil[0]);
      f.vkCmdSetStencilCompareMask(c, VK_STENCIL_FACE_BACK_BIT, d.stencil[1]);
    }
    if (d.flags & kDrawStencilWrite) {
      f.vkCmdSetStencilWriteMask(c, VK_STENCIL_FACE_FRONT_BIT, d.stencil[2]);
      f.vkCmdSetStencilWriteMask(c, VK_STENCIL_FACE_BACK_BIT, d.stencil[3]);
    }
    if (d.flags & kDrawStencilReference) {
      f.vkCmdSetStencilReference(c, VK_STENCIL_FACE_FRONT_BIT, d.stencil[4]);
      f.vkCmdSetStencilReference(c, VK_STENCIL_FACE_BACK_BIT, d.stencil[5]);
    }
    if (d.flags & kDrawIndexBuffer) {
      f.vkCmdBindIndexBuffer(c, d.index_buffer, d.index_offset, d.index_type);
    }
    if (d.flags & kDrawIndexed) {
      f.vkCmdDrawIndexed(c, d.count, 1, 0, 0, 0);
    } else {
      f.vkCmdDraw(c, d.count, 1, 0, 0);
    }
  }
  static constexpr uint32_t kRecordQueueSize = 4096;
  std::unique_ptr<DrawCommand[]> record_queue;
  std::atomic<uint32_t> record_head{0};
  std::atomic<uint32_t> record_tail{0};
  std::atomic<bool> record_stop{false};
  std::atomic<bool> record_sleeping{false};
  std::mutex record_mutex;
  std::condition_variable record_cv;
  std::thread record_thread;
  bool record_threaded = false;

  void WakeRecorder() {
    if (record_sleeping.load(std::memory_order_seq_cst)) {
      std::lock_guard<std::mutex> lock(record_mutex);
      record_cv.notify_one();
    }
  }

  void RecordThread() {
#if defined(__SWITCH__)
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2C);
    // Off the ring thread's core.
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 1, 0x3);
#endif
    uint32_t idle = 0;
    while (true) {
      uint32_t tail = record_tail.load(std::memory_order_relaxed);
      if (tail != record_head.load(std::memory_order_acquire)) {
        ExecuteDrawCommand(record_queue[tail % kRecordQueueSize]);
        record_tail.store(tail + 1, std::memory_order_release);
        idle = 0;
        continue;
      }
      if (record_stop.load(std::memory_order_acquire)) return;
      // Briefly: spinning longer took cores from the game's own threads.
      if (++idle < 16) {
        std::this_thread::yield();
        continue;
      }
      std::unique_lock<std::mutex> lock(record_mutex);
      record_sleeping.store(true, std::memory_order_seq_cst);
      if (record_head.load(std::memory_order_seq_cst) == tail &&
          !record_stop.load(std::memory_order_acquire)) {
        record_cv.wait_for(lock, std::chrono::milliseconds(2));
      }
      record_sleeping.store(false, std::memory_order_relaxed);
      idle = 0;
    }
  }

  void SubmitDraw(const DrawCommand& d) {
    if (!record_threaded) {
      ExecuteDrawCommand(d);
      return;
    }
    uint32_t head = record_head.load(std::memory_order_relaxed);
    while (head - record_tail.load(std::memory_order_acquire) >= kRecordQueueSize) {
      WakeRecorder();
      std::this_thread::yield();
    }
    record_queue[head % kRecordQueueSize] = d;
    record_head.store(head + 1, std::memory_order_seq_cst);
    // In batches: the recording thread kept up draw by draw, slept between
    // them, and waking it for every draw (a lock and a system call) showed as
    // 6% of this thread in lock contention. It is also woken when this
    // thread is about to wait for the game (KickRecorder), and anything
    // recording into the command buffer drains the queue first.
    if (head + 1 - record_tail.load(std::memory_order_relaxed) >= kRecordBatch) WakeRecorder();
  }
  static constexpr uint32_t kRecordBatch = 32;

  void KickRecorder() {
    if (record_threaded &&
        record_tail.load(std::memory_order_acquire) != record_head.load(std::memory_order_relaxed)) {
      WakeRecorder();
    }
  }

  void DrainRecorder() {
    if (!record_threaded) return;
    uint32_t head = record_head.load(std::memory_order_relaxed);
    if (record_tail.load(std::memory_order_acquire) == head) return;
    uint64_t start = NowNs();
    uint32_t spins = 0;
    while (record_tail.load(std::memory_order_acquire) != head) {
      WakeRecorder();
      if (++spins > 32) std::this_thread::yield();
    }
    frame_drain_wait_ns += NowNs() - start;
  }
  RendererStats& stats() { return owner->stats_; }

  // --- Setup ---------------------------------------------------------------------

  bool Initialize();
  void Shutdown();
  bool CreateSwapResources();

  // --- Submissions and the stream ----------------------------------------------

  // A frame is complete when every submission up to it is: a frame can be
  // split over several submissions (when the stream fills up), and counting it
  // complete when its first one finished freed textures and staging memory the
  // rest still used, an MMU fault that lost the GPU. The frame being recorded
  // is never complete.
  void UpdateCompletedFrame() {
    uint64_t oldest = frame;
    for (const Submission& other : submissions) {
      if (other.pending) oldest = std::min(oldest, other.frame);
    }
    if (oldest > 0) {
      completed_frame = std::max(completed_frame, oldest - 1);
    }
  }

  void Completed(Submission& s) {
    s.pending = false;
    UpdateCompletedFrame();
    ProfileCollect(uint32_t(&s - submissions));
    if (s.timed && timestamps) {
      uint32_t slot = uint32_t(&s - submissions);
      uint64_t ticks[2] = {};
      if (dfn().vkGetQueryPoolResults(vk, timestamps, slot * 2, 2, sizeof(ticks), ticks,
                                      sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
          ticks[1] > ticks[0]) {
        stats().gpu_ns += uint64_t(double(ticks[1] - ticks[0]) * timestamp_period);
        ++stats().gpu_submissions;
      }
      s.timed = false;
    }
  }

  // This frame's waits and texture time (RendererStats::frame_*).
  uint64_t frame_upload_wait_ns = 0, frame_fence_wait_ns = 0, frame_drain_wait_ns = 0,
           frame_texture_ns = 0, frame_draw_count = 0, frame_stream_wait_ns = 0;
  static uint64_t NowNs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
  }

  void WaitSubmission(Submission& s) {
    if (s.pending) {
      uint64_t start = NowNs();
      dfn().vkWaitForFences(vk, 1, &s.fence, VK_TRUE, UINT64_MAX);
      frame_fence_wait_ns += NowNs() - start;
      Completed(s);
    }
  }

  void PollCompleted() {
    for (Submission& s : submissions) {
      if (s.pending && dfn().vkGetFenceStatus(vk, s.fence) == VK_SUCCESS) {
        Completed(s);
      }
    }
    UpdateCompletedFrame();
  }

  void BeginRecording() {
    if (recording) {
      return;
    }
    ++submission_number;
    Submission& s = current();
    WaitSubmission(s);
    dfn().vkResetFences(vk, 1, &s.fence);
    dfn().vkResetCommandPool(vk, s.pool, 0);
    dfn().vkResetCommandPool(vk, s.main_pool, 0);
    for (VkDescriptorPool pool : s.descriptor_pools) {
      dfn().vkResetDescriptorPool(vk, pool, 0);
    }
    s.descriptor_pool_used = 0;
    s.number = submission_number;
    s.frame = frame;
    s.stream_oldest = stream_head;
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    dfn().vkBeginCommandBuffer(s.setup, &begin);
    dfn().vkBeginCommandBuffer(s.main, &begin);
    if (timestamps) {
      uint32_t slot = uint32_t(submission_number % kSubmissionSlots);
      dfn().vkCmdResetQueryPool(s.setup, timestamps, slot * 2, 2);
      dfn().vkCmdWriteTimestamp(s.setup, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamps, slot * 2);
      s.timed = true;
    }
    if (!null_images_initialized) {
      textures.InitializeNullImages(s.setup);
      null_images_initialized = true;
    }
    {
      uint32_t slot = uint32_t(submission_number % kSubmissionSlots);
      profile_segments[slot].clear();
      profile_closed[slot] = false;
      if (profile_frame && profile_pool) {
        // F4.1: só o que este slot escreveu da última vez pode guardar resultado velho.
        const ring_caches::SelectiveQueryReset::Plan reset =
            profile_queries[slot].Begin(profile_query_used[slot]);
        if (reset.count) {
          dfn().vkCmdResetQueryPool(s.setup, profile_pool, slot * kProfileQueries + reset.first,
                                    reset.count);
          profile_query_reset_count += reset.count;
        }
      }
    }
    // Everything the setup command buffer uploads is visible to the draws.
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
                            VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    dfn().vkCmdPipelineBarrier(s.main, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0,
                               nullptr);
    recording = true;
    bound_pipeline = VK_NULL_HANDLE;
    bound_layout = VK_NULL_HANDLE;
    bound_static_sets = false;
    std::memset(&bound_viewport, 0, sizeof(bound_viewport));
    std::memset(&bound_scissor, 0, sizeof(bound_scissor));
    bound_depth_bias[0] = bound_depth_bias[1] = NAN;
    bound_offsets_valid = false;
    bound_texture_sets[0] = bound_texture_sets[1] = VK_NULL_HANDLE;
    bound_index_valid = false;
    for (float& f : bound_blend_constants) f = NAN;
    for (uint32_t& v : bound_stencil) v = ~0u;
  }

  void Submit() {
    if (!recording) {
      return;
    }
    DrainRecorder();
    EndPass();
    // The setup command buffer copies staging data the upload worker fills.
    {
      uint64_t start = NowNs();
      textures.WaitForUploads();
      frame_upload_wait_ns += NowNs() - start;
    }
    // Submission boundaries order everything anyway.
    resolve_barrier_pending = false;
    Submission& s = current();
    {
      uint32_t slot = uint32_t(submission_number % kSubmissionSlots);
      if (profile_pool && !profile_segments[slot].empty()) {
        const uint32_t index = uint32_t(profile_segments[slot].size());
        ring_caches::SelectiveQueryReset::Plan reset;
        if (profile_queries[slot].NeedReset(index, &reset)) {
          dfn().vkCmdResetQueryPool(s.main, profile_pool, slot * kProfileQueries + reset.first,
                                    reset.count);
          profile_query_reset_count += reset.count;
        }
        dfn().vkCmdWriteTimestamp(s.main, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, profile_pool,
                                  slot * kProfileQueries + index);
        profile_queries[slot].OnWrite(index);
        profile_query_used[slot] = profile_queries[slot].Used();
        profile_closed[slot] = true;
      }
    }
    if (s.timed) {
      dfn().vkCmdWriteTimestamp(s.main, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps,
                                uint32_t(submission_number % kSubmissionSlots) * 2 + 1);
    }
    dfn().vkEndCommandBuffer(s.setup);
    dfn().vkEndCommandBuffer(s.main);
    VkCommandBuffer buffers[] = {s.setup, s.main};
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 2;
    submit.pCommandBuffers = buffers;
    {
      auto queue = device->AcquireQueue(device->queue_family_graphics_compute(), 0);
      if (dfn().vkQueueSubmit(queue.queue(), 1, &submit, s.fence) != VK_SUCCESS) {
        REXLOG_ERROR("[native] vkQueueSubmit failed");
      }
    }
    s.pending = true;
    recording = false;
    ++stats().submissions;
  }

  // Stream memory for this submission; stalls on older submissions if the
  // ring would run over data they still read.
  // The start of the stream's next lap. Not rex::align: that rounds with a
  // mask, right only for powers of two, and the stream is 160 MB. Wrapping
  // with it put allocations at arbitrary places, far enough ahead that the
  // stream looked full and the renderer waited for the GPU to finish all its
  // work (30-45 ms, every wrap), or across the end of the buffer.
  static uint64_t NextStreamLap(uint64_t position) {
    return (position / kStreamSize + 1) * kStreamSize;
  }

  uint8_t* Allocate(VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& offset_out) {
    if (size > kStreamSize / 4) {
      return nullptr;
    }
    BeginRecording();
    // Allocations stay kStreamGuard short of the end, so a constant binding's
    // fixed range starting at any allocation stays inside the buffer.
    uint64_t start = rex::align(stream_head, uint64_t(alignment));
    if ((start % kStreamSize) + size > kStreamSize - kStreamGuard) {
      start = NextStreamLap(start);
    }
    uint64_t end = start + size;
    // The current submission itself must not wrap onto what it still reads.
    const uint64_t fence_before = frame_fence_wait_ns;
    if (end - current().stream_oldest > kStreamSize) {
      Submit();
      WaitSubmission(submissions[submission_number % kSubmissionSlots]);
      BeginRecording();
      start = rex::align(stream_head, uint64_t(alignment));
      if ((start % kStreamSize) + size > kStreamSize - kStreamGuard) {
        start = NextStreamLap(start);
      }
      end = start + size;
    }
    for (Submission& s : submissions) {
      if (&s != &current() && s.pending && end - s.stream_oldest > kStreamSize) {
        WaitSubmission(s);
      }
    }
    frame_stream_wait_ns += frame_fence_wait_ns - fence_before;
    stream_head = end;
    offset_out = VkDeviceSize(start % kStreamSize);
    return stream_mapping + offset_out;
  }

  VkDescriptorSet AllocatePersistentSet(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    while (true) {
      if (set_pool_used >= set_pools.size()) {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16384},
            {VK_DESCRIPTOR_TYPE_SAMPLER, 16384},
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 4096;
        pool_info.poolSizeCount = 2;
        pool_info.pPoolSizes = sizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (dfn().vkCreateDescriptorPool(vk, &pool_info, nullptr, &pool) != VK_SUCCESS) {
          return VK_NULL_HANDLE;
        }
        set_pools.push_back(pool);
      }
      info.descriptorPool = set_pools[set_pool_used];
      if (dfn().vkAllocateDescriptorSets(vk, &info, &set) == VK_SUCCESS) {
        return set;
      }
      ++set_pool_used;
    }
  }

  // Forgets every cached set; the pools are freed once the GPU is done with
  // the frames that may use them.
  // Bumped when the cached texture sets are dropped: their pools are freed
  // once the GPU is done, so bindings kept from earlier frames must not name
  // them any more.
  uint64_t set_cache_generation = 1;
  void FlushSetCache() {
    ++set_cache_generation;
    texture_set_cache.clear();
    if (!set_pools.empty()) {
      retired_set_pools.push_back({frame, std::move(set_pools)});
      set_pools.clear();
    }
    set_pool_used = 0;
  }

  void FreeRetiredSetPools() {
    for (auto it = retired_set_pools.begin(); it != retired_set_pools.end();) {
      if (it->frame <= completed_frame) {
        for (VkDescriptorPool pool : it->pools) {
          dfn().vkDestroyDescriptorPool(vk, pool, nullptr);
        }
        it = retired_set_pools.erase(it);
      } else {
        ++it;
      }
    }
  }

  VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout) {
    Submission& s = current();
    VkDescriptorSetAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    while (true) {
      if (s.descriptor_pool_used >= s.descriptor_pools.size()) {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16384},
            {VK_DESCRIPTOR_TYPE_SAMPLER, 16384},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256},
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 4096;
        pool_info.poolSizeCount = 3;
        pool_info.pPoolSizes = sizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (dfn().vkCreateDescriptorPool(vk, &pool_info, nullptr, &pool) != VK_SUCCESS) {
          return VK_NULL_HANDLE;
        }
        s.descriptor_pools.push_back(pool);
      }
      info.descriptorPool = s.descriptor_pools[s.descriptor_pool_used];
      if (dfn().vkAllocateDescriptorSets(vk, &info, &set) == VK_SUCCESS) {
        return set;
      }
      ++s.descriptor_pool_used;
    }
  }

  // --- Shaders, layouts and pipelines ----------------------------------------------

  VkDescriptorSetLayout GetTextureSetLayout(bool vertex, uint32_t textures, uint32_t samplers);
  const PipelineLayout* GetPipelineLayout(uint32_t tex_vs, uint32_t samp_vs, uint32_t tex_ps,
                                          uint32_t samp_ps);
  bool EnsureTranslated(VulkanShader::VulkanTranslation* translation, bool* pending_out);
  // T35: VkShaderModule do shader, otimizado pelo spirv-opt quando a cvar está ligada. Criado uma
  // vez por tradução (não por quadro); em falha cai no módulo original do SDK.
  VkShaderModule ShaderModuleFor(VulkanShader::VulkanTranslation* translation);
  void DrainTranslations();
  void TranslationThread();
  std::unique_ptr<SpirvShaderTranslator> worker_translator;
  // 1 = being translated on the worker, 2 = done.
  std::unordered_map<const void*, uint8_t> translation_state;
  std::thread translation_thread;
  std::mutex translation_mutex;
  std::condition_variable translation_cv;
  std::deque<VulkanShader::VulkanTranslation*> translation_jobs;
  std::vector<VulkanShader::VulkanTranslation*> translations_done;
  std::atomic<uint32_t> translations_done_count{0};
  bool translation_stop = false;
  // T35: módulos do spirv-opt por tradução (a tradução do SDK fica intacta para o fallback).
  std::mutex optimized_modules_mutex;
  std::unordered_map<VulkanShader::VulkanTranslation*, VkShaderModule> optimized_shader_modules;
  std::mutex spirv_opt_stats_mutex;
  spirv_opt::Stats spirv_opt_stats;
  shader_simplify::Stats shader_simplify_stats;
  std::atomic<uint32_t> spirv_opt_fallbacks_logged{0};
  std::atomic<uint32_t> shader_simplify_fallbacks_logged{0};
  struct PipelineJob {
    uint64_t hash = 0;
    PipelineKey key = {};
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule pixel = VK_NULL_HANDLE;
    VkShaderModule geometry = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
  };
  VkPipeline GetPipeline(const PipelineKey& key, VulkanShader::VulkanTranslation* vs,
                         VulkanShader::VulkanTranslation* ps, VkShaderModule geometry,
                         VkPipelineLayout layout, bool* pending_out);
  VkPipeline CreatePipeline(const PipelineJob& job);
  void DrainCompletedPipelines();
  void PipelineThread();
  std::unordered_set<uint64_t> pending_pipelines;
  std::thread pipeline_thread;
  std::mutex pipeline_mutex;
  std::condition_variable pipeline_cv;
  std::deque<PipelineJob> pipeline_jobs;
  std::vector<std::pair<uint64_t, VkPipeline>> pipelines_completed;
  std::atomic<uint32_t> pipelines_completed_count{0};
  bool pipeline_stop = false;

  // F2.5: as chaves que já viraram pipeline, com o microcódigo dos shaders, para o prewarm da
  // próxima execução. O arquivo fica ao lado do cache do driver e tem a mesma versão dele.
  std::mutex prewarm_mutex;
  std::unordered_map<uint64_t, prewarm::Entry> prewarm_entries;
  prewarm::Stats prewarm_stats;
  std::filesystem::path prewarm_path;
  size_t prewarm_saved_size = 0;
  size_t prewarm_saved_count = 0;
  std::atomic<size_t> prewarm_entry_count{0};
  std::chrono::steady_clock::time_point last_prewarm_save;
  std::atomic<bool> prewarm_writing{false};
  std::thread prewarm_writer;
  uint32_t pipelines_at_prewarm = 0;
  void RecordPipeline(uint64_t hash, const PipelineKey& key,
                      VulkanShader::VulkanTranslation* vs, VulkanShader::VulkanTranslation* ps);
  bool PrewarmPipelines(uint32_t version);
  void SavePrewarmManifest();

  // --- Render targets ------------------------------------------------------------

  RenderTarget* GetRenderTarget(const RenderTargetKey& key, uint32_t min_height);
  RenderTarget* CreateOrGetRenderTarget(const RenderTargetKey& key, uint32_t min_height);
  // T15: guest MSAA surfaces drawn with one sample (no samples as pixels, no host MSAA) keep their own
  // image; the game then reads that EDRAM as the 1x target at pitch * 2, which SyncAliasTarget fills.
  uint32_t AliasMsaa(uint32_t guest_msaa) const {
    return (!msaa_as_samples && !multisample && guest_msaa >= 1) ? guest_msaa : 0;
  }
  uint64_t alias_clock = 0;
  void SyncAliasTarget(RenderTarget* target);
  // F3.3: resolve de profundidade sem cópia. O alvo inteiro passa a imagem ao destino e ganha uma
  // imagem nova; o conteúdo volta por cópia antes de o alvo ser escrito sem clear.
  bool CreateDepthImage(RenderTarget* rt);
  bool HandOffDepthImage(RenderTarget* rt, uint32_t address);
  void RestoreTargetContent(RenderTarget* rt);
  ownership::Stats resolve_ownership;
  void SyncSceneTarget(RenderTarget* target);
  void BlitTarget(RenderTarget* source, RenderTarget* target, int32_t src_w, int32_t src_h,
                  int32_t dst_w, int32_t dst_h, VkFilter filter);
  // T32: CAS no lugar do blit LINEAR ao subir a cena escalada; false = usa o blit.
  bool EnsureSceneCas(VkFormat format);
  bool CasUpscale(RenderTarget* source, RenderTarget* target, const scene::SceneBlit& extent);
  bool EnsureSceneEasu(VkFormat format);
  bool EasuUpscale(RenderTarget* source, RenderTarget* target, const scene::SceneBlit& extent);
  void EndPass() {
    if (in_pass) {
      dfn().vkCmdEndRendering(cmd());
      in_pass = false;
    }
  }
  bool BeginPass(const PassTargets& targets, uint32_t wanted_height = UINT32_MAX);

  // --- Draws ---------------------------------------------------------------------

  void Draw(const uint32_t* immediate_indices, uint32_t immediate_index_words);
  void UpdateSystemConstants(bool primitive_polygonal, const draw_util::ViewportInfo& viewport,
                             uint32_t used_texture_mask, uint32_t vertex_index_endian_flags,
                             const VulkanShader* vs, const VulkanShader* ps);
  void Resolve();
  bool resolve_barrier_pending = false;
  void FlushResolveBarrier();
  void Swap(rex::ui::Presenter* presenter, uint32_t frontbuffer, uint32_t width, uint32_t height);
  void SaveDriverCache();
};

// --- Setup -------------------------------------------------------------------------

bool Renderer::Impl::Initialize() {
  device = provider->vulkan_device();
  vk = device->device();
  auto get_proc = device->vulkan_instance()->functions().vkGetDeviceProcAddr;
  vkCmdClearDepthStencilImage = reinterpret_cast<PFN_vkCmdClearDepthStencilImage>(
      get_proc(vk, "vkCmdClearDepthStencilImage"));
  vkCmdResolveImage =
      reinterpret_cast<PFN_vkCmdResolveImage>(get_proc(vk, "vkCmdResolveImage"));
  vkCmdBlitImage = reinterpret_cast<PFN_vkCmdBlitImage>(get_proc(vk, "vkCmdBlitImage"));
  vkDeviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(get_proc(vk, "vkDeviceWaitIdle"));
  if (!vkCmdClearDepthStencilImage || !vkCmdResolveImage || !vkCmdBlitImage || !vkDeviceWaitIdle) {
    REXLOG_ERROR("[native] Missing Vulkan device functions");
    return false;
  }
  const auto& properties = device->properties();
  if (!properties.dynamicRendering) {
    REXLOG_ERROR("[native] The native renderer needs dynamic rendering");
    return false;
  }
  specialize_signs = REXCVAR_GET(gpu_specialize_texture_signs);
  multisample = REXCVAR_GET(gh3_native_msaa);
  msaa_as_samples = REXCVAR_GET(gh3_native_msaa_as_samples);
  scene_scale = scene::ClampScale(float(REXCVAR_GET(gh3_native_scene_scale)));
  const bool dynamic_usable = scene::DynamicUsable(REXCVAR_GET(gh3_min_frame_vblanks));
  if (REXCVAR_GET(gh3_native_scene_dynamic) && !dynamic_usable) {
    REXLOG_WARN("[native] scene dynamic off: frame cap (gh3_min_frame_vblanks = {}) hides the headroom; "
                "fixed scene scale {:g}",
                REXCVAR_GET(gh3_min_frame_vblanks), scene_scale);
  }
  if (REXCVAR_GET(gh3_native_scene_dynamic) && dynamic_usable) {
    // T31: o piso do controlador é 0.65; a escala pedida só define por onde ele começa.
    scene_dynamic = std::make_unique<scene::DynamicScale>(scene::DynamicConfig(), scene_scale);
    scene_scale = float(scene_dynamic->scale());
    const scene::DynamicConfig& c = scene_dynamic->config();
    REXLOG_INFO("[native] scene dynamic: enabled {:.2f}-{:.2f}, target {:.1f} ms, step {:.2f}, "
                "hold {:.2f} s, start {:g}",
                double(c.min_level) / 20.0, double(c.max_level) / 20.0, c.target_ms,
                double(c.step_levels) / 20.0, c.hold_s, scene_scale);
  }
  scene_cas_sharpness = scene::ClampCasSharpness(float(REXCVAR_GET(gh3_native_scene_cas_sharpness)));
  scene_cas = scene::CasEnabled(REXCVAR_GET(gh3_native_scene_cas), scene_cas_sharpness);
  if (scene_cas) {
    REXLOG_INFO("[native] scene CAS: enabled (sharpness {:.2f}, setup {:g}) on the upscale of the "
                "scaled scene; LINEAR elsewhere",
                scene_cas_sharpness, scene::CasSetupSharpness(scene_cas_sharpness));
    if (!scene::Enabled(scene_scale)) {
      REXLOG_WARN("[native] scene CAS: has no effect while gh3_native_scene_scale is 1.0");
    }
  }
  // T47: o upscaler do blit da cena escalada. O bool gh3_native_scene_cas do T32 ainda seleciona o
  // CAS quando gh3_native_scene_upscale é linear (compatibilidade da config antiga).
  scene_upscale_mode = scene::ParseUpscaleMode(REXCVAR_GET(gh3_native_scene_upscale));
  if (scene_upscale_mode == scene::UpscaleMode::kLinear && scene_cas) {
    scene_upscale_mode = scene::UpscaleMode::kCas;
  }
  REXLOG_INFO("[native] scene upscale: {} on the upscale of the scaled scene (scale < 1, opaque "
              "R8G8B8A8); LINEAR elsewhere",
              std::string(scene::UpscaleModeName(scene_upscale_mode)));
  if (scene_upscale_mode != scene::UpscaleMode::kLinear && !scene::Enabled(scene_scale)) {
    REXLOG_WARN("[native] scene upscale: has no effect while gh3_native_scene_scale is 1.0");
  }
  alias_cap = REXCVAR_GET(gh3_native_alias_cap);
  zcull = REXCVAR_GET(gh3_native_zcull);
  ubo_alignment = std::max<VkDeviceSize>(properties.minUniformBufferOffsetAlignment, 16);

  translator = std::make_unique<SpirvShaderTranslator>(SpirvShaderTranslator::Features(device),
                                                       true, true, false);

  VkShaderStageFlags guest_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                    (properties.geometryShader ? VK_SHADER_STAGE_GEOMETRY_BIT : 0);

  VkDescriptorSetLayoutCreateInfo layout_info = {};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  if (dfn().vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &layout_empty) != VK_SUCCESS) {
    return false;
  }
  // Set 0: the translator's shared memory, here the stream. With a small
  // maxStorageBufferRange the translator splits it into several bindings.
  shared_binding_count = 1u << SpirvShaderTranslator::GetSharedMemoryStorageBufferCountLog2(
                             properties.maxStorageBufferRange);
  VkDescriptorSetLayoutBinding shared_binding = {};
  shared_binding.binding = 0;
  shared_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  shared_binding.descriptorCount = shared_binding_count;
  shared_binding.stageFlags = guest_stages;
  layout_info.bindingCount = 1;
  layout_info.pBindings = &shared_binding;
  if (dfn().vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &layout_shared) != VK_SUCCESS) {
    return false;
  }
  // Set 1: the five constant buffers, dynamic so one set serves every draw.
  VkDescriptorSetLayoutBinding constant_bindings[SpirvShaderTranslator::kConstantBufferCount] = {};
  for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
    constant_bindings[i].binding = i;
    constant_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    constant_bindings[i].descriptorCount = 1;
    constant_bindings[i].stageFlags = guest_stages;
  }
  constant_bindings[SpirvShaderTranslator::kConstantBufferFloatVertex].stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT;
  constant_bindings[SpirvShaderTranslator::kConstantBufferFloatPixel].stageFlags =
      VK_SHADER_STAGE_FRAGMENT_BIT;
  layout_info.bindingCount = SpirvShaderTranslator::kConstantBufferCount;
  layout_info.pBindings = constant_bindings;
  if (dfn().vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &layout_constants) !=
      VK_SUCCESS) {
    return false;
  }

  // The stream.
  VkDeviceSize stream_allocation = 0;
  uint32_t stream_memory_type = 0;
  if (!rex::ui::vulkan::util::CreateDedicatedAllocationBuffer(
          device, kStreamSize + kStreamSlack,
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          rex::ui::vulkan::util::MemoryPurpose::kUpload, stream_buffer, stream_memory,
          &stream_memory_type, &stream_allocation)) {
    REXLOG_ERROR("[native] Could not create the {} MB stream buffer", kStreamSize >> 20);
    return false;
  }
  if (!(device->memory_types().host_coherent & (1u << stream_memory_type))) {
    REXLOG_WARN("[native] The stream memory is not host-coherent");
  }
  void* mapping = nullptr;
  if (dfn().vkMapMemory(vk, stream_memory, 0, VK_WHOLE_SIZE, 0, &mapping) != VK_SUCCESS) {
    return false;
  }
  stream_mapping = static_cast<uint8_t*>(mapping);

  VkDescriptorPoolSize static_sizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, SpirvShaderTranslator::kConstantBufferCount},
  };
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 2;
  pool_info.poolSizeCount = 2;
  pool_info.pPoolSizes = static_sizes;
  if (dfn().vkCreateDescriptorPool(vk, &pool_info, nullptr, &static_pool) != VK_SUCCESS) {
    return false;
  }
  VkDescriptorSetLayout static_layouts[] = {layout_shared, layout_constants};
  VkDescriptorSet static_sets[2];
  VkDescriptorSetAllocateInfo allocate = {};
  allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocate.descriptorPool = static_pool;
  allocate.descriptorSetCount = 2;
  allocate.pSetLayouts = static_layouts;
  if (dfn().vkAllocateDescriptorSets(vk, &allocate, static_sets) != VK_SUCCESS) {
    return false;
  }
  set_shared = static_sets[0];
  set_constants = static_sets[1];
  // With several shared memory bindings each covers a slice of guest memory;
  // the stream is far smaller than the first slice, so all of them can point at
  // the whole stream.
  std::vector<VkDescriptorBufferInfo> shared_infos(shared_binding_count);
  VkDeviceSize slice = VkDeviceSize(512u << 20) / shared_binding_count;
  for (uint32_t i = 0; i < shared_binding_count; ++i) {
    // Binding i serves the addresses from i * slice, so it starts there in
    // the stream (clamped for the slices past the end of the stream, which
    // are never addressed).
    VkDeviceSize offset = std::min(VkDeviceSize(i) * slice, kStreamSize - 256);
    offset &= ~(properties.minStorageBufferOffsetAlignment - 1);
    shared_infos[i].buffer = stream_buffer;
    shared_infos[i].offset = offset;
    shared_infos[i].range = kStreamSize + kStreamSlack - offset;
  }
  VkDescriptorBufferInfo constant_infos[SpirvShaderTranslator::kConstantBufferCount];
  VkDeviceSize constant_ranges[] = {sizeof(SpirvShaderTranslator::SystemConstants),
                                    kFloatConstantsBytes, kFloatConstantsBytes,
                                    kBoolLoopConstantsBytes, kFetchConstantsBytes};
  for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
    constant_infos[i].buffer = stream_buffer;
    constant_infos[i].offset = 0;
    constant_infos[i].range = constant_ranges[i];
  }
  VkWriteDescriptorSet writes[2] = {};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = set_shared;
  writes[0].descriptorCount = shared_binding_count;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[0].pBufferInfo = shared_infos.data();
  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = set_constants;
  writes[1].descriptorCount = SpirvShaderTranslator::kConstantBufferCount;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  writes[1].pBufferInfo = constant_infos;
  dfn().vkUpdateDescriptorSets(vk, 2, writes, 0, nullptr);

  // Submissions.
  for (Submission& s : submissions) {
    VkCommandPoolCreateInfo command_pool = {};
    command_pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    command_pool.queueFamilyIndex = device->queue_family_graphics_compute();
    if (dfn().vkCreateCommandPool(vk, &command_pool, nullptr, &s.pool) != VK_SUCCESS) {
      return false;
    }
    if (dfn().vkCreateCommandPool(vk, &command_pool, nullptr, &s.main_pool) != VK_SUCCESS) {
      return false;
    }
    VkCommandBufferAllocateInfo buffers = {};
    buffers.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    buffers.commandPool = s.pool;
    buffers.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    buffers.commandBufferCount = 1;
    if (dfn().vkAllocateCommandBuffers(vk, &buffers, &s.setup) != VK_SUCCESS) {
      return false;
    }
    buffers.commandPool = s.main_pool;
    if (dfn().vkAllocateCommandBuffers(vk, &buffers, &s.main) != VK_SUCCESS) {
      return false;
    }
    VkFenceCreateInfo fence = {};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (dfn().vkCreateFence(vk, &fence, nullptr, &s.fence) != VK_SUCCESS) {
      return false;
    }
  }

  VkQueryPoolCreateInfo query_info = {};
  query_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
  query_info.queryCount = kSubmissionSlots * 2;
  if (properties.timestampPeriod > 0.0f &&
      dfn().vkCreateQueryPool(vk, &query_info, nullptr, &timestamps) == VK_SUCCESS) {
    timestamp_period = properties.timestampPeriod;
  } else {
    timestamps = VK_NULL_HANDLE;
  }

  VkPipelineCacheCreateInfo cache_info = {};
  cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  dfn().vkCreatePipelineCache(vk, &cache_info, nullptr, &driver_cache);

  if (!textures.Initialize(device, memory)) {
    return false;
  }
  upload.buffer = stream_buffer;
  upload.allocate = [this](size_t size, size_t alignment, VkDeviceSize& offset) {
    uint8_t* result = Allocate(size, alignment, offset);
    // An allocation may have started a new submission.
    upload.setup_commands = current().setup;
    upload.submission = submission_number;
    return result;
  };
  if (!CreateSwapResources()) {
    return false;
  }
  pipeline_thread = std::thread([this]() { PipelineThread(); });
  if (REXCVAR_GET(gh3_native_record_thread)) {
    record_queue = std::make_unique<DrawCommand[]>(kRecordQueueSize);
    record_thread = std::thread([this]() { RecordThread(); });
    record_threaded = true;
  }
  worker_translator = std::make_unique<SpirvShaderTranslator>(
      SpirvShaderTranslator::Features(device), true, true, false);
  translation_thread = std::thread([this]() { TranslationThread(); });
  if (!rex::ui::vulkan::util::CreateDedicatedAllocationBuffer(
          device, kDepthScratchSize,
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          rex::ui::vulkan::util::MemoryPurpose::kDeviceLocal, depth_scratch,
          depth_scratch_memory)) {
    depth_scratch = VK_NULL_HANDLE;
    REXLOG_WARN("[native] No scratch buffer for depth resolves");
  }
  if (depth_scratch && !CreateDepthConversion()) {
    REXLOG_WARN("[native] No depth resolve pipeline; depth resolves are not done");
    if (depth_convert_pipeline) dfn().vkDestroyPipeline(vk, depth_convert_pipeline, nullptr);
    depth_convert_pipeline = VK_NULL_HANDLE;
  }
  if (copy_color_pipeline) {
    textures.SetComputeCopy([this](VkCommandBuffer c, VkImageView source, VkImageView dest,
                                   int32_t sx, int32_t sy, int32_t dx, int32_t dy, uint32_t width,
                                   uint32_t height) {
      return ComputeColorCopy(c, source, dest, sx, sy, dx, dy, width, height);
    });
  }
  if (REXCVAR_GET(gh3_native_depth24)) {
    VkFormatProperties format_properties = {};
    device->vulkan_instance()->functions().vkGetPhysicalDeviceFormatProperties(
        device->physical_device(), VK_FORMAT_D24_UNORM_S8_UINT, &format_properties);
    if ((format_properties.optimalTilingFeatures &
         (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) ==
            (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) &&
        depth_scratch && depth_convert_pipeline) {
      depth24 = true;
    } else {
      REXLOG_WARN("[native] 24-bit depth buffers unavailable; using 32-bit float");
    }
  }
  if (REXCVAR_GET(gh3_native_gpu_profile) && timestamps) {
    VkQueryPoolCreateInfo profile_info = {};
    profile_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    profile_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    profile_info.queryCount = kSubmissionSlots * kProfileQueries;
    if (dfn().vkCreateQueryPool(vk, &profile_info, nullptr, &profile_pool) != VK_SUCCESS) {
      profile_pool = VK_NULL_HANDLE;
    }
  }
  // The default linear 256-entry gamma ramp (CommandProcessor::Initialize).
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t value = i * 0x3FF / 0xFF;
    gamma_ramp[i] = value | (value << 10) | (value << 20);
  }
  REXLOG_INFO("[native] Renderer ready: {} MB stream, {} shared memory binding(s)",
              kStreamSize >> 20, shared_binding_count);
  // O anel deste renderer é o de WORK slots (um conjunto de fence, pool e descriptor pool por
  // slot); não confundir com o anel de saída do presenter, logado abaixo.
  REXLOG_INFO("[native] work ring: {} slots", kSubmissionSlots);
  // F3.2: o anel de saída (present) é o do presenter do SDK — 3 imagens de guest output e 3
  // submissões de paint, um submit por quadro, esperando só o slot reutilizado. O T39 mediu a
  // espera (linha "present fence wait"); no Mac ela não justifica nada, no Switch fica para a F5.
  REXLOG_INFO("[native] {}",
              present_ring::Format(present_ring::kOutputSlots,
                                   uint32_t(rex::ui::vulkan::VulkanPresenter::
                                                kMaxActiveGuestOutputImageVersions),
                                   kSubmissionSlots,
                                   REXCVAR_GET(vulkan_allow_present_mode_immediate)));
  return true;
}

bool Renderer::Impl::CreateDepthConversion() {
  VkDescriptorSetLayoutBinding binding = {};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo layout_info = {};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = 1;
  layout_info.pBindings = &binding;
  if (dfn().vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &depth_convert_set_layout) !=
      VK_SUCCESS) {
    return false;
  }
  VkPushConstantRange push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 5};
  VkPipelineLayoutCreateInfo pipeline_layout = {};
  pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout.setLayoutCount = 1;
  pipeline_layout.pSetLayouts = &depth_convert_set_layout;
  pipeline_layout.pushConstantRangeCount = 1;
  pipeline_layout.pPushConstantRanges = &push;
  if (dfn().vkCreatePipelineLayout(vk, &pipeline_layout, nullptr, &depth_convert_layout) !=
      VK_SUCCESS) {
    return false;
  }
  depth_convert_pipeline = rex::ui::vulkan::util::CreateComputePipeline(
      device, depth_convert_layout, shaders::kDepthResolveCs, sizeof(shaders::kDepthResolveCs));
  if (!depth_convert_pipeline) {
    return false;
  }
  VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &size;
  if (dfn().vkCreateDescriptorPool(vk, &pool_info, nullptr, &depth_convert_pool) != VK_SUCCESS) {
    return false;
  }
  VkDescriptorSetAllocateInfo allocate = {};
  allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocate.descriptorPool = depth_convert_pool;
  allocate.descriptorSetCount = 1;
  allocate.pSetLayouts = &depth_convert_set_layout;
  if (dfn().vkAllocateDescriptorSets(vk, &allocate, &depth_convert_set) != VK_SUCCESS) {
    return false;
  }
  VkDescriptorBufferInfo buffer = {depth_scratch, 0, kDepthScratchSize};
  VkWriteDescriptorSet write = {};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = depth_convert_set;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write.pBufferInfo = &buffer;
  dfn().vkUpdateDescriptorSets(vk, 1, &write, 0, nullptr);

  VkDescriptorSetLayoutBinding copy_bindings[2] = {};
  copy_bindings[0].binding = 0;
  copy_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  copy_bindings[0].descriptorCount = 1;
  copy_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  copy_bindings[1] = copy_bindings[0];
  copy_bindings[1].binding = 1;
  copy_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  layout_info.bindingCount = 2;
  layout_info.pBindings = copy_bindings;
  VkPushConstantRange copy_push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 4};
  pipeline_layout.pSetLayouts = &depth_copy_set_layout;
  pipeline_layout.pPushConstantRanges = &copy_push;
  if (dfn().vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &depth_copy_set_layout) ==
          VK_SUCCESS &&
      dfn().vkCreatePipelineLayout(vk, &pipeline_layout, nullptr, &depth_copy_layout) ==
          VK_SUCCESS) {
    depth_copy_pipeline = rex::ui::vulkan::util::CreateComputePipeline(
        device, depth_copy_layout, shaders::kDepthToFloatCs, sizeof(shaders::kDepthToFloatCs));
  }
  if (!depth_copy_pipeline) {
    REXLOG_WARN("[native] No direct depth resolve; resolving depth through a buffer");
  }

  VkPhysicalDeviceFeatures features = {};
  device->vulkan_instance()->functions().vkGetPhysicalDeviceFeatures(device->physical_device(),
                                                                     &features);
  if (depth_copy_set_layout && features.shaderStorageImageWriteWithoutFormat) {
    VkPushConstantRange color_push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int32_t) * 6};
    pipeline_layout.pSetLayouts = &depth_copy_set_layout;
    pipeline_layout.pPushConstantRanges = &color_push;
    if (dfn().vkCreatePipelineLayout(vk, &pipeline_layout, nullptr, &copy_color_layout) ==
        VK_SUCCESS) {
      copy_color_pipeline = rex::ui::vulkan::util::CreateComputePipeline(
          device, copy_color_layout, shaders::kCopyColorCs, sizeof(shaders::kCopyColorCs));
    }
  }
  if (!copy_color_pipeline) {
    REXLOG_WARN("[native] No compute color copy; resolving color with vkCmdCopyImage");
  }
  return true;
}

bool Renderer::Impl::ComputeColorCopy(VkCommandBuffer c, VkImageView source, VkImageView dest,
                                      int32_t sx, int32_t sy, int32_t dx, int32_t dy,
                                      uint32_t width, uint32_t height) {
  if (!copy_color_pipeline || !width || !height) return false;
  VkDescriptorSet set = AllocateSet(depth_copy_set_layout);
  if (!set) return false;
  VkDescriptorImageInfo images[2] = {{VK_NULL_HANDLE, source, VK_IMAGE_LAYOUT_GENERAL},
                                     {VK_NULL_HANDLE, dest, VK_IMAGE_LAYOUT_GENERAL}};
  VkWriteDescriptorSet writes[2] = {};
  for (uint32_t i = 0; i < 2; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = set;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType =
        i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[i].pImageInfo = &images[i];
  }
  dfn().vkUpdateDescriptorSets(vk, 2, writes, 0, nullptr);
  // Earlier writes of the source (rendering, copies, compute) before the
  // reads, and earlier reads of the destination before the writes.
  VkMemoryBarrier before = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                                VK_ACCESS_SHADER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(
      c,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
          VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
  int32_t params[6] = {sx, sy, dx, dy, int32_t(width), int32_t(height)};
  dfn().vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, copy_color_pipeline);
  dfn().vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_COMPUTE, copy_color_layout, 0, 1, &set,
                                0, nullptr);
  dfn().vkCmdPushConstants(c, copy_color_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params),
                           params);
  dfn().vkCmdDispatch(c, (width + 7) / 8, (height + 7) / 8, 1);
  VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                           VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(
      c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
          VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      0, 1, &after, 0, nullptr, 0, nullptr);
  return true;
}

void Renderer::Impl::ProfileCollect(uint32_t slot) {
  auto& segments = profile_segments[slot];
  if (!profile_pool || !profile_closed[slot] || segments.empty()) {
    segments.clear();
    return;
  }
  profile_closed[slot] = false;
  std::vector<uint64_t> ticks(segments.size() + 1);
  if (dfn().vkGetQueryPoolResults(vk, profile_pool, slot * kProfileQueries, uint32_t(ticks.size()),
                                  ticks.size() * sizeof(uint64_t), ticks.data(), sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
    for (size_t i = 0; i < segments.size(); ++i) {
      if (ticks[i + 1] < ticks[i]) continue;
      double ns = double(ticks[i + 1] - ticks[i]) * timestamp_period;
      ProfileTotal& total = profile_totals[segments[i].key];
      total.ns += ns;
      ++total.count;
      total.draws += segments[i].draws;
      profiled_ns += ns;
    }
  }
  segments.clear();
}

void Renderer::Impl::ProfileReport() {
  if (!profile_pool || !profile_frame) return;
  // One profiled frame in 16; report about every 10 seconds at 30 fps.
  if (++profiled_frames < 20) return;
  std::vector<std::pair<uint64_t, ProfileTotal>> sorted(profile_totals.begin(),
                                                        profile_totals.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second.ns > b.second.ns; });
  double frames = double(profiled_frames);
  std::string line = fmt::format("[native] GPU by pass, ms per frame (sampled, as the driver "
                                 "reports it): total {:.1f}",
                                 profiled_ns / 1e6 / frames);
  auto target = [](uint64_t bits) -> std::string {
    if (bits == 0xFFFFFF) return "-";
    return fmt::format("t{}p{}{}", (bits >> 12) & 0xFFF, (bits >> 1) & 0x7FF,
                       (bits & 1) ? "f" : "");
  };
  for (size_t i = 0; i < sorted.size() && i < 14; ++i) {
    uint64_t key = sorted[i].first;
    const ProfileTotal& t = sorted[i].second;
    std::string what;
    switch (key >> 60) {
      case kProfilePass:
        what = fmt::format("pass h{} c={} d={}", (key >> 48) & 0xFFF,
                           target((key >> 24) & 0xFFFFFF), target(key & 0xFFFFFF));
        break;
      case kProfileResolve:
        what = fmt::format("resolve{} p{} {}x{}", ((key >> 56) & 1) ? " depth" : "",
                           (key >> 32) & 0x7FF, (key >> 16) & 0xFFFF, key & 0xFFFF);
        break;
      default:
        what = "other";
        break;
    }
    line += fmt::format(" | {}: {:.2f} ({:.1f}x, {:.0f} draws)", what, t.ns / 1e6 / frames,
                        double(t.count) / frames, double(t.draws) / frames);
  }
  REXLOG_INFO("{}", line);
  profile_totals.clear();
  profiled_frames = 0;
  profiled_ns = 0;
}

bool Renderer::Impl::CreateSwapResources() {
  // The same layout as VulkanCommandProcessor's apply-gamma compute pipeline:
  // set 0 the ramp (uniform texel buffer), set 1 the source (sampled image),
  // set 2 the destination (storage image), and the size as a push constant.
  VkDescriptorSetLayoutBinding binding = {};
  binding.binding = 0;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo layout_info = {};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = 1;
  layout_info.pBindings = &binding;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
  if (dfn().vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &swap_layout_ramp) !=
      VK_SUCCESS) {
    return false;
  }
  binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  if (dfn().vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &swap_layout_source) !=
      VK_SUCCESS) {
    return false;
  }
  binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  if (dfn().vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &swap_layout_dest) !=
      VK_SUCCESS) {
    return false;
  }
  VkDescriptorSetLayout set_layouts[] = {swap_layout_ramp, swap_layout_source, swap_layout_dest};
  VkPushConstantRange push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 2};
  VkPipelineLayoutCreateInfo pipeline_layout = {};
  pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout.setLayoutCount = 3;
  pipeline_layout.pSetLayouts = set_layouts;
  pipeline_layout.pushConstantRangeCount = 1;
  pipeline_layout.pPushConstantRanges = &push;
  if (dfn().vkCreatePipelineLayout(vk, &pipeline_layout, nullptr, &swap_pipeline_layout) !=
      VK_SUCCESS) {
    return false;
  }
  swap_pipeline = rex::ui::vulkan::util::CreateComputePipeline(
      device, swap_pipeline_layout, shaders::apply_gamma_table_cs,
      sizeof(shaders::apply_gamma_table_cs));
  if (swap_pipeline == VK_NULL_HANDLE) {
    REXLOG_ERROR("[native] Could not create the swap pipeline");
    return false;
  }

  VkDeviceSize ramp_size = 256 * sizeof(uint32_t) * kSubmissionSlots;
  if (!rex::ui::vulkan::util::CreateDedicatedAllocationBuffer(
          device, ramp_size, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
          rex::ui::vulkan::util::MemoryPurpose::kUpload, ramp_buffer, ramp_memory)) {
    return false;
  }
  void* mapping = nullptr;
  dfn().vkMapMemory(vk, ramp_memory, 0, VK_WHOLE_SIZE, 0, &mapping);
  ramp_mapping = static_cast<uint8_t*>(mapping);

  VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, kSubmissionSlots},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSubmissionSlots},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSubmissionSlots},
  };
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 3 * kSubmissionSlots;
  pool_info.poolSizeCount = 3;
  pool_info.pPoolSizes = sizes;
  if (dfn().vkCreateDescriptorPool(vk, &pool_info, nullptr, &swap_pool) != VK_SUCCESS) {
    return false;
  }
  for (uint32_t i = 0; i < kSubmissionSlots; ++i) {
    VkDescriptorSetAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = swap_pool;
    allocate.descriptorSetCount = 3;
    allocate.pSetLayouts = set_layouts;
    if (dfn().vkAllocateDescriptorSets(vk, &allocate, swap_sets[i]) != VK_SUCCESS) {
      return false;
    }
    VkBufferViewCreateInfo view = {};
    view.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    view.buffer = ramp_buffer;
    view.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    view.offset = 256 * sizeof(uint32_t) * i;
    view.range = 256 * sizeof(uint32_t);
    if (dfn().vkCreateBufferView(vk, &view, nullptr, &ramp_views[i]) != VK_SUCCESS) {
      return false;
    }
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = swap_sets[i][0];
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    write.pTexelBufferView = &ramp_views[i];
    dfn().vkUpdateDescriptorSets(vk, 1, &write, 0, nullptr);
  }
  return true;
}

void Renderer::Impl::Shutdown() {
  if (!device) {
    return;
  }
  if (record_thread.joinable()) {
    DrainRecorder();
    record_stop.store(true, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lock(record_mutex);
      record_cv.notify_all();
    }
    record_thread.join();
    record_threaded = false;
  }
  if (driver_cache_writer.joinable()) {
    driver_cache_writer.join();
  }
  SavePrewarmManifest();
  if (prewarm_writer.joinable()) {
    prewarm_writer.join();
  }
  if (translation_thread.joinable()) {
    {
      std::lock_guard<std::mutex> lock(translation_mutex);
      translation_stop = true;
    }
    translation_cv.notify_all();
    translation_thread.join();
  }
  if (pipeline_thread.joinable()) {
    {
      std::lock_guard<std::mutex> lock(pipeline_mutex);
      pipeline_stop = true;
    }
    pipeline_cv.notify_all();
    pipeline_thread.join();
  }
  DrainCompletedPipelines();
  Submit();
  for (Submission& s : submissions) {
    WaitSubmission(s);
  }
  vkDeviceWaitIdle(vk);
  textures.Shutdown();
  for (auto& [key, rt] : render_targets) {
    if (rt->sample_view) dfn().vkDestroyImageView(vk, rt->sample_view, nullptr);
    if (rt->view) dfn().vkDestroyImageView(vk, rt->view, nullptr);
    if (rt->image) dfn().vkDestroyImage(vk, rt->image, nullptr);
    if (rt->memory) dfn().vkFreeMemory(vk, rt->memory, nullptr);
  }
  render_targets.clear();
  for (auto& retired : retired_render_targets) {
    if (retired.sample_view) dfn().vkDestroyImageView(vk, retired.sample_view, nullptr);
    dfn().vkDestroyImageView(vk, retired.view, nullptr);
    dfn().vkDestroyImage(vk, retired.image, nullptr);
    dfn().vkFreeMemory(vk, retired.memory, nullptr);
  }
  retired_render_targets.clear();
  for (SceneCasPipeline& cached : scene_cas_pipelines) {
    if (cached.pipeline) dfn().vkDestroyPipeline(vk, cached.pipeline, nullptr);
  }
  scene_cas_pipelines.clear();
  if (scene_cas_layout) dfn().vkDestroyPipelineLayout(vk, scene_cas_layout, nullptr);
  if (scene_cas_set_layout) {
    dfn().vkDestroyDescriptorSetLayout(vk, scene_cas_set_layout, nullptr);
  }
  for (SceneEasuPipeline& cached : scene_easu_pipelines) {
    if (cached.pipeline) dfn().vkDestroyPipeline(vk, cached.pipeline, nullptr);
  }
  scene_easu_pipelines.clear();
  if (scene_easu_layout) dfn().vkDestroyPipelineLayout(vk, scene_easu_layout, nullptr);
  if (scene_easu_set_layout) {
    dfn().vkDestroyDescriptorSetLayout(vk, scene_easu_set_layout, nullptr);
  }
  if (scene_easu_sampler) dfn().vkDestroySampler(vk, scene_easu_sampler, nullptr);
  if (depth_convert_pipeline) dfn().vkDestroyPipeline(vk, depth_convert_pipeline, nullptr);
  if (depth_convert_layout) dfn().vkDestroyPipelineLayout(vk, depth_convert_layout, nullptr);
  if (depth_copy_pipeline) dfn().vkDestroyPipeline(vk, depth_copy_pipeline, nullptr);
  if (copy_color_pipeline) dfn().vkDestroyPipeline(vk, copy_color_pipeline, nullptr);
  if (copy_color_layout) dfn().vkDestroyPipelineLayout(vk, copy_color_layout, nullptr);
  if (depth_copy_layout) dfn().vkDestroyPipelineLayout(vk, depth_copy_layout, nullptr);
  if (depth_copy_set_layout) {
    dfn().vkDestroyDescriptorSetLayout(vk, depth_copy_set_layout, nullptr);
  }
  if (depth_convert_pool) dfn().vkDestroyDescriptorPool(vk, depth_convert_pool, nullptr);
  if (depth_convert_set_layout) {
    dfn().vkDestroyDescriptorSetLayout(vk, depth_convert_set_layout, nullptr);
  }
  if (profile_pool) dfn().vkDestroyQueryPool(vk, profile_pool, nullptr);
  if (depth_scratch) dfn().vkDestroyBuffer(vk, depth_scratch, nullptr);
  if (depth_scratch_memory) dfn().vkFreeMemory(vk, depth_scratch_memory, nullptr);
  for (auto& [key, pipeline] : pipelines) {
    if (pipeline) dfn().vkDestroyPipeline(vk, pipeline, nullptr);
  }
  pipelines.clear();
  for (auto& [key, module] : geometry_shaders) {
    if (module) dfn().vkDestroyShaderModule(vk, module, nullptr);
  }
  geometry_shaders.clear();
  {
    std::lock_guard<std::mutex> lock(optimized_modules_mutex);
    for (auto& [translation, module] : optimized_shader_modules) {
      if (module) dfn().vkDestroyShaderModule(vk, module, nullptr);
    }
    optimized_shader_modules.clear();
  }
  for (auto& [key, layout] : pipeline_layouts) {
    dfn().vkDestroyPipelineLayout(vk, layout.layout, nullptr);
  }
  pipeline_layouts.clear();
  for (auto& [key, layout] : texture_set_layouts) {
    if (layout != layout_empty) dfn().vkDestroyDescriptorSetLayout(vk, layout, nullptr);
  }
  texture_set_layouts.clear();
  shaders.clear();
  FlushSetCache();
  for (auto& retired : retired_set_pools) {
    for (VkDescriptorPool pool : retired.pools) dfn().vkDestroyDescriptorPool(vk, pool, nullptr);
  }
  retired_set_pools.clear();
  for (Submission& s : submissions) {
    for (VkDescriptorPool pool : s.descriptor_pools) {
      dfn().vkDestroyDescriptorPool(vk, pool, nullptr);
    }
    s.descriptor_pools.clear();
    if (s.fence) dfn().vkDestroyFence(vk, s.fence, nullptr);
    if (s.pool) dfn().vkDestroyCommandPool(vk, s.pool, nullptr);
    if (s.main_pool) dfn().vkDestroyCommandPool(vk, s.main_pool, nullptr);
    s = Submission();
  }
  for (VkBufferView view : ramp_views) {
    if (view) dfn().vkDestroyBufferView(vk, view, nullptr);
  }
  if (swap_pool) dfn().vkDestroyDescriptorPool(vk, swap_pool, nullptr);
  if (swap_pipeline) dfn().vkDestroyPipeline(vk, swap_pipeline, nullptr);
  if (swap_pipeline_layout) dfn().vkDestroyPipelineLayout(vk, swap_pipeline_layout, nullptr);
  for (VkDescriptorSetLayout l : {swap_layout_ramp, swap_layout_source, swap_layout_dest,
                                  layout_empty, layout_shared, layout_constants}) {
    if (l) dfn().vkDestroyDescriptorSetLayout(vk, l, nullptr);
  }
  if (ramp_buffer) dfn().vkDestroyBuffer(vk, ramp_buffer, nullptr);
  if (ramp_memory) dfn().vkFreeMemory(vk, ramp_memory, nullptr);
  if (static_pool) dfn().vkDestroyDescriptorPool(vk, static_pool, nullptr);
  if (stream_buffer) dfn().vkDestroyBuffer(vk, stream_buffer, nullptr);
  if (stream_memory) dfn().vkFreeMemory(vk, stream_memory, nullptr);
  if (driver_cache) dfn().vkDestroyPipelineCache(vk, driver_cache, nullptr);
  if (timestamps) dfn().vkDestroyQueryPool(vk, timestamps, nullptr);
  device = nullptr;
}

// --- Layouts and pipelines -----------------------------------------------------------

VkDescriptorSetLayout Renderer::Impl::GetTextureSetLayout(bool vertex, uint32_t texture_count,
                                                          uint32_t sampler_count) {
  if (!texture_count && !sampler_count) {
    return layout_empty;
  }
  uint64_t key = (uint64_t(vertex) << 40) | (uint64_t(texture_count) << 20) | sampler_count;
  auto it = texture_set_layouts.find(key);
  if (it != texture_set_layouts.end()) {
    return it->second;
  }
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  VkShaderStageFlags stages = vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
  for (uint32_t i = 0; i < texture_count; ++i) {
    bindings.push_back({i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, stages, nullptr});
  }
  for (uint32_t i = 0; i < sampler_count; ++i) {
    bindings.push_back({texture_count + i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, stages, nullptr});
  }
  VkDescriptorSetLayoutCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.bindingCount = uint32_t(bindings.size());
  info.pBindings = bindings.data();
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  if (dfn().vkCreateDescriptorSetLayout(vk, &info, nullptr, &layout) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  texture_set_layouts.emplace(key, layout);
  return layout;
}

const PipelineLayout* Renderer::Impl::GetPipelineLayout(uint32_t tex_vs, uint32_t samp_vs,
                                                        uint32_t tex_ps, uint32_t samp_ps) {
  uint64_t key = (uint64_t(tex_vs) << 48) | (uint64_t(samp_vs) << 32) | (uint64_t(tex_ps) << 16) |
                 samp_ps;
  auto it = pipeline_layouts.find(key);
  if (it != pipeline_layouts.end()) {
    return &it->second;
  }
  PipelineLayout layout;
  layout.textures_vertex = GetTextureSetLayout(true, tex_vs, samp_vs);
  layout.textures_pixel = GetTextureSetLayout(false, tex_ps, samp_ps);
  if (!layout.textures_vertex || !layout.textures_pixel) {
    return nullptr;
  }
  VkDescriptorSetLayout sets[SpirvShaderTranslator::kDescriptorSetCount];
  sets[SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram] = layout_shared;
  sets[SpirvShaderTranslator::kDescriptorSetConstants] = layout_constants;
  sets[SpirvShaderTranslator::kDescriptorSetTexturesVertex] = layout.textures_vertex;
  sets[SpirvShaderTranslator::kDescriptorSetTexturesPixel] = layout.textures_pixel;
  VkPipelineLayoutCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  info.setLayoutCount = SpirvShaderTranslator::kDescriptorSetCount;
  info.pSetLayouts = sets;
  if (dfn().vkCreatePipelineLayout(vk, &info, nullptr, &layout.layout) != VK_SUCCESS) {
    return nullptr;
  }
  return &pipeline_layouts.emplace(key, layout).first->second;
}

// T35/T37: um módulo por tradução, criado uma vez (a simplificação e o otimizador custam caro,
// nunca por quadro). A simplificação roda mesmo com o spirv-opt desligado: é ela que tira o
// max(a,a) do caminho do driver na GPU.
VkShaderModule Renderer::Impl::ShaderModuleFor(VulkanShader::VulkanTranslation* translation) {
  const bool simplify_shader = REXCVAR_GET(gh3_native_shader_simplify);
  const bool optimize_shader = REXCVAR_GET(gh3_native_spirv_opt);
  if (!simplify_shader && !optimize_shader) {
    return translation->GetOrCreateShaderModule();
  }
  {
    std::lock_guard<std::mutex> lock(optimized_modules_mutex);
    auto it = optimized_shader_modules.find(translation);
    if (it != optimized_shader_modules.end()) {
      return it->second;
    }
  }
  const std::vector<uint8_t>& bytes = translation->translated_binary();
  std::vector<uint32_t> binary(bytes.size() / sizeof(uint32_t));
  if (!binary.empty()) {
    std::memcpy(binary.data(), bytes.data(), binary.size() * sizeof(uint32_t));
  }
  bool changed = false;
  if (simplify_shader) {
    const shader_simplify::Outcome outcome = shader_simplify::Simplify(binary);
    {
      std::lock_guard<std::mutex> lock(spirv_opt_stats_mutex);
      shader_simplify_stats.Add(outcome);
    }
    if (outcome.fallback) {
      // Uma linha por motivo, para o fallback não virar silêncio no log.
      const uint32_t bit = 1u << uint32_t(outcome.reason);
      if (!(shader_simplify_fallbacks_logged.fetch_or(bit) & bit)) {
        static const char* const kReasons[] = {"", "module too small", "invalid input",
                                               "invalid output"};
        REXLOG_INFO("[native] shader simplify: using the original shader ({})",
                    kReasons[uint32_t(outcome.reason)]);
      }
    } else if (outcome.modified) {
      changed = true;
    }
  }
  if (optimize_shader) {
    const spirv_opt::Outcome outcome = spirv_opt::Optimize(binary);
    {
      std::lock_guard<std::mutex> lock(spirv_opt_stats_mutex);
      spirv_opt_stats.Add(outcome);
    }
    if (outcome.fallback) {
      // Uma linha por motivo, para o fallback não virar silêncio no log.
      const uint32_t bit = 1u << uint32_t(outcome.reason);
      if (!(spirv_opt_fallbacks_logged.fetch_or(bit) & bit)) {
        static const char* const kReasons[] = {"", "module too small", "invalid input",
                                               "optimizer failed", "invalid output"};
        REXLOG_INFO("[native] spirv-opt: using the original shader ({})",
                    kReasons[uint32_t(outcome.reason)]);
      }
    } else if (outcome.optimized) {
      changed = true;
    }
  }
  VkShaderModule module = VK_NULL_HANDLE;
  if (changed) {
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = binary.size() * sizeof(uint32_t);
    info.pCode = binary.data();
    if (dfn().vkCreateShaderModule(vk, &info, nullptr, &module) != VK_SUCCESS) {
      module = VK_NULL_HANDLE;
    }
  }
  if (module == VK_NULL_HANDLE) {
    // Sem simplificação/otimização (ou o driver recusou o módulo): o original do tradutor, que o
    // VulkanTranslation é dono de destruir. Não entra no mapa (senão seria destruído duas vezes).
    return translation->GetOrCreateShaderModule();
  }
  std::lock_guard<std::mutex> lock(optimized_modules_mutex);
  optimized_shader_modules[translation] = module;
  return module;
}

// Translation runs on one worker thread with a translator of its own. Being one
// thread matters: the first completed translation of a shader fills the
// shader's binding lists, and every later one leaves them alone, so a
// translation reported done means its shader's bindings are final.
bool Renderer::Impl::EnsureTranslated(VulkanShader::VulkanTranslation* translation,
                                      bool* pending_out) {
  *pending_out = false;
  DrainTranslations();
  auto state = translation_state.find(translation);
  if (state == translation_state.end()) {
    if (!translation->shader().is_ucode_analyzed()) {
      translation->shader().AnalyzeUcode(disasm);
    }
    if (!REXCVAR_GET(gh3_native_async_pipelines) || !translation_thread.joinable()) {
      bool ok = translator->TranslateAnalyzedShader(*translation);
      if (ok) ShaderModuleFor(translation);
      ++stats().shaders_translated;
      translation_state[translation] = 2;
      if (!ok) {
        REXLOG_ERROR("[native] Shader {:016X} did not translate",
                     translation->shader().ucode_data_hash());
        return false;
      }
    } else {
      translation_state[translation] = 1;
      {
        std::lock_guard<std::mutex> lock(translation_mutex);
        translation_jobs.push_back(translation);
      }
      translation_cv.notify_one();
      *pending_out = true;
      return false;
    }
  } else if (state->second == 1) {
    *pending_out = true;
    return false;
  }
  return translation->is_valid() && ShaderModuleFor(translation) != VK_NULL_HANDLE;
}

void Renderer::Impl::DrainTranslations() {
  if (!translations_done_count.load(std::memory_order_acquire)) {
    return;
  }
  std::vector<VulkanShader::VulkanTranslation*> done;
  {
    std::lock_guard<std::mutex> lock(translation_mutex);
    done.swap(translations_done);
    translations_done_count.store(0, std::memory_order_release);
  }
  for (auto* t : done) {
    translation_state[t] = 2;
    ++stats().shaders_translated;
  }
}

void Renderer::Impl::TranslationThread() {
#if defined(__SWITCH__)
  svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3D);
#endif
  while (true) {
    VulkanShader::VulkanTranslation* translation;
    {
      std::unique_lock<std::mutex> lock(translation_mutex);
      translation_cv.wait(lock, [&] { return translation_stop || !translation_jobs.empty(); });
      if (translation_stop) return;
      translation = translation_jobs.front();
      translation_jobs.pop_front();
    }
    if (worker_translator->TranslateAnalyzedShader(*translation)) {
      ShaderModuleFor(translation);
    } else {
      REXLOG_ERROR("[native] Shader {:016X} did not translate",
                   translation->shader().ucode_data_hash());
    }
    std::lock_guard<std::mutex> lock(translation_mutex);
    translations_done.push_back(translation);
    translations_done_count.fetch_add(1, std::memory_order_release);
  }
}

VkPipeline Renderer::Impl::GetPipeline(const PipelineKey& key,
                                       VulkanShader::VulkanTranslation* vs,
                                       VulkanShader::VulkanTranslation* ps,
                                       VkShaderModule geometry, VkPipelineLayout layout,
                                       bool* pending_out) {
  *pending_out = false;
  DrainCompletedPipelines();
  uint64_t hash = XXH3_64bits(&key, sizeof(key));
  auto it = pipelines.find(hash);
  if (it != pipelines.end()) {
    return it->second;
  }
  if (pending_pipelines.count(hash)) {
    *pending_out = true;
    return VK_NULL_HANDLE;
  }
  PipelineJob job;
  job.hash = hash;
  job.key = key;
  job.vertex = ShaderModuleFor(vs);
  job.pixel = ps ? ShaderModuleFor(ps) : VK_NULL_HANDLE;
  job.geometry = geometry;
  job.layout = layout;
  if (job.vertex) {
    RecordPipeline(hash, key, vs, ps);
  }
  if (REXCVAR_GET(gh3_native_async_pipelines) && pipeline_thread.joinable()) {
    // Compiled on the worker. Until it is ready the draws that need it are
    // skipped, a frame or two of missing objects instead of a frozen frame.
    pending_pipelines.insert(hash);
    {
      std::lock_guard<std::mutex> lock(pipeline_mutex);
      pipeline_jobs.push_back(job);
    }
    pipeline_cv.notify_one();
    *pending_out = true;
    return VK_NULL_HANDLE;
  }
  VkPipeline pipeline = CreatePipeline(job);
  if (pipeline) {
    ++stats().pipelines_created;
    last_pipeline_time = std::chrono::steady_clock::now();
  }
  pipelines.emplace(hash, pipeline);
  return pipeline;
}

void Renderer::Impl::DrainCompletedPipelines() {
  if (!pipelines_completed_count.load(std::memory_order_acquire)) {
    return;
  }
  std::vector<std::pair<uint64_t, VkPipeline>> done;
  {
    std::lock_guard<std::mutex> lock(pipeline_mutex);
    done.swap(pipelines_completed);
    pipelines_completed_count.store(0, std::memory_order_release);
  }
  for (auto& [hash, pipeline] : done) {
    pending_pipelines.erase(hash);
    pipelines.emplace(hash, pipeline);
    if (pipeline) {
      ++stats().pipelines_created;
      last_pipeline_time = std::chrono::steady_clock::now();
    }
  }
}

void Renderer::Impl::PipelineThread() {
#if defined(__SWITCH__)
  // Below the game's threads and the ring: compiling is never urgent enough
  // to take a core from them.
  svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3F);
#endif
  while (true) {
    PipelineJob job;
    {
      std::unique_lock<std::mutex> lock(pipeline_mutex);
      pipeline_cv.wait(lock, [&] { return pipeline_stop || !pipeline_jobs.empty(); });
      if (pipeline_stop) {
        return;
      }
      job = pipeline_jobs.front();
      pipeline_jobs.pop_front();
    }
    VkPipeline pipeline = CreatePipeline(job);
    std::lock_guard<std::mutex> lock(pipeline_mutex);
    pipelines_completed.emplace_back(job.hash, pipeline);
    pipelines_completed_count.fetch_add(1, std::memory_order_release);
  }
}

// Builds the pipeline for a key. Only reads immutable state, so it runs on the
// pipeline worker as well as on the ring thread.
VkPipeline Renderer::Impl::CreatePipeline(const PipelineJob& job) {
  const PipelineKey& key = job.key;
  VkShaderModule geometry = job.geometry;
  VkPipelineLayout layout = job.layout;

  VkSpecializationMapEntry sign_entries[8];
  for (uint32_t i = 0; i < 8; ++i) {
    sign_entries[i] = {i, uint32_t(sizeof(uint32_t) * i), sizeof(uint32_t)};
  }
  VkSpecializationInfo signs = {8, sign_entries, sizeof(key.texture_signs), key.texture_signs};
  const VkSpecializationInfo* specialization = specialize_signs ? &signs : nullptr;

  VkPipelineShaderStageCreateInfo stages[3] = {};
  uint32_t stage_count = 0;
  stages[stage_count++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                           nullptr,
                           0,
                           VK_SHADER_STAGE_VERTEX_BIT,
                           job.vertex,
                           "main",
                           specialization};
  if (geometry) {
    stages[stage_count++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                             nullptr,
                             0,
                             VK_SHADER_STAGE_GEOMETRY_BIT,
                             geometry,
                             "main",
                             nullptr};
  }
  if (job.pixel) {
    stages[stage_count++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                             nullptr,
                             0,
                             VK_SHADER_STAGE_FRAGMENT_BIT,
                             job.pixel,
                             "main",
                             specialization};
  }

  VkPipelineVertexInputStateCreateInfo vertex_input = {};
  vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
  input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VkPrimitiveTopology(key.topology);
  input_assembly.primitiveRestartEnable = key.primitive_restart ? VK_TRUE : VK_FALSE;
  VkPipelineViewportStateCreateInfo viewport = {};
  viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster = {};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.depthClampEnable = key.depth_clamp ? VK_TRUE : VK_FALSE;
  raster.polygonMode = key.polygon_line ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
  raster.cullMode = key.cull_mode;
  raster.frontFace =
      key.front_face_clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.depthBiasEnable = key.depth_format ? VK_TRUE : VK_FALSE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample = {};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VkSampleCountFlagBits(key.samples);
  VkPipelineDepthStencilStateCreateInfo depth = {};
  depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth.depthTestEnable = key.depth_test ? VK_TRUE : VK_FALSE;
  depth.depthWriteEnable = key.depth_write ? VK_TRUE : VK_FALSE;
  depth.depthCompareOp = VkCompareOp(key.depth_compare);
  depth.stencilTestEnable =
      key.stencil_test && HasStencil(VkFormat(key.depth_format)) ? VK_TRUE : VK_FALSE;
  depth.front.failOp = VkStencilOp(key.stencil_front[0]);
  depth.front.passOp = VkStencilOp(key.stencil_front[1]);
  depth.front.depthFailOp = VkStencilOp(key.stencil_front[2]);
  depth.front.compareOp = VkCompareOp(key.stencil_front[3]);
  depth.back.failOp = VkStencilOp(key.stencil_back[0]);
  depth.back.passOp = VkStencilOp(key.stencil_back[1]);
  depth.back.depthFailOp = VkStencilOp(key.stencil_back[2]);
  depth.back.compareOp = VkCompareOp(key.stencil_back[3]);
  VkPipelineColorBlendAttachmentState attachments[4] = {};
  uint32_t attachment_count = 0;
  VkFormat color_formats[4] = {};
  for (uint32_t i = 0; i < 4; ++i) {
    color_formats[i] = VkFormat(key.color_formats[i]);
    if (key.color_formats[i]) {
      attachment_count = i + 1;
    }
    VkPipelineColorBlendAttachmentState& a = attachments[i];
    uint32_t blend = key.blend[i];
    a.colorWriteMask = (key.write_masks >> (4 * i)) & 0xF;
    if (blend != 0x00010001) {
      reg::RB_BLENDCONTROL control;
      control.value = blend;
      a.blendEnable = VK_TRUE;
      a.srcColorBlendFactor = BlendFactorToVk(control.color_srcblend);
      a.dstColorBlendFactor = BlendFactorToVk(control.color_destblend);
      a.colorBlendOp = BlendOpToVk(control.color_comb_fcn);
      a.srcAlphaBlendFactor = BlendFactorToVk(control.alpha_srcblend);
      a.dstAlphaBlendFactor = BlendFactorToVk(control.alpha_destblend);
      a.alphaBlendOp = BlendOpToVk(control.alpha_comb_fcn);
    }
  }
  VkPipelineColorBlendStateCreateInfo blend = {};
  blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = attachment_count;
  blend.pAttachments = attachments;
  VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_DEPTH_BIAS,
      VK_DYNAMIC_STATE_BLEND_CONSTANTS,
      VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
      VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
      VK_DYNAMIC_STATE_STENCIL_REFERENCE,
  };
  VkPipelineDynamicStateCreateInfo dynamic = {};
  dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = uint32_t(std::size(dynamic_states));
  dynamic.pDynamicStates = dynamic_states;
  VkPipelineRenderingCreateInfo rendering = {};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = attachment_count;
  rendering.pColorAttachmentFormats = color_formats;
  rendering.depthAttachmentFormat = VkFormat(key.depth_format);
  rendering.stencilAttachmentFormat = VkFormat(key.depth_format);

  VkGraphicsPipelineCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  info.pNext = &rendering;
  info.stageCount = stage_count;
  info.pStages = stages;
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &input_assembly;
  info.pViewportState = &viewport;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pDepthStencilState = &depth;
  info.pColorBlendState = &blend;
  info.pDynamicState = &dynamic;
  info.layout = layout;
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (dfn().vkCreateGraphicsPipelines(vk, driver_cache, 1, &info, nullptr, &pipeline) !=
      VK_SUCCESS) {
    REXLOG_ERROR("[native] Pipeline creation failed (VS {:016X}, PS {:016X})", key.vs_hash,
                 key.ps_hash);
    pipeline = VK_NULL_HANDLE;
  }
  return pipeline;
}

// F2.5: guarda a chave e o microcódigo dos shaders de um pipeline que acabou de ser compilado, para
// recriá-lo na próxima execução. Copiar o microcódigo custa pouco e só acontece no primeiro uso da
// chave; o mapa respeita o teto do arquivo.
void Renderer::Impl::RecordPipeline(uint64_t hash, const PipelineKey& key,
                                    VulkanShader::VulkanTranslation* vs,
                                    VulkanShader::VulkanTranslation* ps) {
  std::lock_guard<std::mutex> lock(prewarm_mutex);
  if (prewarm_entries.size() >= prewarm::kMaxEntries) return;
  if (prewarm_entries.count(hash)) return;
  const VulkanShader& vs_shader = static_cast<const VulkanShader&>(vs->shader());
  prewarm::Entry entry;
  entry.hash = hash;
  entry.key.resize(sizeof(key));
  std::memcpy(entry.key.data(), &key, sizeof(key));
  entry.vs_ucode.assign(vs_shader.ucode_dwords(),
                        vs_shader.ucode_dwords() + vs_shader.ucode_dword_count());
  if (ps) {
    const VulkanShader& ps_shader = static_cast<const VulkanShader&>(ps->shader());
    entry.ps_ucode.assign(ps_shader.ucode_dwords(),
                          ps_shader.ucode_dwords() + ps_shader.ucode_dword_count());
  }
  prewarm_entries.emplace(hash, std::move(entry));
  prewarm_entry_count.store(prewarm_entries.size(), std::memory_order_relaxed);
}

// --- Render targets ------------------------------------------------------------------

static std::string RtName(const RenderTarget* t) {
  if (!t) return "-";
  return fmt::format("{}b{}p{}{}{}{}[{}x{}]", t->key.is_depth ? "D" : "C", t->key.base_tiles, t->key.pitch,
                     t->key.alias_msaa ? fmt::format("a{}", t->key.alias_msaa) : "",
                     t->key.scene_scaled ? "S" : "", t->key.msaa ? "m" : "", t->width, t->height);
}
#define RT_TRACE(...)                                                                        \
  do {                                                                                       \
    const uint32_t _tf = REXCVAR_GET(gh3_native_rt_trace);                                   \
    if (_tf && frame >= _tf && frame < _tf + 2) REXLOG_INFO("[rt] f{} " __VA_ARGS__);        \
  } while (0)

RenderTarget* Renderer::Impl::GetRenderTarget(const RenderTargetKey& key, uint32_t min_height) {
  RenderTarget* target = CreateOrGetRenderTarget(key, min_height);
  if (!target) return target;
  if (key.alias_msaa) {
    target->alias_seq = ++alias_clock;  // the image the game is about to draw to / read
  } else {
    SyncAliasTarget(target);
  }
  SyncSceneTarget(target);
  return target;
}

// The 1x target at pitch P*scale takes the 1-sample image of the guest MSAA surface of pitch P at the
// same EDRAM base, if that image was used since: the game clears and draws the stage through the MSAA
// surface and reads (resolves, tests against) the 1x one.
void Renderer::Impl::SyncAliasTarget(RenderTarget* target) {
  if (msaa_as_samples || multisample || target->key.alias_msaa) return;
  // T15 + T16: o espelho do palco vai sempre para a imagem CHEIA. Pedida a escalada, atualiza a cheia;
  // a sincronização da escala (SyncSceneTarget, chamada depois) a desce já com o palco. Copiar direto na
  // escalada perdia para essa descida, e o espelho na cheia depois da subida apagava a cena (palco preto).
  if (target->key.scene_scaled) {
    RenderTargetKey full_key = target->key;
    full_key.scene_scaled = 0;
    auto it = render_targets.find(full_key);
    if (it != render_targets.end()) SyncAliasTarget(it->second.get());
    return;
  }
  if (target->checked_clock == alias_clock) return;
  target->checked_clock = alias_clock;
  for (uint32_t guest_msaa = 1; guest_msaa <= 2; ++guest_msaa) {
    uint32_t scale_x = guest_msaa >= 2 ? 2 : 1;
    if (target->key.pitch % scale_x) continue;
    RenderTargetKey source_key = target->key;
    source_key.pitch = target->key.pitch / scale_x;
    source_key.alias_msaa = guest_msaa;
    auto it = render_targets.find(source_key);
    if (it == render_targets.end()) continue;
    RenderTarget* source = it->second.get();
    if (source->alias_seq <= target->synced_seq) continue;
    edram::AliasBlit extent =
        edram::AliasBlitExtent(guest_msaa, source->width, source->height, target->width, target->height);
    target->synced_seq = source->alias_seq;
    if (!extent.valid) continue;
    BlitTarget(source, target, int32_t(extent.src_w), int32_t(extent.src_h), int32_t(extent.dst_w),
               int32_t(extent.dst_h), VK_FILTER_NEAREST);
    // A cheia mudou: a escalada (se houver) tem de descê-la antes do próximo desenho nela.
    target->scene_touch = ++scene_clock;
  }
}

// Blit da região (0,0)-(src_w,src_h) de source para (0,0)-(dst_w,dst_h) de target, com as barreiras de
// quem escreveu antes e de quem lê depois (alias da T15 e imagem escalada da T16).
void Renderer::Impl::BlitTarget(RenderTarget* source, RenderTarget* target, int32_t src_w,
                                int32_t src_h, int32_t dst_w, int32_t dst_h, VkFilter filter) {
  RT_TRACE("blit {} {}x{} -> {} {}x{}", frame, RtName(source), src_w, src_h, RtName(target), dst_w, dst_h);
  BeginRecording();
  EndPass();
  VkCommandBuffer c = cmd();
  VkMemoryBarrier before = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             1, &before, 0, nullptr, 0, nullptr);
  VkImageAspectFlags aspect =
      target->key.is_depth ? (HasStencil(target->format)
                                  ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                  : VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT))
                           : VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageBlit blit = {};
  blit.srcSubresource = {aspect, 0, 0, 1};
  blit.dstSubresource = blit.srcSubresource;
  blit.srcOffsets[1] = {src_w, src_h, 1};
  blit.dstOffsets[1] = {dst_w, dst_h, 1};
  vkCmdBlitImage(c, source->image, VK_IMAGE_LAYOUT_GENERAL, target->image,
                 VK_IMAGE_LAYOUT_GENERAL, 1, &blit, filter);
  VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_SHADER_READ_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                             1, &after, 0, nullptr, 0, nullptr);
}

// T32: pipeline de CAS por formato do alvo. Criado na primeira subida da cena; se falhar, o blit
// LINEAR continua valendo (não derruba nada). O descriptor set vem do AllocateSet por submissão.
bool Renderer::Impl::EnsureSceneCas(VkFormat format) {
  for (const SceneCasPipeline& cached : scene_cas_pipelines) {
    if (cached.format == format) return cached.pipeline != VK_NULL_HANDLE;
  }
  if (scene_cas_failed) return false;
  if (!scene_cas_set_layout) {
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    if (dfn().vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &scene_cas_set_layout) !=
        VK_SUCCESS) {
      scene_cas_failed = true;
      return false;
    }
    VkPushConstantRange ranges[2] = {
        {VK_SHADER_STAGE_VERTEX_BIT, 0, uint32_t(sizeof(scene::CasRect))},
        {VK_SHADER_STAGE_FRAGMENT_BIT, uint32_t(sizeof(scene::CasRect)),
         uint32_t(sizeof(scene::CasEffect))},
    };
    VkPipelineLayoutCreateInfo pipeline_layout = {};
    pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout.setLayoutCount = 1;
    pipeline_layout.pSetLayouts = &scene_cas_set_layout;
    pipeline_layout.pushConstantRangeCount = uint32_t(std::size(ranges));
    pipeline_layout.pPushConstantRanges = ranges;
    if (dfn().vkCreatePipelineLayout(vk, &pipeline_layout, nullptr, &scene_cas_layout) !=
        VK_SUCCESS) {
      scene_cas_failed = true;
      return false;
    }
  }
  VkShaderModule modules[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkShaderModuleCreateInfo module_info = {};
  module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  module_info.codeSize = sizeof(::shaders::guest_output_triangle_strip_rect_vs);
  module_info.pCode = ::shaders::guest_output_triangle_strip_rect_vs;
  bool ok = dfn().vkCreateShaderModule(vk, &module_info, nullptr, &modules[0]) == VK_SUCCESS;
  module_info.codeSize = sizeof(::shaders::guest_output_ffx_cas_resample_ps);
  module_info.pCode = ::shaders::guest_output_ffx_cas_resample_ps;
  ok = ok && dfn().vkCreateShaderModule(vk, &module_info, nullptr, &modules[1]) == VK_SUCCESS;
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (ok) {
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = modules[0];
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = modules[1];
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    // O VS gera (0,0),(1,0),(0,1),(1,1) a partir do gl_VertexIndex.
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample_state = {};
    multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment = {};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend = {};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = uint32_t(std::size(dynamic_states));
    dynamic.pDynamicStates = dynamic_states;
    VkPipelineRenderingCreateInfo rendering = {};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample_state;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = scene_cas_layout;
    ok = dfn().vkCreateGraphicsPipelines(vk, driver_cache, 1, &info, nullptr, &pipeline) ==
         VK_SUCCESS;
  }
  for (VkShaderModule module : modules) {
    if (module) dfn().vkDestroyShaderModule(vk, module, nullptr);
  }
  if (!ok) {
    REXLOG_WARN("[native] scene CAS: pipeline creation failed; the upscale stays LINEAR");
    scene_cas_failed = true;
    return false;
  }
  scene_cas_pipelines.push_back({format, pipeline});
  return true;
}

bool Renderer::Impl::CasUpscale(RenderTarget* source, RenderTarget* target,
                                const scene::SceneBlit& extent) {
  if (!source || !target) return false;
  scene::CasParams params =
      scene::CasFor(extent.scaled_w, extent.scaled_h, extent.full_w, extent.full_h, target->width,
                    target->height, scene_cas_sharpness);
  if (!params.valid || !EnsureSceneCas(target->format)) return false;
  BeginRecording();
  EndPass();
  VkCommandBuffer c = cmd();
  VkDescriptorSet set = AllocateSet(scene_cas_set_layout);
  if (!set) return false;
  VkDescriptorImageInfo image = {VK_NULL_HANDLE, source->view, VK_IMAGE_LAYOUT_GENERAL};
  VkWriteDescriptorSet write = {};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  write.pImageInfo = &image;
  dfn().vkUpdateDescriptorSets(vk, 1, &write, 0, nullptr);
  VkMemoryBarrier before = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 1, &before, 0, nullptr, 0, nullptr);
  PassTargets targets;
  targets.color[0] = target;
  if (!BeginPass(targets, params.height)) return false;
  VkPipeline pipeline = VK_NULL_HANDLE;
  for (const SceneCasPipeline& cached : scene_cas_pipelines) {
    if (cached.format == target->format) pipeline = cached.pipeline;
  }
  VkViewport viewport = {0.0f, 0.0f, float(target->width), float(target->height), 0.0f, 1.0f};
  VkRect2D scissor = {{0, 0}, {params.width, params.height}};
  dfn().vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  dfn().vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_cas_layout, 0, 1, &set, 0,
                                nullptr);
  dfn().vkCmdPushConstants(c, scene_cas_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           uint32_t(sizeof(params.push)), &params.push);
  dfn().vkCmdSetViewport(c, 0, 1, &viewport);
  dfn().vkCmdSetScissor(c, 0, 1, &scissor);
  dfn().vkCmdDraw(c, 4, 1, 0, 0);
  EndPass();
  // O CAS mexeu no estado dinâmico e nos binds que o caminho dos draws guarda como "já ligados":
  // esquecer tudo força o próximo draw do jogo a religar o que é dele.
  bound_pipeline = VK_NULL_HANDLE;
  bound_layout = VK_NULL_HANDLE;
  bound_static_sets = false;
  bound_viewport = {};
  bound_scissor = {};
  VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_SHADER_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, nullptr, 0,
                             nullptr);
  if (!scene_cas_logged) {
    scene_cas_logged = true;
    REXLOG_INFO("[native] scene CAS: upscaling {}x{} -> {}x{} (ratio {:g})", extent.scaled_w,
                extent.scaled_h, extent.full_w, extent.full_h,
                double(params.push.effect.input_output_ratio[0]));
  }
  return true;
}

// T47: pipeline do EASU (FSR1) por formato do alvo, no mesmo encaixe do CAS — criado na primeira
// subida da cena e, em falha, o CAS/LINEAR continua valendo (não derruba nada). O PS do EASU lê a
// imagem em binding 0 e um sampler linear-clamp imutável em binding 1, como o presenter do SDK.
bool Renderer::Impl::EnsureSceneEasu(VkFormat format) {
  for (const SceneEasuPipeline& cached : scene_easu_pipelines) {
    if (cached.format == format) return cached.pipeline != VK_NULL_HANDLE;
  }
  if (scene_easu_failed) return false;
  if (!scene_easu_set_layout) {
    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    if (dfn().vkCreateSampler(vk, &sampler_info, nullptr, &scene_easu_sampler) != VK_SUCCESS) {
      scene_easu_failed = true;
      return false;
    }
    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = &scene_easu_sampler;
    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = uint32_t(std::size(bindings));
    layout_info.pBindings = bindings;
    if (dfn().vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &scene_easu_set_layout) !=
        VK_SUCCESS) {
      scene_easu_failed = true;
      return false;
    }
    VkPushConstantRange ranges[2] = {
        {VK_SHADER_STAGE_VERTEX_BIT, 0, uint32_t(sizeof(scene::CasRect))},
        {VK_SHADER_STAGE_FRAGMENT_BIT, uint32_t(sizeof(scene::CasRect)),
         uint32_t(sizeof(scene::EasuEffect))},
    };
    VkPipelineLayoutCreateInfo pipeline_layout = {};
    pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout.setLayoutCount = 1;
    pipeline_layout.pSetLayouts = &scene_easu_set_layout;
    pipeline_layout.pushConstantRangeCount = uint32_t(std::size(ranges));
    pipeline_layout.pPushConstantRanges = ranges;
    if (dfn().vkCreatePipelineLayout(vk, &pipeline_layout, nullptr, &scene_easu_layout) !=
        VK_SUCCESS) {
      scene_easu_failed = true;
      return false;
    }
  }
  VkShaderModule modules[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkShaderModuleCreateInfo module_info = {};
  module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  module_info.codeSize = sizeof(::shaders::guest_output_triangle_strip_rect_vs);
  module_info.pCode = ::shaders::guest_output_triangle_strip_rect_vs;
  bool ok = dfn().vkCreateShaderModule(vk, &module_info, nullptr, &modules[0]) == VK_SUCCESS;
  module_info.codeSize = sizeof(::shaders::guest_output_ffx_fsr_easu_ps);
  module_info.pCode = ::shaders::guest_output_ffx_fsr_easu_ps;
  ok = ok && dfn().vkCreateShaderModule(vk, &module_info, nullptr, &modules[1]) == VK_SUCCESS;
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (ok) {
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = modules[0];
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = modules[1];
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample_state = {};
    multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment = {};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend = {};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = uint32_t(std::size(dynamic_states));
    dynamic.pDynamicStates = dynamic_states;
    VkPipelineRenderingCreateInfo rendering = {};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample_state;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = scene_easu_layout;
    ok = dfn().vkCreateGraphicsPipelines(vk, driver_cache, 1, &info, nullptr, &pipeline) ==
         VK_SUCCESS;
  }
  for (VkShaderModule module : modules) {
    if (module) dfn().vkDestroyShaderModule(vk, module, nullptr);
  }
  if (!ok) {
    REXLOG_WARN("[native] scene FSR1: EASU pipeline creation failed; the upscale falls back");
    scene_easu_failed = true;
    return false;
  }
  scene_easu_pipelines.push_back({format, pipeline});
  return true;
}

bool Renderer::Impl::EasuUpscale(RenderTarget* source, RenderTarget* target,
                                 const scene::SceneBlit& extent) {
  RT_TRACE("easu {} -> {}", frame, RtName(source), RtName(target));
  if (!source || !target) return false;
  scene::EasuParams params =
      scene::EasuFor(extent.scaled_w, extent.scaled_h, source->width, source->height, extent.full_w,
                     extent.full_h, target->width, target->height);
  if (!params.valid || !EnsureSceneEasu(target->format)) return false;
  BeginRecording();
  EndPass();
  VkCommandBuffer c = cmd();
  VkDescriptorSet set = AllocateSet(scene_easu_set_layout);
  if (!set) return false;
  VkDescriptorImageInfo image = {VK_NULL_HANDLE, source->view, VK_IMAGE_LAYOUT_GENERAL};
  VkWriteDescriptorSet write = {};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  write.pImageInfo = &image;
  dfn().vkUpdateDescriptorSets(vk, 1, &write, 0, nullptr);
  VkMemoryBarrier before = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 1, &before, 0, nullptr, 0, nullptr);
  PassTargets targets;
  targets.color[0] = target;
  if (!BeginPass(targets, params.height)) return false;
  VkPipeline pipeline = VK_NULL_HANDLE;
  for (const SceneEasuPipeline& cached : scene_easu_pipelines) {
    if (cached.format == target->format) pipeline = cached.pipeline;
  }
  VkViewport viewport = {0.0f, 0.0f, float(target->width), float(target->height), 0.0f, 1.0f};
  VkRect2D scissor = {{0, 0}, {params.width, params.height}};
  dfn().vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  dfn().vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_easu_layout, 0, 1, &set, 0,
                                nullptr);
  dfn().vkCmdPushConstants(c, scene_easu_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           uint32_t(sizeof(params.push)), &params.push);
  dfn().vkCmdSetViewport(c, 0, 1, &viewport);
  dfn().vkCmdSetScissor(c, 0, 1, &scissor);
  dfn().vkCmdDraw(c, 4, 1, 0, 0);
  EndPass();
  // Como o CAS, o EASU mexeu no estado dinâmico e nos binds que o caminho dos draws guarda como "já
  // ligados": esquecer tudo força o próximo draw do jogo a religar o que é dele.
  bound_pipeline = VK_NULL_HANDLE;
  bound_layout = VK_NULL_HANDLE;
  bound_static_sets = false;
  bound_viewport = {};
  bound_scissor = {};
  VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_SHADER_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, nullptr, 0,
                             nullptr);
  if (!scene_easu_logged || extent.scaled_w != scene_easu_last_extent.scaled_w ||
      extent.scaled_h != scene_easu_last_extent.scaled_h ||
      extent.full_w != scene_easu_last_extent.full_w ||
      extent.full_h != scene_easu_last_extent.full_h) {
    scene_easu_logged = true;
    scene_easu_last_extent = extent;
    REXLOG_INFO("[native] scene FSR1: EASU upscaling {}x{} -> {}x{} (ratio {:g}/{:g}, inv {:g}/{:g}, "
                "alvo {}x{})",
                extent.scaled_w, extent.scaled_h, extent.full_w, extent.full_h,
                double(params.push.effect.input_output_size_ratio[0]),
                double(params.push.effect.input_output_size_ratio[1]),
                double(params.push.effect.input_size_inv[0]),
                double(params.push.effect.input_size_inv[1]), target->width, target->height);
  }
  return true;
}

// T16: o par alvo cheio F / imagem escalada S da cena. F acessado (HUD, resolve, limpeza) traz antes o
// que S desenhou desde então (blit para cima); S usada para desenhar traz antes o que F recebeu (blit
// para baixo: a limpeza do palco, por exemplo). Cada lado só absorve o que o outro tem de novo.
void Renderer::Impl::SyncSceneTarget(RenderTarget* target) {
  if (!scene::Enabled(scene_scale) || target->key.alias_msaa || target->key.msaa ||
      target->key.pitch != scene::kScenePitch) {
    return;
  }
  const bool scaled = target->key.scene_scaled != 0;
  RenderTargetKey other_key = target->key;
  other_key.scene_scaled = scaled ? 0 : 1;
  auto it = render_targets.find(other_key);
  RenderTarget* other = it == render_targets.end() ? nullptr : it->second.get();
  const VkFilter filter = target->key.is_depth ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
  if (other) {
    RenderTarget* full = scaled ? other : target;
    RenderTarget* small = scaled ? target : other;
    uint64_t newer = scaled ? full->scene_touch : small->scene_dirty;
    if (newer > target->scene_seen) {
      scene::SceneBlit e = scene::BlitExtent(scene_scale, small->width, small->height, full->width,
                                             full->height);
      target->scene_seen = newer;
      if (e.valid) {
        if (scaled) {
          // Descer para a imagem escalada (a limpeza do palco, por exemplo) continua LINEAR.
          BlitTarget(full, small, int32_t(e.full_w), int32_t(e.full_h), int32_t(e.scaled_w),
                     int32_t(e.scaled_h), filter);
        } else if (scene_upscale_mode == scene::UpscaleMode::kFsr1 &&
                   scene::Fsr1Compatible(true, full->key.is_depth != 0, small->format,
                                         full->format) &&
                   EasuUpscale(small, full, e)) {
          // T47: subiu com EASU (FSR1).
        } else if (scene_upscale_mode == scene::UpscaleMode::kCas &&
                   scene::CasCompatible(true, full->key.is_depth != 0, small->format,
                                        full->format) &&
                   CasUpscale(small, full, e)) {
          // T32: subiu com CAS.
        } else {
          BlitTarget(small, full, int32_t(e.scaled_w), int32_t(e.scaled_h), int32_t(e.full_w),
                     int32_t(e.full_h), filter);
        }
      }
    }
  }
  if (scaled) {
    target->scene_dirty = ++scene_clock;
  } else {
    target->scene_touch = ++scene_clock;
  }
}

RenderTarget* Renderer::Impl::CreateOrGetRenderTarget(const RenderTargetKey& key, uint32_t min_height) {
  min_height = std::clamp(min_height, 16u, 2048u);
  // T24: um alias 4x com o scissor default (8192) pede 2048 linhas, mas o alvo 1x que ele espelha
  // tem a altura real (180-720). Capar o alias a essa altura corta o passe sem mudar o que aparece:
  // o blit do alias (SyncAliasTarget) só usa as primeiras full/2 linhas.
  if (alias_cap && key.alias_msaa) {
    RenderTargetKey full_key = key;
    full_key.alias_msaa = 0;
    auto fit = render_targets.find(full_key);
    const uint32_t cap = fit != render_targets.end()
                             ? edram::AliasHeight(fit->second->height, key.alias_msaa)
                             : 1024u;
    min_height = std::min(min_height, std::max(16u, cap));
  }
  // T16: a imagem escalada da cena; min_height continua em pixels do jogo.
  const bool scaled = key.scene_scaled && scene::Enabled(scene_scale);
  const float scale_now = scaled ? scene_scale : 0.0f;
  auto it = render_targets.find(key);
  RenderTarget* old = nullptr;
  if (it != render_targets.end()) {
    old = it->second.get();
    old->last_used_frame = frame;
    // T31: o controlador dinâmico muda a escala; um alvo criado com outra escala é recriado, mesmo que
    // o tamanho novo seja menor (senão descer a resolução não economizaria nada).
    const bool same_scale = !scaled || std::abs(old->scale_used - scale_now) < 1e-4f;
    if (same_scale &&
        old->height >= (scaled ? scene::ScaledSize(scene_scale, rex::align(min_height, 16u))
                               : min_height)) {
      return old;
    }
  }
  // F3.3: o conteúdo deste alvo pode estar no destino que ficou com a imagem antiga; volta antes de
  // a imagem ser recriada (o caminho abaixo copia o conteúdo do alvo para a nova).
  if (old && old->content_address) {
    RestoreTargetContent(old);
  }
  auto rt = std::make_unique<RenderTarget>();
  rt->key = key;
  rt->width = std::max(key.pitch, 1u);
  rt->height = rex::align(min_height, 16u);
  if (scaled) {
    rt->width = scene::ScaledSize(scene_scale, rt->width);
    rt->height = scene::ScaledSize(scene_scale, rt->height);
  }
  rt->scale_used = scale_now;
  rt->samples = VkSampleCountFlagBits(1u << key.msaa);
  rt->format = key.is_depth ? DepthFormatFor(key.format)
                            : ColorFormatToVk(xenos::ColorRenderTargetFormat(key.format));
  rt->last_used_frame = frame;
  if (old) {
    rt->alias_seq = old->alias_seq;
    rt->synced_seq = old->synced_seq;
    rt->known_1x_height = old->known_1x_height;
    rt->scene_dirty = old->scene_dirty;
    rt->scene_touch = old->scene_touch;
    rt->scene_seen = old->scene_seen;
  }
  VkImageCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = rt->format;
  info.extent = {rt->width, rt->height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = rt->samples;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  // The depth keeps TRANSFER_SRC: the stencil-words resolve falls back to
  // vkCmdCopyImageToBuffer. NVK therefore will not expose ZCULL for it until Mesa
  // accepts TRANSFER_SRC in the ZCULL eligibility (see gh3_native_zcull).
  info.usage = (key.is_depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT
                             : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!rex::ui::vulkan::util::CreateDedicatedAllocationImage(
          device, info, rex::ui::vulkan::util::MemoryPurpose::kDeviceLocal, rt->image,
          rt->memory)) {
    REXLOG_ERROR("[native] Could not create a {}x{} render target", rt->width, rt->height);
    return old;
  }
  VkImageAspectFlags aspect =
      key.is_depth ? (HasStencil(rt->format)
                          ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                          : VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT))
                   : VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageViewCreateInfo view = {};
  view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view.image = rt->image;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = rt->format;
  view.subresourceRange = rex::ui::vulkan::util::InitializeSubresourceRange(aspect);
  dfn().vkCreateImageView(vk, &view, nullptr, &rt->view);
  if (key.is_depth) {
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dfn().vkCreateImageView(vk, &view, nullptr, &rt->sample_view);
    view.subresourceRange.aspectMask = aspect;
  }

  // Render targets live in GENERAL. A new one starts cleared, or with the
  // content of the smaller one it replaces.
  BeginRecording();
  EndPass();
  VkCommandBuffer c = cmd();
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = rt->image;
  barrier.subresourceRange = view.subresourceRange;
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
  if (key.is_depth) {
    VkClearDepthStencilValue clear = {1.0f, 0};
    vkCmdClearDepthStencilImage(c, rt->image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1,
                                      &view.subresourceRange);
  } else {
    VkClearColorValue clear = {};
    dfn().vkCmdClearColorImage(c, rt->image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1,
                               &view.subresourceRange);
  }
  if (old) {
    VkMemoryBarrier memory_barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                      VK_ACCESS_TRANSFER_WRITE_BIT |
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                      VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory_barrier, 0, nullptr,
                               0, nullptr);
    if (scaled && old->scale_used > 0.0f && std::abs(old->scale_used - scale_now) >= 1e-4f) {
      // T31: o conteúdo da imagem escalada é o mesmo, em outro tamanho: um blit estica em vez de
      // copiar 1:1 (o SyncSceneTarget ainda sobrepõe o que o alvo cheio tiver de mais novo).
      VkImageBlit blit = {};
      blit.srcSubresource = {aspect, 0, 0, 1};
      blit.dstSubresource = blit.srcSubresource;
      blit.srcOffsets[1] = {int32_t(old->width), int32_t(old->height), 1};
      blit.dstOffsets[1] = {int32_t(rt->width), int32_t(rt->height), 1};
      vkCmdBlitImage(c, old->image, VK_IMAGE_LAYOUT_GENERAL, rt->image,
                     VK_IMAGE_LAYOUT_GENERAL, 1, &blit,
                     key.is_depth ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    } else {
      VkImageCopy copy = {};
      copy.srcSubresource = {aspect, 0, 0, 1};
      copy.dstSubresource = copy.srcSubresource;
      copy.extent = {std::min(old->width, rt->width), old->height, 1};
      dfn().vkCmdCopyImage(c, old->image, VK_IMAGE_LAYOUT_GENERAL, rt->image,
                           VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    }
    // Destroyed once this frame is done on the GPU (waiting for idle here
    // was a visible hitch).
    retired_render_targets.push_back({frame, old->image, old->view, old->sample_view, old->memory});
  }
  VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                           VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(cmd(), VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             0, 1, &after, 0, nullptr, 0, nullptr);
  RenderTarget* result = rt.get();
  render_targets[key] = std::move(rt);
  ++stats().render_targets_created;
  ++rt_generation;
  REXLOG_INFO("[native] Render target {}x{} {} format {} at tile {}, {}x MSAA", result->width,
              result->height, key.is_depth ? "depth" : "color", key.format, key.base_tiles,
              1u << key.msaa);
  if (REXCVAR_GET(gh3_native_format_log)) {
    using ColorFormat = xenos::ColorRenderTargetFormat;
    format_log::Target entry;
    entry.frame = frame;
    entry.base = key.base_tiles << 12;
    entry.pitch = key.pitch;
    entry.width = result->width;
    entry.height = result->height;
    entry.samples = 1u << key.msaa;
    entry.depth = key.is_depth != 0;
    entry.vk_format = uint32_t(result->format);
    entry.guest_format = key.format;
    entry.half_float = !key.is_depth &&
                       (key.format == uint32_t(ColorFormat::k_16_16_16_16_FLOAT) ||
                        key.format == uint32_t(ColorFormat::k_2_10_10_10_FLOAT) ||
                        key.format == uint32_t(ColorFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16));
    entry.gamma = !key.is_depth && key.format == uint32_t(ColorFormat::k_8_8_8_8_GAMMA);
    entry.clear_color = !key.is_depth;
    entry.clear_depth = key.is_depth != 0;
    entry.scene_scaled = scaled;
    entry.alias_msaa = key.alias_msaa;
    REXLOG_INFO("[native] {}", format_log::Format(entry));
    format_audit::Target audit_target;
    audit_target.vk_format = uint32_t(result->format);
    audit_target.guest_format = key.format;
    audit_target.width = result->width;
    audit_target.height = result->height;
    audit_target.samples = 1u << key.msaa;
    audit_target.depth = key.is_depth != 0;
    format_audit.Add(audit_target);
  }
  return result;
}

// F3.3: a imagem de um alvo de profundidade, de novo (mesmo tamanho, formato e uso), começando
// limpa. Usada quando o alvo entrega a imagem que tinha a um resolve.
bool Renderer::Impl::CreateDepthImage(RenderTarget* rt) {
  if (!rt || !rt->key.is_depth || rt->samples != VK_SAMPLE_COUNT_1_BIT) {
    return false;
  }
  VkImageCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = rt->format;
  info.extent = {rt->width, rt->height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!rex::ui::vulkan::util::CreateDedicatedAllocationImage(
          device, info, rex::ui::vulkan::util::MemoryPurpose::kDeviceLocal, rt->image,
          rt->memory)) {
    REXLOG_ERROR("[native] Could not create a {}x{} depth image", rt->width, rt->height);
    rt->image = VK_NULL_HANDLE;
    rt->memory = VK_NULL_HANDLE;
    return false;
  }
  const VkImageAspectFlags aspect =
      HasStencil(rt->format) ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                             : VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT);
  VkImageViewCreateInfo view = {};
  view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view.image = rt->image;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = rt->format;
  view.subresourceRange = rex::ui::vulkan::util::InitializeSubresourceRange(aspect);
  dfn().vkCreateImageView(vk, &view, nullptr, &rt->view);
  view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  dfn().vkCreateImageView(vk, &view, nullptr, &rt->sample_view);
  view.subresourceRange.aspectMask = aspect;
  // Como em CreateOrGetRenderTarget: GENERAL, começando limpa.
  BeginRecording();
  EndPass();
  VkCommandBuffer c = cmd();
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = rt->image;
  barrier.subresourceRange = rex::ui::vulkan::util::InitializeSubresourceRange(aspect);
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
  VkClearDepthStencilValue clear = {1.0f, 0};
  vkCmdClearDepthStencilImage(c, rt->image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1,
                              &barrier.subresourceRange);
  VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                           VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             0, 1, &after, 0, nullptr, 0, nullptr);
  return true;
}

// F3.3: entrega a imagem do alvo ao destino (nada é copiado) e dá ao alvo uma imagem nova.
bool Renderer::Impl::HandOffDepthImage(RenderTarget* rt, uint32_t address) {
  if (!rt || rt->content_address || !rt->key.is_depth) {
    return false;
  }
  VkImage image = rt->image;
  VkDeviceMemory memory = rt->memory;
  VkImageView view = rt->view;
  VkImageView sample_view = rt->sample_view;
  rt->image = VK_NULL_HANDLE;
  rt->memory = VK_NULL_HANDLE;
  rt->view = VK_NULL_HANDLE;
  rt->sample_view = VK_NULL_HANDLE;
  if (!CreateDepthImage(rt)) {
    rt->image = image;
    rt->memory = memory;
    rt->view = view;
    rt->sample_view = sample_view;
    return false;
  }
  ResolveDestination* dest = textures.BindBorrowedDepth(
      address, rt->width, rt->height, rt->format,
      uint32_t(xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR), image, memory, cmd());
  if (!dest) {
    // Não deu: a imagem antiga (com o conteúdo) volta a ser a do alvo; a nova é descartada.
    retired_render_targets.push_back({frame, rt->image, rt->view, rt->sample_view, rt->memory});
    rt->image = image;
    rt->memory = memory;
    rt->view = view;
    rt->sample_view = sample_view;
    return false;
  }
  if (view) dfn().vkDestroyImageView(vk, view, nullptr);
  if (sample_view) dfn().vkDestroyImageView(vk, sample_view, nullptr);
  rt->content_address = address;
  return true;
}

// F3.3: o conteúdo do alvo está no destino que ficou com a imagem; traz de volta antes de o alvo ser
// lido ou escrito (o passe seguinte carrega o que estiver nele).
void Renderer::Impl::RestoreTargetContent(RenderTarget* rt) {
  const uint32_t address = rt ? rt->content_address : 0;
  if (!address) {
    return;
  }
  rt->content_address = 0;
  const ResolveDestination* dest = textures.FindResolveDestination(address);
  if (!dest || dest->format != rt->format || dest->width < rt->width ||
      dest->height < rt->height) {
    return;
  }
  ++resolve_ownership.restores;
  resolve_ownership.restore_bytes += uint64_t(rt->width) * rt->height * 4;
  EndPass();
  VkCommandBuffer c = cmd();
  const VkImageAspectFlags aspect =
      HasStencil(rt->format) ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                             : VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT);
  VkMemoryBarrier to_copy = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_SHADER_READ_BIT,
                             VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(c,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &to_copy, 0, nullptr, 0,
                             nullptr);
  VkImageCopy copy = {};
  copy.srcSubresource = {aspect, 0, 0, 1};
  copy.dstSubresource = copy.srcSubresource;
  copy.extent = {std::min(rt->width, dest->width), std::min(rt->height, dest->height), 1};
  dfn().vkCmdCopyImage(c, dest->image, VK_IMAGE_LAYOUT_GENERAL, rt->image,
                       VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
  VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                           VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_SHADER_READ_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 1, &after, 0, nullptr, 0, nullptr);
}

bool Renderer::Impl::BeginPass(const PassTargets& targets, uint32_t wanted_height) {
  RT_TRACE("pass c0={} c1={} d={} h={}", frame, RtName(targets.color[0]), RtName(targets.color[1]),
           RtName(targets.depth), wanted_height);
  // F3.3: um alvo que entregou a imagem a um resolve volta a ser desenhado com loadOp LOAD, então o
  // conteúdo vem de volta antes do passe. Um clear inteiro (o do ZCULL) o dispensa.
  for (uint32_t i = 0; i < 4; ++i) {
    if (targets.color[i] && targets.color[i]->content_address) {
      RestoreTargetContent(targets.color[i]);
    }
  }
  if (targets.depth && targets.depth->content_address) {
    if (zcull && targets.depth->cleared_frame != frame) {
      targets.depth->content_address = 0;
    } else {
      RestoreTargetContent(targets.depth);
    }
  }
  // The render area is the attachment size capped by what this draw can touch,
  // not the whole attachment: one full-EDRAM draw (the game's 4x alias with the
  // default 8192 scissor) used to grow the shared 1280 target to 2048 rows and
  // every 720-row stage draw then paid for 2048. Same targets but a different
  // height is a new scope; loadOp=LOAD keeps the contents.
  uint32_t height_cap = UINT32_MAX;
  for (uint32_t i = 0; i < 4; ++i) {
    if (targets.color[i]) height_cap = std::min(height_cap, targets.color[i]->height);
  }
  if (targets.depth) height_cap = std::min(height_cap, targets.depth->height);
  const uint32_t wanted = std::min(wanted_height, height_cap);
  if (in_pass) {
    // Keep the scope if every attachment the draw needs is already bound at
    // the same slot and covers the same rows.
    bool compatible = wanted == pass_extent.height;
    for (uint32_t i = 0; i < 4; ++i) {
      if (targets.color[i] && targets.color[i] != pass.color[i]) compatible = false;
    }
    if (targets.depth && targets.depth != pass.depth) compatible = false;
    if (compatible) {
      return true;
    }
    EndPass();
  }
  FlushResolveBarrier();
  pass = targets;
  VkRenderingAttachmentInfo colors[4] = {};
  uint32_t color_count = 0;
  uint32_t width = UINT32_MAX, height = UINT32_MAX;
  for (uint32_t i = 0; i < 4; ++i) {
    colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    if (RenderTarget* rt = targets.color[i]) {
      colors[i].imageView = rt->view;
      colors[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      color_count = i + 1;
      width = std::min(width, rt->width);
      height = std::min(height, rt->height);
    }
  }
  VkRenderingAttachmentInfo depth = {};
  depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  if (targets.depth) {
    depth.imageView = targets.depth->view;
    depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    if (zcull && targets.depth->cleared_frame != frame) {
      // ZCULL: NVK enables it for a depth attachment loaded with CLEAR (nvk_cmd_draw.c).
      // Only the first pass of the frame, so later passes keep what was drawn.
      depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depth.clearValue.depthStencil = {1.0f, 0};
      targets.depth->cleared_frame = frame;
    }
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    width = std::min(width, targets.depth->width);
    height = std::min(height, targets.depth->height);
  }
  if (width == UINT32_MAX) {
    return false;
  }
  height = std::min(height, wanted);
  VkRenderingInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  info.renderArea.extent = {width, height};
  info.layerCount = 1;
  info.colorAttachmentCount = color_count;
  info.pColorAttachments = colors;
  info.pDepthAttachment = targets.depth ? &depth : nullptr;
  info.pStencilAttachment =
      targets.depth && HasStencil(targets.depth->format) ? &depth : nullptr;
  // F4.1: o carimbo (e a limpeza seletiva de queries que ele pode pedir) fica fora do render
  // pass — vkCmdResetQueryPool não pode ser gravado dentro de um.
  ProfileMark((kProfilePass << 60) | ((uint64_t(height) & 0xFFF) << 48) |
              (ProfileTargetBits(targets.color[0]) << 24) | ProfileTargetBits(targets.depth));
  dfn().vkCmdBeginRendering(cmd(), &info);
  in_pass = true;
  pass_extent = {width, height};
  ++stats().render_passes;
  return true;
}

// --- System constants ----------------------------------------------------------------

void Renderer::Impl::UpdateSystemConstants(bool primitive_polygonal,
                                           const draw_util::ViewportInfo& viewport,
                                           uint32_t used_texture_mask, uint32_t,
                                           const VulkanShader* vs, const VulkanShader* ps) {
  // VulkanCommandProcessor::UpdateSystemConstantValues for host render
  // targets, without EDRAM, tessellation or memory export.
  const RegisterFile& r = *regs;
  auto pa_cl_clip_cntl = r.Get<reg::PA_CL_CLIP_CNTL>();
  auto pa_cl_vte_cntl = r.Get<reg::PA_CL_VTE_CNTL>();
  auto rb_colorcontrol = r.Get<reg::RB_COLORCONTROL>();
  auto rb_depth_info = r.Get<reg::RB_DEPTH_INFO>();
  auto rb_surface_info = r.Get<reg::RB_SURFACE_INFO>();
  auto vgt_draw_initiator = r.Get<reg::VGT_DRAW_INITIATOR>();
  SpirvShaderTranslator::SystemConstants& sc = system_constants;

  uint32_t flags = 0;
  if (pa_cl_vte_cntl.vtx_xy_fmt) flags |= SpirvShaderTranslator::kSysFlag_XYDividedByW;
  if (pa_cl_vte_cntl.vtx_z_fmt) flags |= SpirvShaderTranslator::kSysFlag_ZDividedByW;
  if (pa_cl_vte_cntl.vtx_w0_fmt) flags |= SpirvShaderTranslator::kSysFlag_WNotReciprocal;
  if (primitive_polygonal) flags |= SpirvShaderTranslator::kSysFlag_PrimitivePolygonal;
  if (draw_util::IsPrimitiveLine(r)) flags |= SpirvShaderTranslator::kSysFlag_PrimitiveLine;
  flags |= SurfaceFor(rb_surface_info).msaa << SpirvShaderTranslator::kSysFlag_MsaaSamples_Shift;
  if (rb_depth_info.depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
    flags |= SpirvShaderTranslator::kSysFlag_DepthFloat24;
  }
  xenos::CompareFunction alpha_test = rb_colorcontrol.alpha_test_enable
                                          ? rb_colorcontrol.alpha_func
                                          : xenos::CompareFunction::kAlways;
  flags |= uint32_t(alpha_test) << SpirvShaderTranslator::kSysFlag_AlphaPassIfLess_Shift;
  for (uint32_t i = 0; i < 4; ++i) {
    auto color_info = r.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    if (color_info.color_format == xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {
      flags |= SpirvShaderTranslator::kSysFlag_ConvertColor0ToGamma << i;
    }
    // Exponent bias, with 16_16 and 16_16_16_16 stored as -1...1 SNORM
    // instead of -32...32.
    int32_t exp_bias = color_info.color_exp_bias;
    if (color_info.color_format == xenos::ColorRenderTargetFormat::k_16_16 ||
        color_info.color_format == xenos::ColorRenderTargetFormat::k_16_16_16_16) {
      exp_bias -= 5;
    }
    float scale;
    uint32_t scale_bits = UINT32_C(0x3F800000) + (uint32_t(exp_bias) << 23);
    std::memcpy(&scale, &scale_bits, sizeof(scale));
    sc.color_exp_bias[i] = scale;
  }
  sc.flags = flags;
  sc.vertex_index_endian = xenos::Endian::kNone;
  sc.line_loop_closing_index = 0;
  sc.vertex_base_index = r.Get<int32_t>(rex::graphics::XE_GPU_REG_VGT_INDX_OFFSET);
  sc.vertex_index_min = r[rex::graphics::XE_GPU_REG_VGT_MIN_VTX_INDX];
  sc.vertex_index_max = r[rex::graphics::XE_GPU_REG_VGT_MAX_VTX_INDX];
  sc.vertex_index_reset = 0;
  sc.compute_memexport_vertex_count = 0;
  if (!pa_cl_clip_cntl.clip_disable) {
    float* planes = sc.user_clip_planes[0];
    uint32_t remaining = pa_cl_clip_cntl.ucp_ena;
    uint32_t index;
    while (rex::bit_scan_forward(remaining, &index)) {
      remaining &= ~(1u << index);
      std::memcpy(planes, &r[rex::graphics::XE_GPU_REG_PA_CL_UCP_0_X + index * 4],
                  4 * sizeof(float));
      planes += 4;
    }
  }
  for (uint32_t i = 0; i < 3; ++i) {
    sc.ndc_scale[i] = viewport.ndc_scale[i];
    sc.ndc_offset[i] = viewport.ndc_offset[i];
  }
  if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
    auto minmax = r.Get<reg::PA_SU_POINT_MINMAX>();
    auto size = r.Get<reg::PA_SU_POINT_SIZE>();
    sc.point_vertex_diameter_min = float(minmax.min_size) * (2.0f / 16.0f);
    sc.point_vertex_diameter_max = float(minmax.max_size) * (2.0f / 16.0f);
    sc.point_constant_diameter[0] = float(size.width) * (2.0f / 16.0f);
    sc.point_constant_diameter[1] = float(size.height) * (2.0f / 16.0f);
    sc.point_screen_diameter_to_ndc_radius[0] =
        1.0f / float(std::max(viewport.xy_extent[0], 1u));
    sc.point_screen_diameter_to_ndc_radius[1] =
        1.0f / float(std::max(viewport.xy_extent[1], 1u));
  }
  uint32_t remaining = used_texture_mask;
  uint32_t index;
  while (rex::bit_scan_forward(remaining, &index)) {
    remaining &= ~(1u << index);
    uint32_t& word = sc.texture_swizzled_signs[index >> 2];
    uint32_t shift = 8 * (index & 3);
    uint8_t signs = rex::graphics::texture_util::SwizzleSigns(r.GetTextureFetch(index));
    word = (word & ~(0xFFu << shift)) | (uint32_t(signs) << shift);
  }
  sc.textures_resolution_scaled = 0;
  sc.alpha_test_reference = r.Get<float>(rex::graphics::XE_GPU_REG_RB_ALPHA_REF);
  sc.alpha_to_mask = rb_colorcontrol.alpha_to_mask_enable
                         ? (rb_colorcontrol.value >> 24) | (UINT32_C(1) << 8)
                         : 0;
  (void)vs;
  (void)ps;
}

// --- Draws -------------------------------------------------------------------------------

// The render state of a draw, rebuilt only when a register that can change it
// is written (Renderer::state_generation_), the shaders or the primitive type
// change, or a render target is created. Consecutive draws almost always share
// it, and rebuilding it per draw was most of the renderer's CPU time.
bool Renderer::Impl::BuildDrawSetup(uint32_t initiator_value) {
  DrawSetup& st = setup;
  st = DrawSetup();
  st.valid = true;
  st.state_generation = owner->state_generation_;
  st.fetch_generation = owner->fetch_generation_;
  st.rt_generation = rt_generation;
  st.active_vs = active_vs;
  st.active_ps = active_ps;
  st.initiator_state = initiator_value & 0xFFFF;
  st.skip_reason = UINT32_MAX;

  const RegisterFile& r = *regs;
  const auto& properties = device->properties();
  auto fail = [&](uint32_t reason) {
    st.skip_reason = reason;
    return false;
  };

  xenos::EdramMode edram_mode = r.Get<reg::RB_MODECONTROL>().edram_mode;
  auto surface_info = r.Get<reg::RB_SURFACE_INFO>();
  if (!surface_info.surface_pitch) return fail(0);
  VulkanShader* vs = active_vs;
  if (!vs) return fail(1);
  if (!vs->is_ucode_analyzed()) vs->AnalyzeUcode(disasm);
  if (vs->memexport_eM_written()) return fail(2);
  bool primitive_polygonal = draw_util::IsPrimitivePolygonal(r);
  if (!draw_util::IsRasterizationPotentiallyDone(r, primitive_polygonal)) return fail(3);
  VulkanShader* ps = nullptr;
  if (edram_mode == xenos::EdramMode::kColorDepth && active_ps) {
    ps = active_ps;
    if (!ps->is_ucode_analyzed()) ps->AnalyzeUcode(disasm);
    if (ps->memexport_eM_written()) return fail(2);
    if (!draw_util::IsPixelShaderNeededWithRasterization(*ps, r)) ps = nullptr;
  }
  reg::RB_DEPTHCONTROL depth_control = draw_util::GetNormalizedDepthControl(r);
  uint32_t color_mask = draw_util::GetNormalizedColorMask(r, ps ? ps->writes_color_targets() : 0);

  reg::VGT_DRAW_INITIATOR initiator;
  initiator.value = initiator_value;
  xenos::PrimitiveType guest_type = initiator.prim_type;
  VkPrimitiveTopology topology;
  PipelineGeometryShader geometry = PipelineGeometryShader::kNone;
  namespace expand = gh3::native::expand;
  expand::Primitive expand_primitive = guest_type == xenos::PrimitiveType::kPointList
                                           ? expand::Primitive::kPointList
                                       : guest_type == xenos::PrimitiveType::kRectangleList
                                           ? expand::Primitive::kRectangleList
                                       : guest_type == xenos::PrimitiveType::kQuadList
                                           ? expand::Primitive::kQuadList
                                           : expand::Primitive::kOther;
  expand::Expansion expansion =
      expand::Choose(expand_primitive, properties.geometryShader != VK_FALSE);
  switch (guest_type) {
    case xenos::PrimitiveType::kPointList:
      topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      geometry = PipelineGeometryShader::kPointList;
      break;
    case xenos::PrimitiveType::kLineList:
      topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      break;
    case xenos::PrimitiveType::kLineStrip:
      topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      st.strip = true;
      break;
    case xenos::PrimitiveType::kLineLoop:
      topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      st.convert_loop = true;
      break;
    case xenos::PrimitiveType::kTriangleList:
      topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      break;
    case xenos::PrimitiveType::kTriangleStrip:
      topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
      st.strip = true;
      break;
    case xenos::PrimitiveType::kTriangleFan:
    case xenos::PrimitiveType::kPolygon:
      topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      st.convert_fan = true;
      break;
    case xenos::PrimitiveType::kRectangleList:
      topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      geometry = PipelineGeometryShader::kRectangleList;
      break;
    case xenos::PrimitiveType::kQuadList:
      topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
      geometry = PipelineGeometryShader::kQuadList;
      break;
    default:
      return fail(4);
  }
  if (expansion == expand::Expansion::kQuadToTriangles) {
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    geometry = PipelineGeometryShader::kNone;
    st.convert_quad = true;
  } else if (expansion == expand::Expansion::kVertexShaderStrip) {
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    geometry = PipelineGeometryShader::kNone;
    st.host_vs_type = guest_type == xenos::PrimitiveType::kPointList
                          ? Shader::HostVertexShaderType::kPointListAsTriangleStrip
                          : Shader::HostVertexShaderType::kRectangleListAsTriangleStrip;
  }
  if (geometry != PipelineGeometryShader::kNone && !properties.geometryShader) return fail(5);
  bool indexed = initiator.source_select != xenos::SourceSelect::kAutoIndex;
  st.reset_enabled =
      indexed && st.strip && r.Get<reg::PA_SU_SC_MODE_CNTL>().multi_prim_ib_ena;

  // Shader modifications (VulkanPipelineCache::GetCurrent*ShaderModification).
  uint32_t ps_param_gen_pos = UINT32_MAX;
  uint32_t interpolator_mask =
      ps ? (vs->writes_interpolators() &
            ps->GetInterpolatorInputMask(r.Get<reg::SQ_PROGRAM_CNTL>(),
                                         r.Get<reg::SQ_CONTEXT_MISC>(), ps_param_gen_pos))
         : 0;
  SpirvShaderTranslator::Modification vs_mod(translator->GetDefaultVertexShaderModification(
      vs->GetDynamicAddressableRegisterCount(r.Get<reg::SQ_PROGRAM_CNTL>().vs_num_reg),
      st.host_vs_type));
  vs_mod.vertex.interpolator_mask = interpolator_mask;
  auto pa_cl_clip_cntl = r.Get<reg::PA_CL_CLIP_CNTL>();
  uint32_t user_clip_planes = pa_cl_clip_cntl.clip_disable ? 0 : pa_cl_clip_cntl.ucp_ena;
  if (user_clip_planes) {
    if (pa_cl_clip_cntl.ucp_cull_only_ena && properties.shaderCullDistance) {
      vs_mod.vertex.user_clip_plane_cull = 1;
    } else if (!properties.shaderClipDistance && properties.shaderCullDistance) {
      vs_mod.vertex.user_clip_plane_cull = 1;
    } else if (!properties.shaderClipDistance) {
      user_clip_planes = 0;
    }
  }
  vs_mod.vertex.user_clip_plane_count = rex::bit_count(user_clip_planes);
  vs_mod.vertex.point_ps_ucp_mode = pa_cl_clip_cntl.ps_ucp_mode;
  if (st.host_vs_type == Shader::HostVertexShaderType::kPointListAsTriangleStrip) {
    // The point coordinates for the pixel shader, not the size
    // (VulkanPipelineCache::GetCurrentVertexShaderModification).
    vs_mod.vertex.output_point_parameters =
        uint32_t(ps && ps_param_gen_pos < xenos::kMaxInterpolators);
  } else {
    vs_mod.vertex.output_point_parameters =
        uint32_t((vs->writes_point_size_edge_flag_kill_vertex() & 0b001) &&
                 guest_type == xenos::PrimitiveType::kPointList);
  }
  vs_mod.vertex.vertex_kill_and =
      uint32_t(properties.shaderCullDistance &&
               (vs->writes_point_size_edge_flag_kill_vertex() & 0b100) &&
               !pa_cl_clip_cntl.vtx_kill_or);
  SpirvShaderTranslator::Modification ps_mod(0);
  if (ps) {
    ps_mod = SpirvShaderTranslator::Modification(translator->GetDefaultPixelShaderModification(
        ps->GetDynamicAddressableRegisterCount(r.Get<reg::SQ_PROGRAM_CNTL>().ps_num_reg)));
    ps_mod.pixel.interpolator_mask = interpolator_mask;
    ps_mod.pixel.interpolators_centroid =
        interpolator_mask &
        ~xenos::GetInterpolatorSamplingPattern(surface_info.msaa_samples,
                                               r.Get<reg::SQ_CONTEXT_MISC>().sc_sample_cntl,
                                               r.Get<reg::SQ_INTERPOLATOR_CNTL>().sampling_pattern);
    if (ps_param_gen_pos < xenos::kMaxInterpolators) {
      ps_mod.pixel.param_gen_enable = 1;
      ps_mod.pixel.param_gen_interpolator = ps_param_gen_pos;
      ps_mod.pixel.param_gen_point = uint32_t(guest_type == xenos::PrimitiveType::kPointList);
    }
    using DepthStencilMode = SpirvShaderTranslator::Modification::DepthStencilMode;
    if (ps->implicit_early_z_write_allowed() &&
        (!ps->writes_color_target(0) ||
         !draw_util::DoesCoverageDependOnAlpha(r.Get<reg::RB_COLORCONTROL>()))) {
      ps_mod.pixel.depth_stencil_mode = DepthStencilMode::kEarlyHint;
    } else {
      ps_mod.pixel.depth_stencil_mode = DepthStencilMode::kNoModifiers;
    }
  }
  auto* vs_translation =
      static_cast<VulkanShader::VulkanTranslation*>(vs->GetOrCreateTranslation(vs_mod.value));
  auto* ps_translation =
      ps ? static_cast<VulkanShader::VulkanTranslation*>(ps->GetOrCreateTranslation(ps_mod.value))
         : nullptr;
  {
    bool vs_pending = false, ps_pending = false;
    bool vs_ok = EnsureTranslated(vs_translation, &vs_pending);
    bool ps_ok = !ps_translation || EnsureTranslated(ps_translation, &ps_pending);
    if (!vs_ok || !ps_ok) {
      // Still being translated: look again on the next draw.
      if (vs_pending || ps_pending) st.valid = false;
      return fail(6);
    }
  }

  // Render targets: the color targets the draw writes and the depth buffer
  // if it is tested or written.
  bool depth_used = depth_control.z_enable || depth_control.stencil_enable;
  // The guest's sample count sizes the EDRAM below; the host's keys the targets.
  uint32_t guest_msaa = uint32_t(surface_info.msaa_samples);
  const edram::Surface surface = SurfaceFor(surface_info);
  uint32_t msaa = surface.msaa;
  draw_util::GetScissor(r, st.scissor);
  st.scissor.offset[0] *= surface.scale_x;
  st.scissor.extent[0] *= surface.scale_x;
  st.scissor.offset[1] *= surface.scale_y;
  st.scissor.extent[1] *= surface.scale_y;
  draw_util::GetHostViewportInfo(r, surface.scale_x, surface.scale_y, false, properties.maxViewportDimensions[0],
                                 properties.maxViewportDimensions[1], true, depth_control, false,
                                 true, ps && ps->writes_depth(), st.viewport);
  // Rows the draw can touch: the viewport, cut by the scissor. The scissor
  // alone is often the 8192-pixel default and made every target 2048 tall.
  uint32_t needed_height = st.scissor.offset[1] + st.scissor.extent[1];
  if (st.viewport.xy_extent[1]) {
    needed_height =
        std::min(needed_height, st.viewport.xy_offset[1] + st.viewport.xy_extent[1]);
  }
  // T16: o alvo 1280 com profundidade vai para a imagem escalada (viewport/scissor escalados mais abaixo).
  st.scene_scaled = scene::Enabled(scene_scale) &&
                    scene::Qualifies(surface.pitch, depth_used, msaa, AliasMsaa(guest_msaa));
  st.needed_height = st.scene_scaled ? scene::ScaledSize(scene_scale, needed_height) : needed_height;
  PassTargets& targets = st.targets;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!((color_mask >> (4 * i)) & 0xF)) continue;
    auto color_info = r.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    RenderTargetKey key = {};
    key.base_tiles = color_info.color_base;
    key.pitch = surface.pitch;
    key.format = uint32_t(color_info.color_format);
    key.msaa = msaa;
    key.alias_msaa = AliasMsaa(guest_msaa);
    key.scene_scaled = st.scene_scaled;
    // The EDRAM below the base limits how tall the target can be.
    uint32_t pitch_tiles = (surface_info.surface_pitch * (guest_msaa >= 2 ? 2 : 1) + 79) / 80 *
                           (IsColorFormat64bpp(color_info.color_format) ? 2 : 1);
    uint32_t rows = pitch_tiles ? (2048 - std::min(key.base_tiles, 2047u)) / pitch_tiles *
                                      (guest_msaa >= 1 ? 8 : 16) * surface.scale_y
                                : 2048;
    uint32_t color_height = std::min(needed_height, rows);
    // A 4x alias with the default 8192 scissor means "the whole EDRAM": the game uses it to
    // clear the 1x target at pitch 2P. Half the EDRAM rows covers that target (and the stage's
    // explicit 720) without making every pass pay for 2048.
    if (alias_cap && msaa_as_samples && guest_msaa >= 2 && surface.scale_y > 1 &&
        st.scissor.extent[1] >= 8192) {
      color_height = std::min(color_height, rows / surface.scale_y);
    }
    targets.color[i] = GetRenderTarget(key, color_height);
  }
  if (depth_used) {
    auto depth_info = r.Get<reg::RB_DEPTH_INFO>();
    RenderTargetKey key = {};
    key.base_tiles = depth_info.depth_base;
    key.pitch = surface.pitch;
    key.format = uint32_t(depth_info.depth_format);
    key.msaa = msaa;
    key.alias_msaa = AliasMsaa(guest_msaa);
    key.scene_scaled = st.scene_scaled;
    key.is_depth = 1;
    uint32_t pitch_tiles = (surface_info.surface_pitch * (guest_msaa >= 2 ? 2 : 1) + 79) / 80;
    uint32_t rows = pitch_tiles ? (2048 - std::min(key.base_tiles, 2047u)) / pitch_tiles *
                                      (guest_msaa >= 1 ? 8 : 16) * surface.scale_y
                                : 2048;
    targets.depth = GetRenderTarget(key, std::min(needed_height, rows));
  }
  // T24: os draws 1x com scissor explicito revelam a altura real do alvo (180-720). Guardar no
  // alvo para que os draws 4x com scissor default (8192) nao paguem 2048 linhas na proxima volta.
  if (guest_msaa == 0 && st.scissor.extent[1] < 8192) {
    for (RenderTarget* rt : targets.color) {
      if (rt) rt->known_1x_height = std::max(rt->known_1x_height, needed_height);
    }
    if (targets.depth) {
      targets.depth->known_1x_height = std::max(targets.depth->known_1x_height, needed_height);
    }
  }
  // O cap do render area dos draws 4x: no modo samples-as-pixels o alvo e' 1x (altura cheia);
  // no alias do T15 a imagem 4x so precisa da altura do 1x dividida pelo empilhamento (2).
  // T24/F1.2: vale para 1x tambem. Um draw com o scissor default (8192) cobre "o EDRAM inteiro";
  // a altura real do alvo e' a que os draws explicitos ja revelaram (known_1x_height). No alias do
  // T15 a imagem 4x so precisa dessa altura dividida pelo empilhamento (2).
  if (st.scissor.extent[1] >= 8192 && targets.color[0]) {
    uint32_t useful = 0;
    if (guest_msaa == 0 || msaa_as_samples) {
      useful = targets.color[0]->known_1x_height;
    } else {
      RenderTargetKey full_key = targets.color[0]->key;
      full_key.alias_msaa = 0;
      auto fit = render_targets.find(full_key);
      if (fit != render_targets.end()) useful = fit->second->known_1x_height;
      if (useful) useful = std::max(16u, useful / 2);
    }
    if (useful) st.needed_height = std::min(st.needed_height, useful);
  }
  // Creating a render target above bumped rt_generation; this setup is built
  // against the new ones.
  st.rt_generation = rt_generation;
  if (targets.Empty()) return fail(7);

  // Layout.
  st.vs = vs;
  st.ps = ps;
  const VulkanShader* stage_shaders[2] = {vs, ps};
  for (uint32_t stage = 0; stage < 2; ++stage) {
    const VulkanShader* shader = stage_shaders[stage];
    if (!shader) continue;
    st.counts[stage * 2] = uint32_t(shader->GetTextureBindingsAfterTranslation().size());
    st.counts[stage * 2 + 1] = uint32_t(shader->GetSamplerBindingsAfterTranslation().size());
  }
  st.layout = GetPipelineLayout(st.counts[0], st.counts[1], st.counts[2], st.counts[3]);
  if (!st.layout) return fail(10);

  // Pipeline.
  PipelineKey key = {};
  key.vs_hash = vs->ucode_data_hash();
  key.vs_modification = vs_mod.value;
  key.ps_hash = ps ? ps->ucode_data_hash() : 0;
  key.ps_modification = ps ? ps_mod.value : 0;
  for (uint32_t i = 0; i < 4; ++i) {
    key.color_formats[i] = targets.color[i] ? uint32_t(targets.color[i]->format) : 0;
  }
  key.depth_format = targets.depth ? uint32_t(targets.depth->format) : 0;
  key.samples = 1u << msaa;
  key.topology = uint32_t(topology);
  // The expanded strips are separated by restarts.
  key.primitive_restart =
      st.reset_enabled || st.host_vs_type != Shader::HostVertexShaderType::kVertex ? 1 : 0;
  auto sc_mode = r.Get<reg::PA_SU_SC_MODE_CNTL>();
  if (primitive_polygonal) {
    key.cull_mode = (sc_mode.cull_front ? VK_CULL_MODE_FRONT_BIT : 0) |
                    (sc_mode.cull_back ? VK_CULL_MODE_BACK_BIT : 0);
    key.front_face_clockwise = sc_mode.face != 0;
    if (properties.fillModeNonSolid &&
        sc_mode.poly_mode == xenos::PolygonModeEnable::kDualMode) {
      xenos::PolygonType type = xenos::PolygonType::kTriangles;
      if (!sc_mode.cull_front) type = std::min(type, sc_mode.polymode_front_ptype);
      if (!sc_mode.cull_back) type = std::min(type, sc_mode.polymode_back_ptype);
      key.polygon_line = type != xenos::PolygonType::kTriangles;
    }
  }
  key.depth_clamp = properties.depthClamp && pa_cl_clip_cntl.clip_disable;
  if (targets.depth) {
    if (depth_control.z_enable) {
      key.depth_test = 1;
      key.depth_write = depth_control.z_write_enable;
      key.depth_compare = uint32_t(VK_COMPARE_OP_NEVER) + uint32_t(depth_control.zfunc);
    }
    if (depth_control.stencil_enable) {
      key.stencil_test = 1;
      key.stencil_front[0] = uint32_t(VK_STENCIL_OP_KEEP) + uint32_t(depth_control.stencilfail);
      key.stencil_front[1] = uint32_t(VK_STENCIL_OP_KEEP) + uint32_t(depth_control.stencilzpass);
      key.stencil_front[2] = uint32_t(VK_STENCIL_OP_KEEP) + uint32_t(depth_control.stencilzfail);
      key.stencil_front[3] = uint32_t(VK_COMPARE_OP_NEVER) + uint32_t(depth_control.stencilfunc);
      if (primitive_polygonal && depth_control.backface_enable) {
        key.stencil_back[0] =
            uint32_t(VK_STENCIL_OP_KEEP) + uint32_t(depth_control.stencilfail_bf);
        key.stencil_back[1] =
            uint32_t(VK_STENCIL_OP_KEEP) + uint32_t(depth_control.stencilzpass_bf);
        key.stencil_back[2] =
            uint32_t(VK_STENCIL_OP_KEEP) + uint32_t(depth_control.stencilzfail_bf);
        key.stencil_back[3] =
            uint32_t(VK_COMPARE_OP_NEVER) + uint32_t(depth_control.stencilfunc_bf);
      } else {
        std::memcpy(key.stencil_back, key.stencil_front, sizeof(key.stencil_back));
      }
    }
  }
  for (uint32_t i = 0; i < 4; ++i) {
    uint32_t mask = (color_mask >> (4 * i)) & 0xF;
    if (!targets.color[i]) mask = 0;
    key.write_masks |= mask << (4 * i);
    uint32_t blend = r[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1FFF1FFF;
    key.blend[i] = mask ? blend : 0x00010001;
  }
  if (specialize_signs) {
    for (const VulkanShader* shader : stage_shaders) {
      if (!shader) continue;
      for (const auto& binding : shader->texture_bindings()) {
        uint32_t fetch = binding.fetch_constant;
        uint8_t signs = rex::graphics::texture_util::SwizzleSigns(r.GetTextureFetch(fetch));
        uint32_t& word = key.texture_signs[fetch >> 2];
        uint32_t shift = 8 * (fetch & 3);
        word = (word & ~(0xFFu << shift)) | (uint32_t(signs) << shift);
      }
    }
  }
  key.layout = (uint64_t(st.counts[0]) << 48) | (uint64_t(st.counts[1]) << 32) |
               (uint64_t(st.counts[2]) << 16) | st.counts[3];
  VkShaderModule geometry_module = VK_NULL_HANDLE;
  if (geometry != PipelineGeometryShader::kNone) {
    GeometryShaderKey gs_key;
    gs_key.type = geometry;
    gs_key.interpolator_count = rex::bit_count(vs_mod.vertex.interpolator_mask);
    gs_key.user_clip_plane_count = vs_mod.vertex.user_clip_plane_count;
    gs_key.user_clip_plane_cull = vs_mod.vertex.user_clip_plane_cull;
    gs_key.has_vertex_kill_and = vs_mod.vertex.vertex_kill_and;
    gs_key.has_point_size = vs_mod.vertex.output_point_parameters;
    gs_key.has_point_coordinates = ps ? ps_mod.pixel.param_gen_point : 0;
    gs_key.point_ps_ucp_mode = vs_mod.vertex.point_ps_ucp_mode;
    key.geometry_shader = gs_key.key;
    auto it = geometry_shaders.find(gs_key.key);
    if (it == geometry_shaders.end()) {
      it = geometry_shaders.emplace(gs_key.key, CreateGeometryShader(device, gs_key)).first;
    }
    geometry_module = it->second;
    if (!geometry_module) return fail(12);
  }
  bool pending = false;
  st.pipeline = GetPipeline(key, vs_translation, ps_translation, geometry_module,
                            st.layout->layout, &pending);
  if (!st.pipeline) {
    // A pipeline being compiled: look again on the next draw.
    if (pending) st.valid = false;
    return fail(13);
  }

  // Dynamic state (VulkanCommandProcessor::UpdateDynamicState): all of it
  // comes from state registers.
  st.primitive_polygonal = primitive_polygonal;
  st.depth_control = depth_control;
  if (st.viewport.xy_extent[0] && st.viewport.xy_extent[1]) {
    st.vp.x = float(st.viewport.xy_offset[0]);
    st.vp.y = float(st.viewport.xy_offset[1]);
    st.vp.width = float(st.viewport.xy_extent[0]);
    st.vp.height = float(st.viewport.xy_extent[1]);
  } else {
    st.vp.x = -1.0f;
    st.vp.y = -1.0f;
    st.vp.width = 1.0f;
    st.vp.height = 1.0f;
  }
  if (st.scene_scaled) {
    if (st.viewport.xy_extent[0] && st.viewport.xy_extent[1]) {
      st.vp.x *= scene_scale;
      st.vp.y *= scene_scale;
      st.vp.width *= scene_scale;
      st.vp.height *= scene_scale;
    }
    for (int axis = 0; axis < 2; ++axis) {
      scene::ScaledSpan span =
          scene::ScaleSpan(scene_scale, st.scissor.offset[axis], st.scissor.extent[axis]);
      st.scissor.offset[axis] = span.offset;
      st.scissor.extent[axis] = span.extent;
    }
    if (!scene_logged) {
      scene_logged = true;
      REXLOG_INFO("[native] scene scale {:g}: {}x{}", scene_scale,
                  scene::ScaledSize(scene_scale, scene::kScenePitch),
                  scene::ScaledSize(scene_scale, 720));
    }
  }
  st.vp.minDepth = st.viewport.z_min;
  st.vp.maxDepth = st.viewport.z_max;
  draw_util::GetPreferredFacePolygonOffset(r, primitive_polygonal, st.bias_slope,
                                           st.bias_constant);
  st.bias_constant *= r.Get<reg::RB_DEPTH_INFO>().depth_format ==
                              xenos::DepthRenderTargetFormat::kD24S8
                          ? draw_util::kD3D10PolygonOffsetFactorUnorm24
                          : draw_util::kD3D10PolygonOffsetFactorFloat24;
  st.bias_slope *= xenos::kPolygonOffsetScaleSubpixelUnit;
  st.blend_constants[0] = r.Get<float>(rex::graphics::XE_GPU_REG_RB_BLEND_RED);
  st.blend_constants[1] = r.Get<float>(rex::graphics::XE_GPU_REG_RB_BLEND_GREEN);
  st.blend_constants[2] = r.Get<float>(rex::graphics::XE_GPU_REG_RB_BLEND_BLUE);
  st.blend_constants[3] = r.Get<float>(rex::graphics::XE_GPU_REG_RB_BLEND_ALPHA);
  auto front = r.Get<reg::RB_STENCILREFMASK>();
  auto back = front;
  if (primitive_polygonal && depth_control.backface_enable) {
    back = r.Get<reg::RB_STENCILREFMASK>(rex::graphics::XE_GPU_REG_RB_STENCILREFMASK_BF);
  }
  uint32_t stencil[6] = {front.stencilmask,      back.stencilmask, front.stencilwritemask,
                         back.stencilwritemask, front.stencilref, back.stencilref};
  std::memcpy(st.stencil, stencil, sizeof(stencil));
  return true;
}

void Renderer::Impl::Draw(const uint32_t* immediate_indices, uint32_t immediate_index_words) {
  const RegisterFile& r = *regs;
  // The reasons, in order: 0 no surface pitch, 1 no vertex shader, 2 memory
  // export, 3 no rasterization, 4 primitive type, 5 no geometry shaders,
  // 6 translation, 7 no render target, 8 index data, 9 stream full,
  // 10 layout, 11 descriptors, 12 geometry shader, 13 pipeline, 14 pass,
  // 15 immediate indices.
  auto skip = [&](uint32_t reason) {
    ++stats().draws_skipped;
    ++stats().skipped_by_reason[reason & 15];
  };

  auto initiator = r.Get<reg::VGT_DRAW_INITIATOR>();
  uint32_t guest_count = initiator.num_indices;
  if (!guest_count) {
    return;
  }
  // The render state: the same as the previous draw (nothing changed), or
  // one seen before (looked up by the hash of the state registers, the
  // shaders and the primitive setup), or built now.
  if (!(setup.valid && setup.state_generation == owner->state_generation_ &&
        setup.active_vs == active_vs && setup.active_ps == active_ps &&
        setup.initiator_state == (initiator.value & 0xFFFF) &&
        setup.rt_generation == rt_generation &&
        (!specialize_signs || setup.fetch_generation == owner->fetch_generation_))) {
    if (setup_cache_rt_generation != rt_generation) {
      // Setups hold render target pointers.
      setup_cache.clear();
      setup_cache_rt_generation = rt_generation;
    }
    uint64_t key = owner->state_hash_ ^
                   Renderer::Mix(0x10000, uint32_t(reinterpret_cast<uintptr_t>(active_vs))) ^
                   Renderer::Mix(0x10001, uint32_t(reinterpret_cast<uintptr_t>(active_vs) >> 32)) ^
                   Renderer::Mix(0x10002, uint32_t(reinterpret_cast<uintptr_t>(active_ps))) ^
                   Renderer::Mix(0x10003, uint32_t(reinterpret_cast<uintptr_t>(active_ps) >> 32)) ^
                   Renderer::Mix(0x10004, (initiator.value & 0xFFFF) | 0x80000000u);
    if (specialize_signs) {
      // The pipeline depends on the signs of the textures the shaders read.
      for (VulkanShader* shader : {active_vs, active_ps}) {
        if (!shader || !shader->is_ucode_analyzed()) continue;
        for (const auto& binding : shader->texture_bindings()) {
          key ^= Renderer::Mix(
              0x10010 + binding.fetch_constant,
              0x100u | rex::graphics::texture_util::SwizzleSigns(
                           r.GetTextureFetch(binding.fetch_constant)));
        }
      }
    }
    auto found = setup_cache.find(key);
    if (found != setup_cache.end() && found->second.active_vs == active_vs &&
        found->second.active_ps == active_ps &&
        found->second.initiator_state == (initiator.value & 0xFFFF)) {
      setup = found->second;
      setup.state_generation = owner->state_generation_;
      setup.fetch_generation = owner->fetch_generation_;
      ++setup_generation;
    } else {
      ++stats().setups_built;
      ++setup_generation;
      BuildDrawSetup(initiator.value);
      if (setup.valid) {
        if (setup_cache.size() >= 4096) setup_cache.clear();
        setup_cache[key] = setup;
      }
    }
  }
  DrawSetup& st = setup;
  if (st.skip_reason != UINT32_MAX) {
    return skip(st.skip_reason);
  }
  VulkanShader* vs = st.vs;
  VulkanShader* ps = st.ps;
  const PassTargets& targets = st.targets;
  if (const RenderTarget* c0 = targets.color[0]) {
    // Experimentos de corte de passe (o perfil mostrou ~5 ms cada): o alvo fica com o conteudo
    // anterior, o que serve para medir o teto de GPU e para um modo de desempenho opcional.
    if (REXCVAR_GET(gh3_native_skip_shadows) && c0->key.base_tiles == 0 && c0->key.pitch == 160) {
      return skip(15);
    }
    if (REXCVAR_GET(gh3_native_skip_bloom) && c0->key.base_tiles == 1440 && c0->key.pitch == 320) {
      return skip(15);
    }
  }
  const PipelineLayout* layout = st.layout;

  // Indices.
  BeginRecording();
  auto dma_size = r.Get<reg::VGT_DMA_SIZE>();
  bool indexed = initiator.source_select != xenos::SourceSelect::kAutoIndex;
  bool index32 = initiator.index_size == xenos::IndexFormat::kInt32;
  bool reset_enabled = st.reset_enabled;
  bool convert_fan = st.convert_fan, convert_loop = st.convert_loop;
  bool convert_quad = st.convert_quad;
  bool vs_strip = st.host_vs_type != Shader::HostVertexShaderType::kVertex;
  uint32_t reset_index = r.Get<reg::VGT_MULTI_PRIM_IB_RESET_INDX>().reset_indx;
  const uint8_t* guest_indices = nullptr;
  xenos::Endian index_endian = dma_size.swap_mode;
  uint32_t dma_base = 0;
  if (indexed) {
    if (initiator.source_select == xenos::SourceSelect::kDMA) {
      dma_base = r[rex::graphics::XE_GPU_REG_VGT_DMA_BASE] & 0x1FFFFFFF;
      uint32_t bytes = guest_count * (index32 ? 4 : 2);
      if (uint64_t(dma_base) + bytes > 0x20000000u) {
        return skip(8);
      }
      guest_indices = memory->TranslatePhysical<const uint8_t*>(dma_base);
    } else if (immediate_indices) {
      guest_indices = reinterpret_cast<const uint8_t*>(immediate_indices);
      index_endian = xenos::Endian::kNone;
      guest_count = std::min(guest_count, immediate_index_words * (index32 ? 1u : 2u));
    } else {
      return skip(15);
    }
  }
  auto read_index = [&](uint32_t i) -> uint32_t {
    if (!indexed) {
      return i;
    }
    if (index32) {
      uint32_t v;
      std::memcpy(&v, guest_indices + size_t(i) * 4, 4);
      return xenos::GpuSwap(v, index_endian);
    }
    uint16_t v;
    std::memcpy(&v, guest_indices + size_t(i) * 2, 2);
    return xenos::GpuSwap(v, index_endian);
  };
  uint32_t draw_count = guest_count;
  bool host_indexed = indexed || convert_fan || convert_loop || convert_quad || vs_strip;
  VkIndexType index_type = index32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
  VkDeviceSize index_offset = 0;
  // Points and rectangles expanded in the vertex shader: the host draws
  // two-triangle strips, and the shader reads the guest indices (if any)
  // itself, from their copy in the stream (the shared memory binding).
  bool index_load = false;
  uint32_t index_load_address = 0;
  xenos::Endian index_load_endian = xenos::Endian::kNone;
  if (vs_strip) {
    uint32_t primitives = gh3::native::expand::ExpandedPrimitiveCount(
        st.host_vs_type == Shader::HostVertexShaderType::kRectangleListAsTriangleStrip
            ? gh3::native::expand::Primitive::kRectangleList
            : gh3::native::expand::Primitive::kPointList,
        guest_count);
    if (!primitives) return;
    if (indexed) {
      uint32_t bytes = guest_count * (index32 ? 4 : 2);
      VkDeviceSize load_offset;
      uint8_t* copy = Allocate(bytes, 4, load_offset);
      if (!copy) return skip(9);
      std::memcpy(copy, guest_indices, bytes);
      index_load = true;
      index_load_address = uint32_t(load_offset);
      // VulkanCommandProcessor::UpdateSystemConstantValues: 16-bit indices
      // only swap within 16 bits.
      index_load_endian = index_endian;
      if (!index32 && index_load_endian != xenos::Endian::kNone &&
          index_load_endian != xenos::Endian::k8in16) {
        index_load_endian = index_load_endian == xenos::Endian::k8in32 ? xenos::Endian::k8in16
                                                                       : xenos::Endian::kNone;
      }
    }
    draw_count = gh3::native::expand::TwoTriangleStripIndexCount(primitives);
    index_type = VK_INDEX_TYPE_UINT32;
    uint32_t* strips = reinterpret_cast<uint32_t*>(
        Allocate(VkDeviceSize(draw_count) * 4, 4, index_offset));
    if (!strips) return skip(9);
    gh3::native::expand::WriteTwoTriangleStrips(primitives, strips);
  } else if (host_indexed) {
    if (convert_fan) {
      if (guest_count < 3) return;
      draw_count = (guest_count - 2) * 3;
    } else if (convert_loop) {
      if (guest_count < 2) return;
      draw_count = guest_count + 1;
    } else if (convert_quad) {
      draw_count = gh3::native::expand::QuadListIndexCount(guest_count);
      if (!draw_count) return;
    }
    if (!indexed) {
      index_type = VK_INDEX_TYPE_UINT32;
    }
    uint32_t index_size = index_type == VK_INDEX_TYPE_UINT32 ? 4 : 2;
    // Index buffers in guest memory are usually static: reuse the converted
    // copy while the guest data is the same (checked once per frame).
    bool reused = false;
    VertexRangeKey index_key = {};
    if (dma_base && !convert_fan && !convert_loop && !convert_quad) {
      uint32_t bytes = guest_count * (index32 ? 4 : 2);
      VertexHash& known = vertex_hashes[(uint64_t(dma_base) << 32) | bytes];
      RangeHash(known, guest_indices, bytes, dma_base);
      // The conversion also depends on the byte order and the reset index.
      index_key = {dma_base, bytes | 0x80000000u,
                   known.hash ^ (uint64_t(index_endian) << 60) ^
                       (reset_enabled ? (uint64_t(reset_index) << 8) | 1 : 0)};
      uint64_t found = FindRange(index_key);
      if (found != UINT64_MAX && stream_head - found < kStreamSize / 2) {
        index_offset = VkDeviceSize(found % kStreamSize);
        current().stream_oldest = std::min(current().stream_oldest, found);
        reused = true;
      }
    }
    if (!reused) {
      uint8_t* out = Allocate(VkDeviceSize(draw_count) * index_size, 4, index_offset);
      if (!out) {
        return skip(9);
      }
      if (index_key.address) {
        AddRange(index_key, stream_head - VkDeviceSize(draw_count) * index_size);
      }
      uint32_t host_reset = index_size == 4 ? 0xFFFFFFFFu : 0xFFFFu;
      auto put = [&](uint32_t i, uint32_t v) {
        if (reset_enabled && v == reset_index) {
          v = host_reset;
        }
        if (index_size == 4) {
          std::memcpy(out + size_t(i) * 4, &v, 4);
        } else {
          uint16_t h = uint16_t(v);
          std::memcpy(out + size_t(i) * 2, &h, 2);
        }
      };
      if (convert_fan) {
        uint32_t first = read_index(0);
        for (uint32_t t = 0; t + 2 < guest_count; ++t) {
          put(t * 3 + 0, first);
          put(t * 3 + 1, read_index(t + 1));
          put(t * 3 + 2, read_index(t + 2));
        }
      } else if (convert_loop) {
        for (uint32_t i = 0; i < guest_count; ++i) put(i, read_index(i));
        put(guest_count, read_index(0));
      } else if (convert_quad) {
        gh3::native::expand::QuadListToTriangleList(guest_count, read_index, put);
      } else if (!reset_enabled && index_endian == xenos::Endian::kNone) {
        std::memcpy(out, guest_indices, size_t(draw_count) * index_size);
      } else if (!reset_enabled && index_size == 2 && index_endian == xenos::Endian::k8in16) {
        rex::memory::copy_and_swap_16_unaligned(out, guest_indices, draw_count);
      } else if (!reset_enabled && index_size == 4 && index_endian == xenos::Endian::k8in32) {
        rex::memory::copy_and_swap_32_unaligned(out, guest_indices, draw_count);
      } else {
        for (uint32_t i = 0; i < draw_count; ++i) put(i, read_index(i));
      }
    }
  }

  // Fetch constants: the vertex buffers the shader reads are pointed at their
  // copies in the stream. Only slots written since they were last resolved,
  // or not yet resolved this frame, are looked at again.
  const Shader::ConstantRegisterMap& vs_constants = vs->constant_register_map();
  if (fetch_words_frame != frame || fetch_words_submission != submission_number) {
    // New frame: vertex data is checked for changes again. New submission:
    // copies in older submissions may be reused, but the bookkeeping is
    // simpler starting over.
    fetch_resolved_frame.fill(0);
    fetch_words_frame = frame;
    fetch_words_submission = submission_number;
  }
  uint32_t dirty_slots = owner->vertex_fetch_dirty_;
  owner->vertex_fetch_dirty_ = 0;
  if (dirty_slots) {
    for (uint32_t i = 0; i < 96; ++i) {
      if (dirty_slots & (1u << (i / 3))) fetch_resolved_frame[i] = 0;
    }
    // Texture words are copied as they are; vertex words get rewritten below.
    for (uint32_t slot = 0; slot < 32; ++slot) {
      if (dirty_slots & (1u << slot)) {
        std::memcpy(&fetch_words[slot * 6],
                    &r[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + slot * 6],
                    6 * sizeof(uint32_t));
      }
    }
  }
  for (uint32_t word = 0; word < 3; ++word) {
    uint32_t bits = vs_constants.vertex_fetch_bitmap[word];
    uint32_t bit;
    while (rex::bit_scan_forward(bits, &bit)) {
      bits &= ~(1u << bit);
      uint32_t fetch_index = word * 32 + bit;
      if (fetch_resolved_frame[fetch_index] == frame) {
        continue;
      }
      fetch_resolved_frame[fetch_index] = frame;
      uint32_t guest_dword0 =
          r[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + fetch_index * 2];
      uint32_t dword1 =
          r[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + fetch_index * 2 + 1];
      uint32_t& dword0 = fetch_words[fetch_index * 2];
      dword0 = guest_dword0;
      uint32_t address = guest_dword0 & 0x1FFFFFFC;
      uint32_t size = (dword1 >> 2) & 0xFFFFFF;  // in words
      uint32_t bytes = std::min(size * 4, 16u << 20);
      if (!bytes || uint64_t(address) + bytes > 0x20000000u) {
        continue;
      }
      const uint8_t* source = memory->TranslatePhysical<const uint8_t*>(address);
      VertexHash& known = vertex_hashes[(uint64_t(address) << 32) | bytes];
      RangeHash(known, source, bytes, address);
      VertexRangeKey range_key = {address, bytes, known.hash};
      uint64_t found = FindRange(range_key);
      VkDeviceSize range_offset;
      if (found != UINT64_MAX && stream_head - found < kStreamSize / 2) {
        range_offset = VkDeviceSize(found % kStreamSize);
        current().stream_oldest = std::min(current().stream_oldest, found);
      } else {
        uint8_t* out = Allocate(bytes, 16, range_offset);
        if (!out) continue;
        std::memcpy(out, source, bytes);
        stats().vertex_bytes_copied += bytes;
        AddRange(range_key, stream_head - bytes);
      }
      dword0 = (guest_dword0 & 0x3) | uint32_t(range_offset);
    }
  }
  if (vertex_ranges.size() > 65536) {
    vertex_ranges.clear();
  }
  if (vertex_hashes.size() > 65536) {
    vertex_hashes.clear();
  }
  VkDeviceSize fetch_offset;
  if (last_fetch.submission == submission_number &&
      !std::memcmp(last_fetch_words, fetch_words.data(), sizeof(last_fetch_words))) {
    fetch_offset = last_fetch.offset;
  } else {
    uint8_t* fetch_out = Allocate(kFetchConstantsBytes, ubo_alignment, fetch_offset);
    if (!fetch_out) return skip(9);
    std::memcpy(fetch_out, fetch_words.data(), kFetchConstantsBytes);
    std::memcpy(last_fetch_words, fetch_words.data(), sizeof(last_fetch_words));
    last_fetch = {submission_number, 0, nullptr, frame, fetch_offset};
  }

  auto write_float_constants = [&](const Shader::ConstantRegisterMap& map, uint32_t base_register,
                                   VkDeviceSize& offset) -> bool {
    // Only the constants the shader reads: the binding's fixed range reads on
    // into whatever follows (never used), which the stream's guard keeps
    // inside the buffer. Allocating the full 4 KB per draw filled the stream
    // and made it wait for the GPU.
    VkDeviceSize bytes = VkDeviceSize(std::max(map.float_count, 1u)) * 4 * sizeof(float);
    uint8_t* out = Allocate(bytes, ubo_alignment, offset);
    if (!out) return false;
    for (uint32_t i = 0; i < 4; ++i) {
      uint64_t bits = map.float_bitmap[i];
      uint32_t bit;
      while (rex::bit_scan_forward(bits, &bit)) {
        bits &= ~(uint64_t(1) << bit);
        std::memcpy(out, &r[base_register + (i << 8) + (bit << 2)], 4 * sizeof(float));
        out += 4 * sizeof(float);
      }
    }
    return true;
  };
  VkDeviceSize vs_float_offset, ps_float_offset, bool_loop_offset, system_offset;
  if (Reusable(last_vs_float, owner->vs_float_generation_, vs)) {
    vs_float_offset = last_vs_float.offset;
  } else {
    if (!write_float_constants(vs_constants, rex::graphics::XE_GPU_REG_SHADER_CONSTANT_000_X,
                               vs_float_offset)) {
      return skip(9);
    }
    last_vs_float = {submission_number, owner->vs_float_generation_, vs, frame, vs_float_offset};
  }
  if (ps) {
    if (Reusable(last_ps_float, owner->ps_float_generation_, ps)) {
      ps_float_offset = last_ps_float.offset;
    } else {
      if (!write_float_constants(ps->constant_register_map(),
                                 rex::graphics::XE_GPU_REG_SHADER_CONSTANT_256_X,
                                 ps_float_offset)) {
        return skip(9);
      }
      last_ps_float = {submission_number, owner->ps_float_generation_, ps, frame,
                       ps_float_offset};
    }
  } else {
    ps_float_offset = vs_float_offset;
  }
  if (Reusable(last_bool_loop, owner->bool_loop_generation_, nullptr)) {
    bool_loop_offset = last_bool_loop.offset;
  } else {
    uint8_t* bool_loop = Allocate(kBoolLoopConstantsBytes, ubo_alignment, bool_loop_offset);
    if (!bool_loop) return skip(9);
    std::memcpy(bool_loop, &r[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031],
                kBoolLoopConstantsBytes);
    last_bool_loop = {submission_number, owner->bool_loop_generation_, nullptr, frame,
                      bool_loop_offset};
  }

  // Textures and samplers, reused while no fetch slot they come from was
  // written, within one frame (content changes are looked for once a frame)
  // and one submission (uploads go in its setup command buffer).
  upload.setup_commands = current().setup;
  upload.submission = submission_number;
  const VulkanShader* stage_shaders[2] = {vs, ps};
  // Keyed by the shaders and the fetch constants of the slots they read,
  // valid for a frame: materials repeat across the draws of a frame, and a hit
  // skips every texture and sampler lookup.
  uint32_t slot_mask = 0;
  for (const VulkanShader* shader : stage_shaders) {
    if (!shader) continue;
    for (const auto& b : shader->GetTextureBindingsAfterTranslation()) {
      slot_mask |= 1u << b.fetch_constant;
    }
    for (const auto& b : shader->GetSamplerBindingsAfterTranslation()) {
      slot_mask |= 1u << b.fetch_constant;
    }
  }
  uint64_t tb_key = XXH3_64bits_withSeed(&layout, sizeof(layout),
                                         uint64_t(reinterpret_cast<uintptr_t>(vs)) ^
                                             (uint64_t(reinterpret_cast<uintptr_t>(ps)) << 1));
  {
    uint32_t words[6 * 32];
    uint32_t count = 0;
    uint32_t remaining = slot_mask;
    uint32_t slot;
    while (rex::bit_scan_forward(remaining, &slot)) {
      remaining &= ~(1u << slot);
      std::memcpy(&words[count],
                  &r[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + slot * 6],
                  6 * sizeof(uint32_t));
      count += 6;
    }
    tb_key = XXH3_64bits_withSeed(words, count * sizeof(uint32_t), tb_key ^ slot_mask);
  }
  if (tb_cache_frame != frame) {
    if (tb_cache.size() > 8192) tb_cache.clear();
    tb_cache_frame = frame;
  }
  TextureBindings& tb = tb_cache[tb_key];
  // Any view destroyed anywhere moves views_epoch, which while a level
  // streams in is most frames: instead of rebuilding everything then, the
  // bindings check that their own views are still there.
  auto views_alive = [&]() {
    if (tb.views_epoch == textures.views_epoch()) return true;
    if (tb.view_count > 24) return false;
    for (uint32_t i = 0; i < tb.view_count; ++i) {
      if (!textures.ViewAlive(tb.views[i], tb.view_ids[i])) return false;
    }
    tb.views_epoch = textures.views_epoch();
    return true;
  };
  bool tb_hit = tb.valid && tb.vs == vs && tb.ps == ps && tb.layout == layout &&
                tb.set_generation == set_cache_generation &&
                tb.answers_epoch == textures.answers_epoch() && views_alive() &&
                ((tb.frame == frame && tb.submission == submission_number) ||
                 tb.valid_until >= frame);
  if (tb_hit && (tb.frame != frame || tb.submission != submission_number)) {
    for (uint32_t i = 0; i < tb.texture_count; ++i) {
      textures.Touch(tb.textures[i], submission_number);
    }
    tb.frame = frame;
    tb.submission = submission_number;
  }
  if (!tb_hit) {
    TextureTimer texture_timer(frame_texture_ns);
    ++stats().texture_bindings_built;
    tb = TextureBindings();
    tb.valid_until = UINT64_MAX;
    for (uint32_t stage = 0; stage < 2; ++stage) {
      const VulkanShader* shader = stage_shaders[stage];
      if (!shader || (!st.counts[stage * 2] && !st.counts[stage * 2 + 1])) continue;
      const auto& texture_bindings = shader->GetTextureBindingsAfterTranslation();
      const auto& sampler_bindings = shader->GetSamplerBindingsAfterTranslation();
      VkDescriptorImageInfo infos[64];
      uint32_t info_count = 0;
      for (const auto& binding : texture_bindings) {
        if (info_count >= 64) break;
        tb.used_mask |= 1u << binding.fetch_constant;
        VkDescriptorImageInfo& info = infos[info_count++];
        info.sampler = VK_NULL_HANDLE;
        info.imageView = SlotView(binding.fetch_constant, binding.dimension,
                                  bool(binding.is_signed));
        info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        tb.valid_until = std::min(tb.valid_until, slot_valid_until);
        if (tb.view_count < 24) {
          tb.views[tb.view_count] = info.imageView;
          tb.view_ids[tb.view_count] = textures.ViewId(info.imageView);
        }
        ++tb.view_count;
        if (slot_texture) {
          if (tb.texture_count < 24) {
            tb.textures[tb.texture_count++] = slot_texture;
          } else {
            tb.valid_until = frame;
          }
        }
      }
      for (const auto& binding : sampler_bindings) {
        if (info_count >= 64) break;
        tb.slots |= 1u << binding.fetch_constant;
        VkDescriptorImageInfo& info = infos[info_count++];
        info.sampler = SlotSampler(binding);
        info.imageView = VK_NULL_HANDLE;
        info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      }
      VkDescriptorSetLayout set_layout =
          stage ? layout->textures_pixel : layout->textures_vertex;
      // Keyed by view ids, never reused, rather than handles, which are: the
      // cache was dropped whenever any view was destroyed, which while a level
      // streams in is most frames, and every material then allocated and
      // wrote new sets (10-50 ms in the frames that dropped).
      uint64_t key_words[64];
      for (uint32_t i = 0; i < info_count; ++i) {
        key_words[i] = infos[i].imageView ? textures.ViewId(infos[i].imageView)
                                          : (uint64_t(1) << 63) ^
                                                uint64_t(reinterpret_cast<uintptr_t>(
                                                    infos[i].sampler));
      }
      uint64_t set_key =
          XXH3_64bits_withSeed(key_words, sizeof(uint64_t) * info_count, uint64_t(set_layout));
      auto cached = texture_set_cache.find(set_key);
      if (cached != texture_set_cache.end()) {
        tb.sets[stage] = cached->second;
        continue;
      }
      if (texture_set_cache.size() >= 16384) {
        FlushSetCache();
      }
      VkDescriptorSet set = AllocatePersistentSet(set_layout);
      if (!set) return skip(11);
      texture_set_cache.emplace(set_key, set);
      VkWriteDescriptorSet writes[2] = {};
      uint32_t write_count = 0;
      if (!texture_bindings.empty()) {
        VkWriteDescriptorSet& w = writes[write_count++];
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set;
        w.dstBinding = 0;
        w.descriptorCount = uint32_t(texture_bindings.size());
        w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        w.pImageInfo = infos;
      }
      if (!sampler_bindings.empty()) {
        VkWriteDescriptorSet& w = writes[write_count++];
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set;
        w.dstBinding = uint32_t(texture_bindings.size());
        w.descriptorCount = uint32_t(sampler_bindings.size());
        w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        w.pImageInfo = infos + texture_bindings.size();
      }
      dfn().vkUpdateDescriptorSets(vk, write_count, writes, 0, nullptr);
      tb.sets[stage] = set;
    }
    tb.slots |= tb.used_mask;
    tb.valid = true;
    tb.frame = frame;
    tb.submission = submission_number;
    tb.vs = vs;
    tb.ps = ps;
    tb.layout = layout;
    tb.views_epoch = textures.views_epoch();
    tb.set_generation = set_cache_generation;
    tb.answers_epoch = textures.answers_epoch();
    // Only the slots these bindings read are cleared: another draw's
    // bindings may still need to see the others as written.
    owner->texture_fetch_dirty_ &= ~tb.slots;
  }

  // System constants come from the render state, the texture fetch
  // constants and the vertex index registers.
  // Draws reading their indices in the shader point at a fresh copy each time.
  bool system_same = !index_load && last_system.submission == submission_number &&
                     system_inputs.setup_generation == setup_generation &&
                     system_inputs.fetch_generation == owner->fetch_generation_ &&
                     system_inputs.index_generation == owner->index_generation_ &&
                     system_inputs.used_mask == tb.used_mask;
  if (!system_same) {
    UpdateSystemConstants(st.primitive_polygonal, st.viewport, tb.used_mask, 0, vs, ps);
    system_inputs = {setup_generation, owner->fetch_generation_, owner->index_generation_,
                     tb.used_mask};
    if (index_load) {
      system_constants.flags |= SpirvShaderTranslator::kSysFlag_ComputeOrPrimitiveVertexIndexLoad;
      if (index32) {
        system_constants.flags |=
            SpirvShaderTranslator::kSysFlag_ComputeOrPrimitiveVertexIndexLoad32Bit;
      }
      system_constants.vertex_index_load_address = index_load_address;
      system_constants.vertex_index_endian = index_load_endian;
      // The next draw builds them again.
      system_inputs.setup_generation = ~0ull;
    }
    system_same = last_system.submission == submission_number &&
                  !std::memcmp(&last_system_constants, &system_constants,
                               sizeof(system_constants));
  }
  if (system_same) {
    system_offset = last_system.offset;
  } else {
    uint8_t* system_out =
        Allocate(sizeof(SpirvShaderTranslator::SystemConstants), ubo_alignment, system_offset);
    if (!system_out) return skip(9);
    std::memcpy(system_out, &system_constants, sizeof(system_constants));
    std::memcpy(&last_system_constants, &system_constants, sizeof(system_constants));
    last_system = {submission_number, 0, nullptr, frame, system_offset};
  }

  // T15/T16: a cached setup does not go through GetRenderTarget again. O espelho do palco (alias) vem
  // antes da escala: ele atualiza a imagem cheia, que a sincronização da escala desce para a escalada.
  if (!msaa_as_samples && !multisample) {
    for (RenderTarget* t : {targets.color[0], targets.color[1], targets.color[2], targets.color[3],
                            targets.depth}) {
      if (!t) continue;
      if (t->key.alias_msaa) {
        t->alias_seq = ++alias_clock;
      } else {
        SyncAliasTarget(t);
      }
    }
  }
  if (scene::Enabled(scene_scale)) {
    for (RenderTarget* t : {targets.color[0], targets.color[1], targets.color[2], targets.color[3],
                            targets.depth}) {
      if (t) SyncSceneTarget(t);
    }
  }

  // Record.
  // F1.3: draws que nao podem mudar nenhum pixel (exato). Blend 0*src+1*dst em todos os canais
  // escritos, ou alpha test NEVER, e sem escrita de Z/stencil. Ver native_invisible.h.
  {
    const auto color_control = r.Get<reg::RB_COLORCONTROL>();
    const bool alpha_never = color_control.alpha_test_enable &&
                             color_control.alpha_func == xenos::CompareFunction::kNever;
    const uint32_t col_mask =
        draw_util::GetNormalizedColorMask(r, st.ps ? st.ps->writes_color_targets() : 0);
    invisible::Blend blends[4] = {};
    for (uint32_t i = 0; i < 4; ++i) {
      const auto b = r.Get<reg::RB_BLENDCONTROL>(reg::RB_BLENDCONTROL::rt_register_indices[i]);
      blends[i] = {uint32_t(b.color_srcblend), uint32_t(b.color_destblend),
                   uint32_t(b.color_comb_fcn), uint32_t(b.alpha_srcblend),
                   uint32_t(b.alpha_destblend), uint32_t(b.alpha_comb_fcn)};
    }
    const bool writes = st.depth_control.z_write_enable || st.depth_control.stencil_enable;
    if (invisible::DrawIsInvisible(col_mask, blends, alpha_never, writes)) {
      static uint64_t invisible_count = 0;
      const uint64_t n = ++invisible_count;
      if (n == 1 || (n & 0xFFF) == 0) {
        REXLOG_INFO("[native] invisible draws: {} (alpha_never={})", n, alpha_never);
      }
      return skip(15);
    }
  }
  if (!BeginPass(targets, st.needed_height)) return skip(14);
  DrawCommand d;
  d.commands = current().main;
  d.flags = 0;
  d.layout = layout->layout;
  if (bound_pipeline != st.pipeline) {
    d.flags |= kDrawPipeline;
    d.pipeline = st.pipeline;
    bound_pipeline = st.pipeline;
  }
  if (bound_layout != layout->layout || !bound_static_sets) {
    d.flags |= kDrawSharedSet;
    d.set_shared = set_shared;
    bound_layout = layout->layout;
    bound_static_sets = true;
    bound_offsets_valid = false;
    bound_texture_sets[0] = bound_texture_sets[1] = VK_NULL_HANDLE;
  }
  uint32_t* dynamic_offsets = d.offsets;
  dynamic_offsets[SpirvShaderTranslator::kConstantBufferSystem] = uint32_t(system_offset);
  dynamic_offsets[SpirvShaderTranslator::kConstantBufferFloatVertex] = uint32_t(vs_float_offset);
  dynamic_offsets[SpirvShaderTranslator::kConstantBufferFloatPixel] = uint32_t(ps_float_offset);
  dynamic_offsets[SpirvShaderTranslator::kConstantBufferBoolLoop] = uint32_t(bool_loop_offset);
  dynamic_offsets[SpirvShaderTranslator::kConstantBufferFetch] = uint32_t(fetch_offset);
  if (!bound_offsets_valid || std::memcmp(bound_offsets, d.offsets, sizeof(d.offsets))) {
    d.flags |= kDrawConstants;
    d.set_constants = set_constants;
    std::memcpy(bound_offsets, d.offsets, sizeof(d.offsets));
    bound_offsets_valid = true;
  }
  if (tb.sets[0] && tb.sets[0] != bound_texture_sets[0]) {
    d.flags |= kDrawTexturesVertex;
    d.texture_sets[0] = tb.sets[0];
    bound_texture_sets[0] = tb.sets[0];
  }
  if (tb.sets[1] && tb.sets[1] != bound_texture_sets[1]) {
    d.flags |= kDrawTexturesPixel;
    d.texture_sets[1] = tb.sets[1];
    bound_texture_sets[1] = tb.sets[1];
  }

  // Dynamic state.
  if (std::memcmp(&st.vp, &bound_viewport, sizeof(st.vp))) {
    d.flags |= kDrawViewport;
    d.viewport = st.vp;
    bound_viewport = st.vp;
  }
  VkRect2D sr;
  sr.offset.x = int32_t(std::min(st.scissor.offset[0], pass_extent.width));
  sr.offset.y = int32_t(std::min(st.scissor.offset[1], pass_extent.height));
  sr.extent.width = std::min(st.scissor.extent[0], pass_extent.width - uint32_t(sr.offset.x));
  sr.extent.height = std::min(st.scissor.extent[1], pass_extent.height - uint32_t(sr.offset.y));
  if (std::memcmp(&sr, &bound_scissor, sizeof(sr))) {
    d.flags |= kDrawScissor;
    d.scissor = sr;
    bound_scissor = sr;
  }
  if (std::memcmp(&st.bias_constant, &bound_depth_bias[0], sizeof(float)) ||
      std::memcmp(&st.bias_slope, &bound_depth_bias[1], sizeof(float))) {
    d.flags |= kDrawDepthBias;
    d.bias_constant = st.bias_constant;
    d.bias_slope = st.bias_slope;
    bound_depth_bias[0] = st.bias_constant;
    bound_depth_bias[1] = st.bias_slope;
  }
  if (std::memcmp(st.blend_constants, bound_blend_constants, sizeof(st.blend_constants))) {
    d.flags |= kDrawBlend;
    std::memcpy(d.blend, st.blend_constants, sizeof(d.blend));
    std::memcpy(bound_blend_constants, st.blend_constants, sizeof(st.blend_constants));
  }
  std::memcpy(d.stencil, st.stencil, sizeof(d.stencil));
  if (st.stencil[0] != bound_stencil[0] || st.stencil[1] != bound_stencil[1]) {
    d.flags |= kDrawStencilCompare;
  }
  if (st.stencil[2] != bound_stencil[2] || st.stencil[3] != bound_stencil[3]) {
    d.flags |= kDrawStencilWrite;
  }
  if (st.stencil[4] != bound_stencil[4] || st.stencil[5] != bound_stencil[5]) {
    d.flags |= kDrawStencilReference;
  }
  std::memcpy(bound_stencil, st.stencil, sizeof(st.stencil));

  d.count = draw_count;
  if (host_indexed) {
    d.flags |= kDrawIndexed;
    if (index_offset != bound_index_offset || index_type != bound_index_type ||
        !bound_index_valid) {
      d.flags |= kDrawIndexBuffer;
      d.index_buffer = stream_buffer;
      d.index_offset = index_offset;
      d.index_type = index_type;
      bound_index_offset = index_offset;
      bound_index_type = index_type;
      bound_index_valid = true;
    }
  }
  SubmitDraw(d);
  ProfileDraw();
  ++frame_draw_count;
  ++stats().draws;
  for (RenderTarget* rt : targets.color) {
    if (rt) rt->last_used_frame = frame;
  }
}

// --- Resolves ------------------------------------------------------------------------

void Renderer::Impl::Resolve() {
  const RegisterFile& r = *regs;
  auto copy_control = r.Get<reg::RB_COPY_CONTROL>();
  auto surface_info = r.Get<reg::RB_SURFACE_INFO>();

  // The rectangle (draw_util::GetResolveInfo): Direct3D 9 puts its three
  // vertices in vertex fetch constant 0.
  xenos::xe_gpu_vertex_fetch_t fetch = r.GetVertexFetch(0);
  if (fetch.type != xenos::FetchConstantType::kVertex || fetch.size != 3 * 2) {
    return;
  }
  const float* vertices = memory->TranslatePhysical<const float*>(fetch.address << 2);
  float half_pixel =
      r.Get<reg::PA_SU_VTX_CNTL>().pix_center == xenos::PixelCenter::kD3DZero ? 0.5f : 0.0f;
  int32_t v[6];
  for (uint32_t i = 0; i < 6; ++i) {
    float f = xenos::GpuSwap(vertices[i], fetch.endian) + half_pixel;
    v[i] = int32_t(f * 256.0f);
  }
  int32_t x0 = (std::min({v[0], v[2], v[4]}) + 127) >> 8;
  int32_t y0 = (std::min({v[1], v[3], v[5]}) + 127) >> 8;
  int32_t x1 = (std::max({v[0], v[2], v[4]}) + 127) >> 8;
  int32_t y1 = (std::max({v[1], v[3], v[5]}) + 127) >> 8;
  if (r.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable) {
    auto offset = r.Get<reg::PA_SC_WINDOW_OFFSET>();
    x0 += offset.window_x_offset;
    x1 += offset.window_x_offset;
    y0 += offset.window_y_offset;
    y1 += offset.window_y_offset;
  }
  draw_util::Scissor scissor;
  draw_util::GetScissor(r, scissor, false);
  x0 = std::clamp(x0, int32_t(scissor.offset[0]), int32_t(scissor.offset[0] + scissor.extent[0]));
  x1 = std::clamp(x1, int32_t(scissor.offset[0]), int32_t(scissor.offset[0] + scissor.extent[0]));
  y0 = std::clamp(y0, int32_t(scissor.offset[1]), int32_t(scissor.offset[1] + scissor.extent[1]));
  y1 = std::clamp(y1, int32_t(scissor.offset[1]), int32_t(scissor.offset[1] + scissor.extent[1]));
  x0 &= ~7;
  y0 &= ~7;
  x1 = rex::align(x1, 8);
  y1 = rex::align(y1, 8);
  x1 = std::min(x1, int32_t(surface_info.surface_pitch));
  if (x0 >= x1 || y0 >= y1) {
    return;
  }
  const edram::Surface surface = SurfaceFor(surface_info);
  uint32_t msaa = surface.msaa;
  // The rectangle in the host target: samples when the surface is drawn as them.
  int32_t host_rect[4] = {x0, y0, x1, y1};
  surface.ScaleRect(host_rect);
  const bool scaled = surface.scale_x > 1 || surface.scale_y > 1;
  bool is_depth = copy_control.copy_src_select >= 4;
  ++stats().resolves;

  BeginRecording();
  EndPass();
  ProfileMark((kProfileResolve << 60) | (uint64_t(is_depth) << 56) |
              (uint64_t(surface_info.surface_pitch & 0x7FF) << 32) |
              (uint64_t(uint32_t(x1 - x0) & 0xFFFF) << 16) | uint32_t(y1 - y0));
  VkCommandBuffer c = cmd();

  RenderTargetKey source_key = {};
  source_key.pitch = surface.pitch;
  source_key.msaa = msaa;
  source_key.alias_msaa = AliasMsaa(uint32_t(surface_info.msaa_samples));
  if (is_depth) {
    auto depth_info = r.Get<reg::RB_DEPTH_INFO>();
    source_key.base_tiles = depth_info.depth_base;
    source_key.format = uint32_t(depth_info.depth_format);
    source_key.is_depth = 1;
  } else {
    auto color_info =
        r.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[copy_control.copy_src_select]);
    source_key.base_tiles = color_info.color_base;
    source_key.format = uint32_t(color_info.color_format);
  }
  RenderTarget* source = GetRenderTarget(source_key, uint32_t(host_rect[3]));
  if (!source) return;
  RT_TRACE("resolve src={} dest=0x{:08X} rect={},{} {}x{}", frame, RtName(source),
           r[rex::graphics::XE_GPU_REG_RB_COPY_DEST_BASE], host_rect[0], host_rect[1], host_rect[2],
           host_rect[3]);
  // F3.3: o conteúdo deste alvo pode estar no destino de um resolve anterior (a imagem foi entregue
  // a ele). Um resolve lê o alvo, então ele volta antes.
  if (source->content_address) {
    RestoreTargetContent(source);
  }
  c = cmd();
  uint32_t width = uint32_t(x1 - x0);
  uint32_t height =
      std::min(uint32_t(y1 - y0),
               (source->height - std::min(uint32_t(host_rect[1]), source->height)) / surface.scale_y);
  if (!height) return;

  // Copy.
  uint32_t dest_base = r[rex::graphics::XE_GPU_REG_RB_COPY_DEST_BASE];
  ++swap_frame.resolve_count;
  swap_frame.resolve_src = source->key.base_tiles << 12;
  swap_frame.resolve_src_format = uint32_t(source->format);
  swap_frame.resolve_dst = dest_base;
  auto dest_info = r.Get<reg::RB_COPY_DEST_INFO>();
  bool copy = dest_base != 0 &&
              (copy_control.copy_command == xenos::CopyCommand::kRaw ||
               copy_control.copy_command == xenos::CopyCommand::kConvert);
  uint32_t dest_width = std::min(uint32_t(x1), 8192u);
  uint32_t dest_height = std::min(uint32_t(y0) + height, 8192u);
  // F3.3: alvo de profundidade inteiro, 1 amostra, destino do mesmo tamanho e formato: o destino fica
  // com a imagem (nada é copiado) e o alvo ganha outra. Só quando o jogo amostra o resolve como
  // profundidade (os words de memória não existiriam).
  bool handed_off = false;
  if (copy && is_depth) {
    ownership::Rect orect{host_rect[0], host_rect[1], host_rect[2], host_rect[3]};
    ownership::Source osrc;
    osrc.width = source->width;
    osrc.height = source->height;
    osrc.samples = 1u << msaa;
    osrc.depth = true;
    osrc.holds_content = source->content_address == 0;
    ownership::Destination odst;
    odst.width = dest_width;
    odst.height = dest_height;
    odst.format = uint32_t(source->format);
    ownership::Decision decision =
        REXCVAR_GET(gh3_native_resolve_swap)
            ? ownership::Decide(orect, uint32_t(source->format), osrc, odst)
            : ownership::Decision::kCopy;
    if (decision == ownership::Decision::kHandOff &&
        !textures.WantsDepthWords(dest_base & 0x1FFFFFFF) &&
        HandOffDepthImage(source, dest_base & 0x1FFFFFFF)) {
      handed_off = true;
      static bool handoff_logged = false;
      if (!handoff_logged) {
        handoff_logged = true;
        REXLOG_INFO("[native] resolve ownership: handed off {:08X} ({}x{}) without copying",
                    dest_base & 0x1FFFFFFF, source->width, source->height);
      }
      ownership::Count(resolve_ownership, decision,
                       uint64_t(source->width) * source->height * 4);
      resolve_barrier_pending = true;
    } else {
      ownership::Count(resolve_ownership,
                       decision == ownership::Decision::kHandOff ? ownership::Decision::kCopy
                                                                 : decision,
                       0);
    }
  }
  if (copy && !handed_off) {
    VkMemoryBarrier to_transfer = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    dfn().vkCmdPipelineBarrier(c,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &to_transfer, 0, nullptr,
                               0, nullptr);
    // Sized by what the resolve covers, not by the destination pitch (aligned
    // to 32): textures sample the image across its whole size, and the game
    // samples its full-screen resolves at 1040 wide, where the pitch made the
    // image 1056 and every one of them needed a corrected copy each frame.
    // Smaller textures sampled from a corner still get one (CopyToSized).
    VkFormat dest_format = is_depth ? VK_FORMAT_R32_SFLOAT : source->format;
    uint32_t host_swizzle = is_depth ? uint32_t(xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR)
                                     : uint32_t(xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA);
    if (!is_depth && dest_info.copy_dest_swap) {
      host_swizzle = XE_GPU_MAKE_TEXTURE_SWIZZLE(B, G, R, A);
    }
    ResolveDestination* dest = textures.GetResolveDestination(
        dest_base & 0x1FFFFFFF, dest_width, dest_height, dest_format, host_swizzle, c);
    if (dest) {
      if (scaled) {
        // Samples drawn as pixels: the resolve averages each pixel's samples,
        // a linear blit to half the size (or half the height for 2x).
        if (is_depth) {
          static bool logged = false;
          if (!logged) {
            logged = true;
            REXLOG_WARN("[native] Depth resolve of a {}x{} multisampled surface not done", width,
                        height);
          }
        } else {
          VkImageBlit blit = {};
          blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
          blit.dstSubresource = blit.srcSubresource;
          blit.srcOffsets[0] = {host_rect[0], host_rect[1], 0};
          blit.srcOffsets[1] = {host_rect[2], host_rect[1] + int32_t(height * surface.scale_y), 1};
          blit.dstOffsets[0] = {x0, y0, 0};
          blit.dstOffsets[1] = {x1, y0 + int32_t(height), 1};
          vkCmdBlitImage(c, source->image, VK_IMAGE_LAYOUT_GENERAL, dest->image,
                               VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);
          textures.CopyToSized(*dest, c);
        }
      } else if (is_depth) {
        // Depth cannot be copied into a color image directly. It goes through
        // a scratch buffer, where a compute pass makes both forms a game can
        // read a depth resolve back as: floats, into this R32_SFLOAT image for
        // k_24_8 fetches, and the words the resolve leaves in Xbox 360
        // memory, (depth24 << 8) | stencil, into a second image for fetches
        // as any other format (see TextureCache::kDepthWordsTag). Games read
        // the stencil byte that way to tell the player from the scene: with
        // depth alone in every channel, a post effect meant to skip the skater
        // drew him doubled with a light halo.
        const uint64_t texels = uint64_t(width) * height;
        const VkDeviceSize stencil_offset = rex::align(VkDeviceSize(texels * 4), VkDeviceSize(256));
        const VkDeviceSize packed_offset =
            rex::align(stencil_offset + VkDeviceSize(texels), VkDeviceSize(256));
        const bool has_stencil = HasStencil(source->format);
        const bool unorm_guest = xenos::DepthRenderTargetFormat(source->key.format) ==
                                 xenos::DepthRenderTargetFormat::kD24S8;
        if (msaa == 0 && depth_copy_pipeline && source->sample_view &&
            !textures.WantsDepthWords(dest_base & 0x1FFFFFFF)) {
          // The usual case: the game samples the resolve only as depth.
          VkMemoryBarrier to_compute = {
              VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                  VK_ACCESS_SHADER_WRITE_BIT,
              VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
          dfn().vkCmdPipelineBarrier(
              c,
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                  VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &to_compute, 0, nullptr, 0, nullptr);
          VkDescriptorSet set = AllocateSet(depth_copy_set_layout);
          if (set) {
            VkDescriptorImageInfo images[2] = {
                {VK_NULL_HANDLE, source->sample_view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, dest->view, VK_IMAGE_LAYOUT_GENERAL}};
            VkWriteDescriptorSet writes[2] = {};
            for (uint32_t i = 0; i < 2; ++i) {
              writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
              writes[i].dstSet = set;
              writes[i].dstBinding = i;
              writes[i].descriptorCount = 1;
              writes[i].descriptorType =
                  i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
              writes[i].pImageInfo = &images[i];
            }
            dfn().vkUpdateDescriptorSets(vk, 2, writes, 0, nullptr);
            int32_t params[4] = {x0, y0, int32_t(width), int32_t(height)};
            dfn().vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, depth_copy_pipeline);
            dfn().vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_COMPUTE, depth_copy_layout, 0,
                                          1, &set, 0, nullptr);
            dfn().vkCmdPushConstants(c, depth_copy_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                     sizeof(params), params);
            dfn().vkCmdDispatch(c, (width + 7) / 8, (height + 7) / 8, 1);
          }
          // Readers: the draws, CopyToSized below, and the next depth test.
          VkMemoryBarrier after_compute = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                           VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
          dfn().vkCmdPipelineBarrier(
              c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                  VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              0, 1, &after_compute, 0, nullptr, 0, nullptr);
        } else if (msaa == 0 && depth_scratch && depth_convert_pipeline &&
                   packed_offset + texels * 4 <= kDepthScratchSize) {
          VkBufferImageCopy regions[2] = {};
          regions[0].imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
          regions[0].imageOffset = {x0, y0, 0};
          regions[0].imageExtent = {width, height, 1};
          regions[1] = regions[0];
          regions[1].bufferOffset = stencil_offset;
          regions[1].imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
          dfn().vkCmdCopyImageToBuffer(c, source->image, VK_IMAGE_LAYOUT_GENERAL, depth_scratch,
                                       has_stencil ? 2 : 1, regions);
          VkMemoryBarrier to_compute = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                        VK_ACCESS_TRANSFER_WRITE_BIT,
                                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
          dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &to_compute, 0,
                                     nullptr, 0, nullptr);
          uint32_t count = uint32_t(texels);
          uint32_t groups = (count + 63) / 64;
          constexpr uint32_t kGroupsPerRow = 4096;
          uint32_t params[5] = {count, kGroupsPerRow * 64, uint32_t(stencil_offset / 4),
                                uint32_t(packed_offset / 4),
                                (source->format == VK_FORMAT_D24_UNORM_S8_UINT ? 1u : 0u) |
                                    (has_stencil ? 2u : 0u)};
          dfn().vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, depth_convert_pipeline);
          dfn().vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_COMPUTE, depth_convert_layout,
                                        0, 1, &depth_convert_set, 0, nullptr);
          dfn().vkCmdPushConstants(c, depth_convert_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                   sizeof(params), params);
          dfn().vkCmdDispatch(c, std::min(groups, kGroupsPerRow),
                              (groups + kGroupsPerRow - 1) / kGroupsPerRow, 1);
          VkMemoryBarrier to_copy = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT};
          dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &to_copy, 0, nullptr,
                                     0, nullptr);
          VkBufferImageCopy out = regions[0];
          out.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          dfn().vkCmdCopyBufferToImage(c, depth_scratch, dest->image, VK_IMAGE_LAYOUT_GENERAL, 1,
                                       &out);
          // The words only for unorm depth: float24 would need its own
          // encoding, and the game samples those buffers (shadow maps) as depth.
          if (unorm_guest) {
            ResolveDestination* words = textures.GetResolveDestination(
                (dest_base & 0x1FFFFFFF) | TextureCache::kDepthWordsTag, dest_width, dest_height,
                VK_FORMAT_R8G8B8A8_UNORM, uint32_t(xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA), c);
            if (words) {
              out.bufferOffset = packed_offset;
              dfn().vkCmdCopyBufferToImage(c, depth_scratch, words->image,
                                           VK_IMAGE_LAYOUT_GENERAL, 1, &out);
              textures.CopyToSized(*words, c);
            }
          }
        } else {
          static bool logged = false;
          if (!logged) {
            logged = true;
            REXLOG_WARN("[native] Depth resolve not done: {}x{}, {}x MSAA", width, height,
                        1u << msaa);
          }
        }
      } else if (msaa) {
        VkImageResolve resolve = {};
        resolve.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        resolve.dstSubresource = resolve.srcSubresource;
        resolve.srcOffset = {x0, y0, 0};
        resolve.dstOffset = {x0, y0, 0};
        resolve.extent = {width, height, 1};
        vkCmdResolveImage(c, source->image, VK_IMAGE_LAYOUT_GENERAL, dest->image,
                                VK_IMAGE_LAYOUT_GENERAL, 1, &resolve);
      } else if (source->samples == VK_SAMPLE_COUNT_1_BIT && textures.StorageCapable(dest->format) &&
                 ComputeColorCopy(c, source->view, dest->view, x0, y0, x0, y0, width, height)) {
        // Done by compute (see copy_color.cs.glsl).
      } else {
        VkImageCopy region = {};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = region.srcSubresource;
        region.srcOffset = {x0, y0, 0};
        region.dstOffset = {x0, y0, 0};
        region.extent = {width, height, 1};
        dfn().vkCmdCopyImage(c, source->image, VK_IMAGE_LAYOUT_GENERAL, dest->image,
                             VK_IMAGE_LAYOUT_GENERAL, 1, &region);
      }
      textures.CopyToSized(*dest, c);
    }
  }

  // Clear (RenderTargetCache::PerformTransfersAndResolveClears).
  bool clear_depth = copy_control.depth_clear_enable != 0;
  bool clear_color = !is_depth && copy_control.color_clear_enable != 0;
  if (clear_depth || clear_color) {
    // After a copy the clear must wait for it (it reads what the clear
    // overwrites). Without one, the clear only follows earlier rendering,
    // which the pending resolve barrier or pass order already covers.
    if (copy) {
      VkMemoryBarrier to_clear = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                  VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
      dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 0, 1, &to_clear, 0, nullptr, 0, nullptr);
    }
    PassTargets clear_targets;
    RenderTarget* depth_target = nullptr;
    RenderTarget* color_target = nullptr;
    if (clear_depth) {
      auto depth_info = r.Get<reg::RB_DEPTH_INFO>();
      RenderTargetKey key = {};
      key.base_tiles = depth_info.depth_base;
      key.pitch = surface.pitch;
      key.format = uint32_t(depth_info.depth_format);
      key.msaa = msaa;
      key.alias_msaa = AliasMsaa(uint32_t(surface_info.msaa_samples));
      key.is_depth = 1;
      depth_target = GetRenderTarget(key, uint32_t(host_rect[3]));
      clear_targets.depth = depth_target;
    }
    if (clear_color) {
      color_target = source;
      clear_targets.color[0] = color_target;
    }
    // F3.3: o clear que cobre o alvo inteiro dispensa trazer o conteúdo de volta (o resolve já o
    // entregou ao destino); um clear parcial precisa dele antes de limpar.
    if (depth_target && depth_target->content_address) {
      const bool whole = host_rect[0] <= 0 && host_rect[1] <= 0 &&
                         uint32_t(host_rect[2]) >= depth_target->width &&
                         uint32_t(host_rect[3]) >= depth_target->height;
      if (whole) {
        depth_target->content_address = 0;
      } else {
        RestoreTargetContent(depth_target);
      }
    }
    if (color_target && color_target->content_address) {
      RestoreTargetContent(color_target);
    }
    EndPass();
    if (BeginPass(clear_targets)) {
      c = cmd();
      VkClearAttachment attachments[2] = {};
      uint32_t count = 0;
      if (depth_target) {
        VkClearAttachment& a = attachments[count++];
        a.aspectMask = HasStencil(depth_target->format)
                           ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                           : VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT);
        uint32_t clear = r[rex::graphics::XE_GPU_REG_RB_DEPTH_CLEAR];
        swap_frame.has_clear_depth = true;
        swap_frame.clear_depth = clear;
        swap_frame.clear_depth_target = depth_target->key.base_tiles << 12;
        uint32_t depth24 = clear >> 8;
        a.clearValue.depthStencil.depth =
            xenos::DepthRenderTargetFormat(depth_target->key.format) ==
                    xenos::DepthRenderTargetFormat::kD24FS8
                ? xenos::Float20e4To32(depth24)
                : xenos::UNorm24To32(depth24);
        a.clearValue.depthStencil.stencil = clear & 0xFF;
      }
      if (color_target) {
        VkClearAttachment& a = attachments[count++];
        a.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        a.colorAttachment = 0;
        uint64_t value = r[rex::graphics::XE_GPU_REG_RB_COLOR_CLEAR] |
                         (uint64_t(r[rex::graphics::XE_GPU_REG_RB_COLOR_CLEAR_LO]) << 32);
        swap_frame.has_clear_color = true;
        swap_frame.clear_color = value;
        swap_frame.clear_color_target = color_target->key.base_tiles << 12;
        using F = xenos::ColorRenderTargetFormat;
        switch (F(color_target->key.format)) {
          case F::k_8_8_8_8:
          case F::k_8_8_8_8_GAMMA:
            for (uint32_t j = 0; j < 4; ++j) {
              a.clearValue.color.float32[j] = float((value >> (j * 8)) & 0xFF) / 255.0f;
            }
            break;
          case F::k_2_10_10_10:
          case F::k_2_10_10_10_AS_10_10_10_10:
            for (uint32_t j = 0; j < 3; ++j) {
              a.clearValue.color.float32[j] = float((value >> (j * 10)) & 0x3FF) / 1023.0f;
            }
            a.clearValue.color.float32[3] = float((value >> 30) & 0x3) / 3.0f;
            break;
          case F::k_2_10_10_10_FLOAT:
          case F::k_2_10_10_10_FLOAT_AS_16_16_16_16:
            for (uint32_t j = 0; j < 3; ++j) {
              a.clearValue.color.float32[j] = xenos::Float7e3To32((value >> (j * 10)) & 0x3FF);
            }
            a.clearValue.color.float32[3] = float((value >> 30) & 0x3) / 3.0f;
            break;
          case F::k_16_16_FLOAT:
          case F::k_16_16_16_16_FLOAT:
            for (uint32_t j = 0; j < 4; ++j) {
              a.clearValue.color.float32[j] =
                  rex::xenos_half_to_float(uint16_t((value >> (j * 16)) & 0xFFFF));
            }
            break;
          case F::k_16_16:
          case F::k_16_16_16_16:
            for (uint32_t j = 0; j < 4; ++j) {
              a.clearValue.color.float32[j] = std::max(
                  float(int16_t((value >> (j * 16)) & 0xFFFF)) * (1.0f / 32767.0f), -1.0f);
            }
            break;
          case F::k_32_FLOAT: {
            uint32_t bits = uint32_t(value);
            std::memcpy(&a.clearValue.color.float32[0], &bits, 4);
          } break;
          case F::k_32_32_FLOAT: {
            uint32_t bits[2] = {uint32_t(value), uint32_t(value >> 32)};
            std::memcpy(a.clearValue.color.float32, bits, 8);
          } break;
          default:
            break;
        }
      }
      VkClearRect rect = {};
      const uint32_t host_x0 = uint32_t(host_rect[0]), host_y0 = uint32_t(host_rect[1]);
      rect.rect.offset = {host_rect[0], host_rect[1]};
      rect.rect.extent = {
          std::min(width * surface.scale_x, pass_extent.width - std::min(host_x0, pass_extent.width)),
          std::min(height * surface.scale_y,
                   pass_extent.height - std::min(host_y0, pass_extent.height))};
      rect.layerCount = 1;
      if (rect.rect.extent.width && rect.rect.extent.height) {
        dfn().vkCmdClearAttachments(c, count, attachments, 1, &rect);
      }
      EndPass();
    }
  }

  // The barrier that makes the copies and clears visible is recorded before
  // the next rendering or present, so a run of resolves shares one (each
  // barrier is a full wait for idle on this GPU).
  resolve_barrier_pending = true;
}

void Renderer::Impl::FlushResolveBarrier() {
  if (!resolve_barrier_pending) {
    return;
  }
  resolve_barrier_pending = false;
  VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                           VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_TRANSFER_READ_BIT};
  dfn().vkCmdPipelineBarrier(cmd(),
                             VK_PIPELINE_STAGE_TRANSFER_BIT |
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &after, 0, nullptr, 0, nullptr);
}

// --- Swap ------------------------------------------------------------------------------

void Renderer::Impl::Swap(rex::ui::Presenter* presenter, uint32_t frontbuffer, uint32_t width,
                          uint32_t height) {
  // VdSwap describes the frontbuffer in texture fetch constant 0, swizzle
  // included; the SDK presents from it too (RequestSwapTexture). Without the
  // swizzle, red and blue come out swapped.
  xenos::xe_gpu_texture_fetch_t fetch = regs->GetTextureFetch(0);
  uint32_t address = frontbuffer & 0x1FFFFFFF;
  uint32_t guest_swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
  if (fetch.type == xenos::FetchConstantType::kTexture && fetch.base_address) {
    address = fetch.base_address << 12;
    guest_swizzle = fetch.swizzle;
  }
  uint32_t source_width = 0, source_height = 0;
  VkImageView source_view =
      textures.GetResolveDestinationView2D(address, guest_swizzle, &source_width, &source_height);
  static uint32_t missing_logged = 0;
  if (!source_view && missing_logged < 8) {
    ++missing_logged;
    REXLOG_WARN("[native] Nothing was resolved to the frontbuffer {:08X} ({}x{})", address, width,
                height);
  }
  static bool swap_logged = false;
  if (source_view && !swap_logged) {
    swap_logged = true;
    REXLOG_INFO("[native] Frontbuffer {:08X} {}x{}, swizzle {:03X}", address, width, height,
                guest_swizzle);
  }
  if (presenter && source_view) {
    width = std::min(width ? width : source_width, source_width);
    height = std::min(height ? height : source_height, source_height);
    rex::system::X_VIDEO_MODE mode;
    rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
    uint32_t display_width = std::max(1u, uint32_t(mode.display_width));
    uint32_t display_height = std::max(1u, uint32_t(mode.display_height));
    presenter->RefreshGuestOutput(
        width, height, display_width, display_height,
        [&](rex::ui::Presenter::GuestOutputRefreshContext& context) -> bool {
          auto& vulkan_context = static_cast<
              rex::ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext&>(context);
          BeginRecording();
          EndPass();
          uint32_t slot = uint32_t(submission_number % kSubmissionSlots);
          std::memcpy(ramp_mapping + 256 * sizeof(uint32_t) * slot, gamma_ramp,
                      sizeof(gamma_ramp));
          VkDescriptorImageInfo source_info = {VK_NULL_HANDLE, source_view,
                                               VK_IMAGE_LAYOUT_GENERAL};
          VkDescriptorImageInfo dest_info = {VK_NULL_HANDLE, vulkan_context.image_view(),
                                             VK_IMAGE_LAYOUT_GENERAL};
          VkWriteDescriptorSet writes[2] = {};
          writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
          writes[0].dstSet = swap_sets[slot][1];
          writes[0].descriptorCount = 1;
          writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
          writes[0].pImageInfo = &source_info;
          writes[1] = writes[0];
          writes[1].dstSet = swap_sets[slot][2];
          writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
          writes[1].pImageInfo = &dest_info;
          dfn().vkUpdateDescriptorSets(vk, 2, writes, 0, nullptr);
          FlushResolveBarrier();
          VkCommandBuffer c = cmd();
          VkImageMemoryBarrier barrier = {};
          barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          barrier.srcAccessMask =
              vulkan_context.image_ever_written_previously()
                  ? rex::ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask
                  : 0;
          barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
          barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
          barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
          barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          barrier.image = vulkan_context.image();
          barrier.subresourceRange = rex::ui::vulkan::util::InitializeSubresourceRange();
          dfn().vkCmdPipelineBarrier(
              c,
              vulkan_context.image_ever_written_previously()
                  ? rex::ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask
                  : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
          dfn().vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, swap_pipeline);
          dfn().vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_COMPUTE, swap_pipeline_layout, 0,
                                        3, swap_sets[slot], 0, nullptr);
          uint32_t size[2] = {width, height};
          dfn().vkCmdPushConstants(c, swap_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                   sizeof(size), size);
          dfn().vkCmdDispatch(c, (width + 15) / 16, (height + 7) / 8, 1);
          barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
          barrier.dstAccessMask = rex::ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask;
          barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
          barrier.newLayout = rex::ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout;
          dfn().vkCmdPipelineBarrier(
              c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              rex::ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask, 0, 0, nullptr, 0,
              nullptr, 1, &barrier);
          // Submitted before returning, so the presenter's own work comes
          // after it on the queue.
          Submit();
          context.SetIs8bpc(true);
          return true;
        });
  }
  Submit();
  if (REXCVAR_GET(gh3_native_swap_log)) {
    swap_log::Entry entry;
    entry.frame = frame;
    entry.frontbuffer = address;
    entry.width = width;
    entry.height = height;
    const ResolveDestination* dest = textures.FindResolveDestination(address);
    entry.format = dest ? uint32_t(dest->format) : 0;
    entry.source_width = dest ? dest->width : source_width;
    entry.source_height = dest ? dest->height : source_height;
    entry.has_clear_color = swap_frame.has_clear_color;
    entry.clear_color = swap_frame.clear_color;
    entry.clear_color_target = swap_frame.clear_color_target;
    entry.has_clear_depth = swap_frame.has_clear_depth;
    entry.clear_depth = swap_frame.clear_depth;
    entry.clear_depth_target = swap_frame.clear_depth_target;
    entry.resolve_count = swap_frame.resolve_count;
    entry.has_resolve = swap_frame.resolve_count != 0;
    entry.resolve_src = swap_frame.resolve_src;
    entry.resolve_src_format = swap_frame.resolve_src_format;
    entry.resolve_dst = swap_frame.resolve_dst;
    const RenderTarget* target = pass.color[0] ? pass.color[0] : pass.depth;
    if (target) {
      entry.has_rt = true;
      entry.rt_base = target->key.base_tiles << 12;
      entry.rt_pitch = target->key.pitch;
      entry.rt_format = target->key.format;
      entry.rt_samples = 1u << std::min(target->key.msaa, 31u);
      entry.rt_depth = target->key.is_depth != 0;
      entry.alias_seq = target->alias_seq;
    }
    entry.fence_wait_ns = frame_fence_wait_ns;
    entry.presented = presenter != nullptr && source_view != VK_NULL_HANDLE;
    REXLOG_INFO("[native] {}", swap_log::Format(entry));
  }
  // F1.4: a linha "format audit:" resume os alvos criados; loga de novo a cada alvo novo depois do
  // boot/menu (quadro 300 ~ 5 s), para a conta do fim incluir os alvos da gameplay.
  if (REXCVAR_GET(gh3_native_format_log) && frame >= 300 && !format_audit.Empty() &&
      format_audit_logged_targets != format_audit.totals().targets) {
    REXLOG_INFO("[native] {}", format_audit.Report(frame));
    format_audit_logged_targets = format_audit.totals().targets;
  }
  swap_frame = {};
  // Frame boundary.
  auto now = std::chrono::steady_clock::now();
  double frame_ms = 0.0;
  if (last_swap_time.time_since_epoch().count()) {
    frame_ms = std::chrono::duration<double, std::milli>(now - last_swap_time).count();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_swap_time).count();
    if (ms > 50) {
      RendererStats& st = stats();
      ++st.frames_over_50ms;
      if (ms > 100) ++st.frames_over_100ms;
      st.slow_frame_pipelines += st.pipelines_created - at_last_swap.pipelines_created;
      st.slow_frame_shaders += st.shaders_translated - at_last_swap.shaders_translated;
      st.slow_frame_uploads += textures.stats().uploads - at_last_swap.textures_uploaded;
      st.slow_frame_render_targets +=
          st.render_targets_created - at_last_swap.render_targets_created;
    }
    // T31: o intervalo entre swaps é o que o jogador sente; o controlador só muda a escala no fim do
    // quadro para os alvos escalados do próximo já nascerem no tamanho novo.
    if (scene_dynamic) {
      const float before = scene_scale;
      const double next = scene_dynamic->Update(frame_ms);
      if (float(next) != before) {
        scene_scale = float(next);
        scene_logged = false;
        REXLOG_INFO("[native] scene dynamic: {:g} -> {:g} (frame {}, {:.1f} ms)", before, next,
                    frame, frame_ms);
      }
    }
  }
  last_swap_time = now;
  at_last_swap = stats();
  at_last_swap.textures_uploaded = textures.stats().uploads;
  // Save the driver's pipeline cache once new pipelines stop appearing for a
  // while, off this thread: the file is tens of megabytes on the SD card.
  if (!driver_cache_path.empty() && stats().pipelines_created != pipelines_at_last_save &&
      now - last_pipeline_time > std::chrono::seconds(5) && !driver_cache_writing.load()) {
    SavePrewarmManifest();
    SaveDriverCache();
  }
  // F2.5: o jogo compila pipelines durante uma tela inteira e o cache do driver espera a calmaria;
  // o arquivo de prewarm é pequeno (dezenas de KB) e pode ser gravado a cada 10 s, para uma saída
  // sem Shutdown (o capture encerra o processo) não perder o que foi compilado.
  if (!prewarm_path.empty() && !prewarm_writing.load() &&
      prewarm_entry_count.load(std::memory_order_relaxed) != prewarm_saved_count &&
      now - last_prewarm_save > std::chrono::seconds(10)) {
    SavePrewarmManifest();
  }
  stats().textures_uploaded = textures.stats().uploads;
  stats().texture_bytes_uploaded = textures.stats().upload_bytes;
  stats().textures_created = textures.stats().created;
  stats().textures_streamed = textures.stats().streamed;
  stats().textures_pending = textures.stats().pending;
  stats().textures_replaced = textures.stats().replaced;
  stats().frame_upload_wait_ns = frame_upload_wait_ns;
  stats().frame_fence_wait_ns = frame_fence_wait_ns;
  stats().frame_drain_wait_ns = frame_drain_wait_ns;
  stats().frame_texture_ns = frame_texture_ns;
  stats().frame_draws = frame_draw_count;
  stats().frame_stream_wait_ns = frame_stream_wait_ns;
  frame_stream_wait_ns = 0;
  frame_upload_wait_ns = frame_fence_wait_ns = frame_drain_wait_ns = frame_texture_ns = 0;
  frame_draw_count = 0;
  ++frame;
  profile_frame = profile_pool && frame % 16 == 0;
  PollCompleted();
  ProfileReport();
  textures.BeginFrame(frame, completed_frame);
  FreeRetiredSetPools();
  for (auto it = retired_render_targets.begin(); it != retired_render_targets.end();) {
    if (it->frame <= completed_frame) {
      if (it->sample_view) dfn().vkDestroyImageView(vk, it->sample_view, nullptr);
      dfn().vkDestroyImageView(vk, it->view, nullptr);
      dfn().vkDestroyImage(vk, it->image, nullptr);
      dfn().vkFreeMemory(vk, it->memory, nullptr);
      it = retired_render_targets.erase(it);
    } else {
      ++it;
    }
  }
}

// --- Driver pipeline cache ---------------------------------------------------------------

namespace {
constexpr uint64_t kDriverCacheMagic = 0x3148434E54414E52ULL;  // "RNATNCH1"

// F2.5: o cabeçalho carrega a versão do cache (formato da chave + passes de shader ligados). Um
// arquivo de outra versão é recusado inteiro em vez de ser misturado com pipelines de outro SPIR-V.
bool ReadDriverCache(const std::filesystem::path& path, uint32_t version,
                     std::vector<uint8_t>* bytes) {
  bytes->clear();
  FILE* file = rex::filesystem::OpenFile(path, "rb");
  if (!file) return false;
  uint64_t header[4] = {};
  if (fread(header, sizeof(header), 1, file) == 1 && header[0] == kDriverCacheMagic &&
      header[1] == uint64_t(version) && header[2] >= 32 &&
      header[2] <= 128u * 1024 * 1024) {
    bytes->resize(size_t(header[2]));
    if (fread(bytes->data(), bytes->size(), 1, file) == 1 &&
        XXH3_64bits(bytes->data(), bytes->size()) == header[3]) {
      fclose(file);
      return true;
    }
  }
  bytes->clear();
  fclose(file);
  return false;
}

std::vector<uint8_t> ReadWholeFile(const std::filesystem::path& path) {
  std::vector<uint8_t> bytes;
  if (FILE* file = rex::filesystem::OpenFile(path, "rb")) {
    if (fseek(file, 0, SEEK_END) == 0) {
      long size = ftell(file);
      if (size > 0 && size <= long(128 * 1024 * 1024)) {
        bytes.resize(size_t(size));
        if (fseek(file, 0, SEEK_SET) == 0 &&
            fread(bytes.data(), bytes.size(), 1, file) != 1) {
          bytes.clear();
        }
      }
    }
    fclose(file);
  }
  return bytes;
}
}  // namespace

// F2.5: recria os pipelines do arquivo de prewarm. Roda na inicialização, antes do módulo do jogo
// (InitializeShaderStorage vem antes de o thread do guest rodar), e no thread que chamou a
// inicialização — os threads de tradução e de compilação estão parados, então a tradução aqui
// continua sendo feita por um só thread, como o tradutor exige.
bool Renderer::Impl::PrewarmPipelines(uint32_t version) {
  prewarm_stats = prewarm::Stats();
  prewarm_stats.version = version;
  pipelines_at_prewarm = uint32_t(stats().pipelines_created);
  if (!REXCVAR_GET(gh3_native_pipeline_prewarm)) {
    return false;
  }
  std::vector<uint8_t> data = ReadWholeFile(prewarm_path);
  prewarm::Manifest manifest;
  if (data.empty() || !prewarm::Parse(data.data(), data.size(), version, &manifest)) {
    return false;
  }
  prewarm_stats.loaded = uint32_t(manifest.entries.size());
  auto find_or_create = [this](xenos::ShaderType type, const std::vector<uint32_t>& ucode,
                               uint64_t hash) -> VulkanShader* {
    auto it = shaders.find(hash);
    if (it != shaders.end()) {
      return it->second->type() == type ? it->second.get() : nullptr;
    }
    auto created = std::make_unique<VulkanShader>(device, type, hash, ucode.data(), ucode.size(),
                                                  std::endian::native);
    VulkanShader* shader = created.get();
    shaders.emplace(hash, std::move(created));
    return shader;
  };
  auto translate = [this](VulkanShader::VulkanTranslation* translation) -> bool {
    if (translation->is_valid()) return ShaderModuleFor(translation) != VK_NULL_HANDLE;
    VulkanShader& shader = static_cast<VulkanShader&>(translation->shader());
    if (!shader.is_ucode_analyzed()) shader.AnalyzeUcode(disasm);
    if (!translator->TranslateAnalyzedShader(*translation)) return false;
    ++stats().shaders_translated;
    translation_state[translation] = 2;
    return ShaderModuleFor(translation) != VK_NULL_HANDLE;
  };
  std::vector<prewarm::Entry> kept;
  kept.reserve(manifest.entries.size());
  for (const prewarm::Entry& entry : manifest.entries) {
    if (entry.key.size() != sizeof(PipelineKey) || entry.vs_ucode.empty()) {
      ++prewarm_stats.skipped;
      continue;
    }
    PipelineKey key;
    std::memcpy(&key, entry.key.data(), sizeof(key));
    // O hash da chave é o da identidade do shader que o jogo carrega (sobre o microcódigo no byte
    // order do guest), então o microcódigo gravado aqui — já no byte order do host, como o Shader do
    // SDK guarda — não confere por XXH3; a integridade do arquivo é o hash do corpo (Parse). O
    // Shader criado abaixo usa key.vs_hash/ps_hash como chave, que é a mesma que LoadShader usa.
    VulkanShader* vs =
        find_or_create(xenos::ShaderType::kVertex, entry.vs_ucode, key.vs_hash);
    VulkanShader* ps =
        entry.ps_ucode.empty()
            ? nullptr
            : find_or_create(xenos::ShaderType::kPixel, entry.ps_ucode, key.ps_hash);
    if (!vs || (ps == nullptr && !entry.ps_ucode.empty())) {
      ++prewarm_stats.failed;
      continue;
    }
    auto* vs_translation = static_cast<VulkanShader::VulkanTranslation*>(
        vs->GetOrCreateTranslation(key.vs_modification));
    auto* ps_translation =
        ps ? static_cast<VulkanShader::VulkanTranslation*>(
                 ps->GetOrCreateTranslation(key.ps_modification))
           : nullptr;
    if (!translate(vs_translation) || (ps_translation && !translate(ps_translation))) {
      ++prewarm_stats.failed;
      continue;
    }
    VkShaderModule geometry_module = VK_NULL_HANDLE;
    if (key.geometry_shader) {
      GeometryShaderKey geometry_key;
      geometry_key.key = key.geometry_shader;
      auto it = geometry_shaders.find(geometry_key.key);
      if (it == geometry_shaders.end()) {
        it = geometry_shaders.emplace(geometry_key.key, CreateGeometryShader(device, geometry_key))
                 .first;
      }
      geometry_module = it->second;
      if (!geometry_module) {
        ++prewarm_stats.failed;
        continue;
      }
    }
    const PipelineLayout* pipeline_layout =
        GetPipelineLayout(uint32_t(key.layout >> 48), uint32_t(key.layout >> 32) & 0xFFu,
                          uint32_t(key.layout >> 16) & 0xFFu, uint32_t(key.layout) & 0xFFu);
    if (!pipeline_layout) {
      ++prewarm_stats.failed;
      continue;
    }
    if (pipelines.count(entry.hash)) {
      ++prewarm_stats.skipped;
      kept.push_back(entry);
      continue;
    }
    PipelineJob job;
    job.hash = entry.hash;
    job.key = key;
    job.vertex = ShaderModuleFor(vs_translation);
    job.pixel = ps_translation ? ShaderModuleFor(ps_translation) : VK_NULL_HANDLE;
    job.geometry = geometry_module;
    job.layout = pipeline_layout->layout;
    VkPipeline pipeline = CreatePipeline(job);
    if (!pipeline) {
      ++prewarm_stats.failed;
      continue;
    }
    pipelines.emplace(entry.hash, pipeline);
    ++stats().pipelines_created;
    last_pipeline_time = std::chrono::steady_clock::now();
    ++prewarm_stats.created;
    kept.push_back(entry);
  }
  // O que foi usado continua no arquivo (o prewarm desta execução não pode encolher o cache).
  {
    std::lock_guard<std::mutex> lock(prewarm_mutex);
    for (prewarm::Entry& entry : kept) {
      if (prewarm_entries.size() >= prewarm::kMaxEntries) break;
      prewarm_entries.emplace(entry.hash, std::move(entry));
    }
    prewarm_entry_count.store(prewarm_entries.size(), std::memory_order_relaxed);
  }
  // A contagem de referência é a de pipelines prontos quando o jogo começa: o que passar disso foi
  // compilado durante a gameplay (o número que o F2.5 quer ver zerado).
  pipelines_at_prewarm = uint32_t(stats().pipelines_created);
  return true;
}

void Renderer::Impl::SavePrewarmManifest() {
  if (prewarm_path.empty() || !prewarm_stats.version) return;
  prewarm::Manifest manifest;
  manifest.version = prewarm_stats.version;
  {
    std::lock_guard<std::mutex> lock(prewarm_mutex);
    if (prewarm_entries.empty()) return;
    manifest.entries.reserve(prewarm_entries.size());
    for (const auto& [hash, entry] : prewarm_entries) {
      (void)hash;
      manifest.entries.push_back(entry);
    }
  }
  std::vector<uint8_t> bytes = prewarm::Serialize(manifest);
  if (bytes.size() == prewarm_saved_size) return;
  prewarm_saved_size = bytes.size();
  prewarm_saved_count = manifest.entries.size();
  last_prewarm_save = std::chrono::steady_clock::now();
  prewarm_writing = true;
  std::filesystem::path path = prewarm_path;
  if (prewarm_writer.joinable()) {
    prewarm_writer.join();
  }
  prewarm_writer = std::thread([this, bytes = std::move(bytes), path]() {
    auto temporary = path;
    temporary += ".tmp";
    if (FILE* file = rex::filesystem::OpenFile(temporary, "wb")) {
      bool written = fwrite(bytes.data(), bytes.size(), 1, file) == 1;
      if (fclose(file) != 0) written = false;
      if (written) {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::rename(temporary, path, error);
      }
    }
    prewarm_writing = false;
  });
}

void Renderer::InitializeDriverCache(const std::filesystem::path& cache_root,
                                     uint32_t title_id) {
  Impl& im = *impl_;
  if (!im.device) {
    return;
  }
  std::error_code error;
  auto directory = cache_root / "shaders" / "driver";
  std::filesystem::create_directories(directory, error);
  if (error) {
    return;
  }
  const auto& properties = im.device->properties();
  // F2.5: a versão do cache inclui os passes de shader ligados — trocar o spirv-opt ou a
  // simplificação muda o SPIR-V que o driver compila, então o cache antigo não é reaproveitado.
  const uint32_t cache_version = prewarm::CacheVersion(
      REXCVAR_GET(gh3_native_spirv_opt), REXCVAR_GET(gh3_native_shader_simplify));
  im.cache_version_ = cache_version;
  im.driver_cache_path = directory / fmt::format("native-{:08X}-{:08X}-{:08X}-{:08X}-{:04X}.cache",
                                                 title_id, properties.vendorID, properties.deviceID,
                                                 properties.driverVersion, cache_version);
  im.prewarm_path = directory / prewarm::CacheFileName(cache_version);
  // The newest complete file: the rename that replaces it can fail on Horizon
  // (it does not overwrite), leaving the data in .tmp.
  std::vector<uint8_t> data;
  ReadDriverCache(im.driver_cache_path, cache_version, &data);
  auto temporary = im.driver_cache_path;
  temporary += ".tmp";
  std::vector<uint8_t> newer;
  if (ReadDriverCache(temporary, cache_version, &newer) && newer.size() > data.size()) {
    data = std::move(newer);
  }
  VkPipelineCacheCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  info.initialDataSize = data.size();
  info.pInitialData = data.empty() ? nullptr : data.data();
  VkPipelineCache cache = VK_NULL_HANDLE;
  if (im.dfn().vkCreatePipelineCache(im.vk, &info, nullptr, &cache) != VK_SUCCESS) {
    info.initialDataSize = 0;
    info.pInitialData = nullptr;
    if (im.dfn().vkCreatePipelineCache(im.vk, &info, nullptr, &cache) != VK_SUCCESS) {
      return;
    }
    data.clear();
  }
  // Called before the guest runs, so the ring thread is not using the old one.
  if (im.driver_cache) {
    im.dfn().vkDestroyPipelineCache(im.vk, im.driver_cache, nullptr);
  }
  im.driver_cache = cache;
  im.driver_cache_saved_size = data.size();
  REXLOG_INFO("[native] Driver pipeline cache: {} KB loaded from {}", data.size() >> 10,
              im.driver_cache_path.string());
  // F2.5: recria os pipelines vistos na execução anterior antes de o jogo começar a desenhar.
  im.PrewarmPipelines(cache_version);
  const prewarm::Stats& stats = im.prewarm_stats;
  if (!REXCVAR_GET(gh3_native_pipeline_prewarm)) {
    REXLOG_INFO("[native] pipeline prewarm: off (gh3_native_pipeline_prewarm=false)");
  } else {
    REXLOG_INFO("[native] {}", stats.Line());
  }
}

void Renderer::Impl::SaveDriverCache() {
  pipelines_at_last_save = stats().pipelines_created;
  size_t size = 0;
  if (dfn().vkGetPipelineCacheData(vk, driver_cache, &size, nullptr) != VK_SUCCESS ||
      size < 32 || size == driver_cache_saved_size) {
    return;
  }
  auto data = std::make_shared<std::vector<uint8_t>>(size);
  if (dfn().vkGetPipelineCacheData(vk, driver_cache, &size, data->data()) != VK_SUCCESS) {
    return;
  }
  data->resize(size);
  driver_cache_saved_size = size;
  driver_cache_writing = true;
  std::filesystem::path path = driver_cache_path;
  // Joined, never detached: detaching throws on Horizon, which ended the game.
  if (driver_cache_writer.joinable()) {
    driver_cache_writer.join();
  }
  SavePrewarmManifest();
  if (prewarm_writer.joinable()) {
    prewarm_writer.join();
  }
  driver_cache_writer = std::thread([this, data, path]() {
    auto temporary = path;
    temporary += ".tmp";
    if (FILE* file = rex::filesystem::OpenFile(temporary, "wb")) {
      const uint64_t header[] = {kDriverCacheMagic, uint64_t(cache_version_),
                                 uint64_t(data->size()), XXH3_64bits(data->data(), data->size())};
      bool written = fwrite(header, sizeof(header), 1, file) == 1 &&
                     fwrite(data->data(), data->size(), 1, file) == 1;
      if (fclose(file) != 0) written = false;
      if (written) {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::rename(temporary, path, error);
      }
    }
    driver_cache_writing = false;
  });
}

// --- Renderer ---------------------------------------------------------------------------

Renderer::Renderer() : impl_(std::make_unique<Impl>()) { impl_->owner = this; }
Renderer::~Renderer() { Shutdown(); }

bool Renderer::Initialize(rex::ui::vulkan::VulkanProvider* provider, rex::memory::Memory* memory,
                          RegisterFile* regs) {
  impl_->provider = provider;
  impl_->memory = memory;
  impl_->regs = regs;
  if (!impl_->Initialize()) {
    impl_->Shutdown();
    return false;
  }
  return true;
}

std::string Renderer::TakeTextureUploadReport() {
  return impl_ ? impl_->textures.TakeUploadReport() : std::string();
}

std::string Renderer::TakeSpirvOptReport() {
  if (!impl_) {
    return std::string();
  }
  std::lock_guard<std::mutex> lock(impl_->spirv_opt_stats_mutex);
  if (!impl_->spirv_opt_stats.shaders) {
    return std::string();
  }
  // Cumulativo: o relatório de 10 s volta a linha enquanto houver shaders, como as outras linhas
  // "[native] last 10 s" (não some quando um intervalo não traduziu nada).
  return fmt::format("spirv-opt: {} shaders, {:.1f}% fewer instructions ({} fallbacks)",
                     impl_->spirv_opt_stats.shaders, impl_->spirv_opt_stats.FewerPercent(),
                     impl_->spirv_opt_stats.fallbacks);
}

std::string Renderer::TakePrewarmReport() {
  if (!impl_ || !impl_->prewarm_stats.version) {
    return std::string();
  }
  const uint32_t created = uint32_t(impl_->stats().pipelines_created);
  const uint32_t at_start = impl_->pipelines_at_prewarm;
  const uint32_t after = created >= at_start ? created - at_start : 0;
  return fmt::format("pipeline prewarm: {} at start ({} of {} from the cache), {} new since",
                     at_start, impl_->prewarm_stats.created, impl_->prewarm_stats.loaded, after);
}

std::string Renderer::TakeResolveOwnershipReport() {
  return impl_ ? ownership::Format(impl_->resolve_ownership) : std::string();
}

std::string Renderer::TakeRingCacheReport() {
  if (!impl_) return std::string();
  const ring_caches::ImLoadStats& im = impl_->im_load_cache.stats();
  ring_caches::Report report;
  report.im_hits = im.hits;
  report.im_misses = im.misses;
  report.im_evictions = im.evictions;
  report.samplers_created = impl_->textures.stats().samplers_created;
  if (impl_->profile_pool && impl_->profile_query_reset_count) {
    report.query_resets = impl_->profile_query_reset_count;
    report.query_capacity = Renderer::Impl::kProfileQueries;
  }
  if (!im.hits && !im.misses && !report.samplers_created) {
    return std::string();
  }
  return "ring caches: " + ring_caches::Format(report);
}

std::string Renderer::TakeShaderSimplifyReport() {
  if (!impl_) {
    return std::string();
  }
  std::lock_guard<std::mutex> lock(impl_->spirv_opt_stats_mutex);
  if (!impl_->shader_simplify_stats.modules) {
    return std::string();
  }
  return fmt::format(
      "shader simplify: {} shaders, {} max/min as copies, {} texture queries reused, "
      "{} branches merged ({:.1f}% fewer words, {} fallbacks)",
      impl_->shader_simplify_stats.modules, impl_->shader_simplify_stats.max_min,
      impl_->shader_simplify_stats.texture_queries, impl_->shader_simplify_stats.branches,
      impl_->shader_simplify_stats.FewerWordsPercent(), impl_->shader_simplify_stats.fallbacks);
}

void Renderer::Flush() {
  if (impl_) impl_->Submit();
}

void Renderer::KickRecorder() {
  if (impl_) impl_->KickRecorder();
}

void Renderer::Shutdown() {
  if (impl_) {
    impl_->Shutdown();
  }
}

void Renderer::LoadShader(xenos::ShaderType type, const uint32_t* ucode, uint32_t dword_count,
                          uint32_t guest_address) {
  // The game switches shaders for most draws, loading them from the same few
  // hundred addresses: remember them by address and size, confirmed by a hash
  // of all of the code. A sample of it (the first, middle and last 16 words)
  // was tried first and was not enough: a level loading its shaders where the
  // previous screen's had been got the old shader whenever the two agreed at
  // the sampled words, and drew its world with the wrong vertex shader.
  // A full hash on every load (twice a draw, 12 MB a frame at 3000 draws) was
  // too slow, so each location is verified in full once a frame, and within
  // the frame by its first words: shaders are replaced between screens, not
  // in the middle of drawing one.
  // F4.1: IM_LOAD_IMMEDIATE (guest_address == 0) não tem endereço para localizar o shader, então
  // cada pacote trocava os bytes do microcódigo inteiro e calculava o XXH3. A cache responde
  // pelo conteúdo (assinatura barata + memcmp de todas as palavras) e devolve o shader já
  // resolvido.
  if (!guest_address) {
    if (void* hit = impl_->im_load_cache.Lookup(uint32_t(type), ucode, dword_count)) {
      VulkanShader* shader = static_cast<VulkanShader*>(hit);
      if (type == xenos::ShaderType::kVertex) {
        impl_->active_vs = shader;
      } else {
        impl_->active_ps = shader;
      }
      return;
    }
  }
  uint64_t location = 0;
  uint64_t sample = 0;
  uint64_t head = 0;
  if (guest_address) {
    location = (uint64_t(guest_address) << 20) | (uint64_t(dword_count) << 2) | uint64_t(type);
    auto known = impl_->shaders_by_location.find(location);
    head = XXH3_64bits(ucode, std::min(dword_count, 16u) * sizeof(uint32_t));
    if (known != impl_->shaders_by_location.end() &&
        known->second.verified_frame == impl_->frame && known->second.head == head) {
      if (type == xenos::ShaderType::kVertex) {
        impl_->active_vs = known->second.shader;
      } else {
        impl_->active_ps = known->second.shader;
      }
      return;
    }
    sample = XXH3_64bits(ucode, dword_count * sizeof(uint32_t));
    if (known != impl_->shaders_by_location.end() && known->second.sample == sample) {
      known->second.verified_frame = impl_->frame;
      known->second.head = head;
      if (type == xenos::ShaderType::kVertex) {
        impl_->active_vs = known->second.shader;
      } else {
        impl_->active_ps = known->second.shader;
      }
      return;
    }
  }
  uint64_t hash = guest_address ? sample : XXH3_64bits(ucode, dword_count * sizeof(uint32_t));
  auto& shaders = impl_->shaders;
  auto it = shaders.find(hash);
  VulkanShader* shader;
  if (it != shaders.end()) {
    shader = it->second.get();
  } else {
    auto created = std::make_unique<VulkanShader>(impl_->device, type, hash, ucode, dword_count);
    shader = created.get();
    shaders.emplace(hash, std::move(created));
  }
  if (type == xenos::ShaderType::kVertex) {
    impl_->active_vs = shader;
  } else {
    impl_->active_ps = shader;
  }
  if (location) {
    impl_->shaders_by_location[location] = {shader, sample, impl_->frame, head};
  } else {
    impl_->im_load_cache.Insert(uint32_t(type), ucode, dword_count, shader);
  }
}

namespace {
struct CpuTimer {
  uint64_t& total;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  explicit CpuTimer(uint64_t& t) : total(t) {}
  ~CpuTimer() {
    total += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count());
  }
};
}  // namespace

void Renderer::Draw(const uint32_t* immediate_indices, uint32_t immediate_index_words) {
  CpuTimer timer(stats_.cpu_ns);
  impl_->Draw(immediate_indices, immediate_index_words);
}

void Renderer::Resolve() {
  CpuTimer timer(stats_.cpu_ns);
  impl_->Resolve();
}

void Renderer::Swap(rex::ui::Presenter* presenter, uint32_t frontbuffer, uint32_t width,
                    uint32_t height) {
  CpuTimer timer(stats_.cpu_ns);
  impl_->Swap(presenter, frontbuffer, width, height);
}

void Renderer::SetGammaRamp256(const uint32_t* entries) {
  std::memcpy(impl_->gamma_ramp, entries, sizeof(impl_->gamma_ramp));
}

}  // namespace gh3::native
