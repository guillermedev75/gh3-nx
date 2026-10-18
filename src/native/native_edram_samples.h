// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Multisampled guest surfaces drawn as the samples they are in EDRAM. Xenos
// keeps 2x samples stacked vertically and 4x samples 2x2, so a 4x surface at
// pitch P covers the same EDRAM as a single-sampled one at pitch 2P with the
// samples as its pixels. GH3 clears its 1280x720 1x targets that way: a
// full-screen draw on a 4x alias at pitch 640, never resolved. Keyed by the
// guest pitch and sample count, that clear went to an image of its own, the
// real target was never cleared, its depth kept every earlier frame's and the
// stage drew nothing but the additive lights, which piled up to white.
//
// With samples as pixels the host target is the single-sampled one at the
// sample pitch, and coordinates (viewport, scissor, resolve rectangles) are
// scaled to it. Without, the old keys: the guest pitch, with host MSAA.

#include <cstdint>

namespace gh3::native::edram {

struct Surface {
  uint32_t pitch;    // of the host target, in its pixels
  uint32_t msaa;     // log2 of the host sample count
  uint32_t scale_x;  // host pixels per guest pixel
  uint32_t scale_y;

  // {x0, y0, x1, y1} in guest pixels to host pixels.
  void ScaleRect(int32_t* rect) const {
    rect[0] *= int32_t(scale_x);
    rect[1] *= int32_t(scale_y);
    rect[2] *= int32_t(scale_x);
    rect[3] *= int32_t(scale_y);
  }
};

// guest_msaa: log2 of the guest sample count (xenos::MsaaSamples).
inline Surface HostSurface(uint32_t guest_pitch, uint32_t guest_msaa, bool samples_as_pixels,
                           bool host_msaa) {
  if (!samples_as_pixels) return {guest_pitch, host_msaa ? guest_msaa : 0u, 1, 1};
  uint32_t scale_x = guest_msaa >= 2 ? 2 : 1;
  uint32_t scale_y = guest_msaa >= 1 ? 2 : 1;
  return {guest_pitch * scale_x, 0, scale_x, scale_y};
}

// Drawing the stage with one sample (gh3_native_msaa_as_samples=false, gh3_native_msaa=false): a 4x
// surface at pitch P is a plain P-wide image, and the game then reads the same EDRAM as the 1x target
// at pitch 2P (samples as its pixels). That target gets the stage by a nearest upscale of the image;
// the shading runs once per guest pixel instead of 4x as many.
struct AliasBlit {
  bool valid;
  uint32_t src_w, src_h;  // region of the 1-sample image
  uint32_t dst_w, dst_h;  // the same region in the 1x target (src * scale)
};

// guest_msaa: log2 of the guest sample count of the 1-sample image (pitch src_pitch, src_height rows);
// dst_width/dst_height: the size of the 1x target. The region is cut so the upscaled one fits.
inline AliasBlit AliasBlitExtent(uint32_t guest_msaa, uint32_t src_pitch, uint32_t src_height,
                                 uint32_t dst_width, uint32_t dst_height) {
  uint32_t scale_x = guest_msaa >= 2 ? 2 : 1;
  uint32_t scale_y = guest_msaa >= 1 ? 2 : 1;
  if (scale_x == 1 && scale_y == 1) return {false, 0, 0, 0, 0};
  uint32_t w = src_pitch < dst_width / scale_x ? src_pitch : dst_width / scale_x;
  uint32_t h = src_height < dst_height / scale_y ? src_height : dst_height / scale_y;
  if (!w || !h) return {false, 0, 0, 0, 0};
  return {true, w, h, w * scale_x, h * scale_y};
}

}  // namespace gh3::native::edram
