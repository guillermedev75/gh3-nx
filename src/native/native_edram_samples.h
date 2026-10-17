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

}  // namespace gh3::native::edram
