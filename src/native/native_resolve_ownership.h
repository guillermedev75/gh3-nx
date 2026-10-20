// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Resolve without copying, for depth (F3.3 do plano de 60 FPS).
//
// A resolve of depth is not a memory copy on the Xbox 360: the resolved texture and the render target
// are two views of the same EDRAM. Here, when the whole depth target is resolved, the destination
// texture *takes the render target's image* (no pixel moves) and the render target gets a fresh image
// of its own. The game then clears or draws into the fresh image as before. If it draws into the
// render target without clearing first, the content comes back from the destination by a copy before
// the pass (the restore), which is exactly what the hardware would have shown.
//
// Color is out of scope until parity: the game reuses its color targets without clearing, and the
// restore would cost the copy it saves (measured in nfsmw-nx: 16,807 swaps and 17,350 restores).
//
// The decision, the conditions and the counters live here, free of Vulkan, so they can be tested
// without a GPU. The renderer only does what Decide() says.

#include <cstdint>
#include <cstdio>
#include <string>

namespace gh3::native::ownership {

// A resolve rectangle in host pixels.
struct Rect {
  int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct Source {
  uint32_t width = 0;   // host pixels
  uint32_t height = 0;
  uint32_t samples = 1;
  bool depth = false;
  // false: the content is already in a destination (a previous hand-off); it has to be copied back
  // before the target can be handed off or drawn on.
  bool holds_content = true;
};

struct Destination {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;  // host format id; the destination is made with the source's format
};

enum class Decision {
  kHandOff = 0,   // the destination takes the image; nothing copied
  kRestoreFirst,  // content is in a destination: copy back, then hand off
  kCopy,          // the usual resolve copy
  kNotDepth,
  kMultisampled,
  kNotWholeTarget,
  kSizeMismatch,
  kFormatMismatch,
  kCount,
};

inline const char* Name(Decision d) {
  switch (d) {
    case Decision::kHandOff:
      return "handoff";
    case Decision::kRestoreFirst:
      return "restore first";
    case Decision::kCopy:
      return "copy";
    case Decision::kNotDepth:
      return "not depth";
    case Decision::kMultisampled:
      return "multisampled";
    case Decision::kNotWholeTarget:
      return "not the whole target";
    case Decision::kSizeMismatch:
      return "size mismatch";
    case Decision::kFormatMismatch:
      return "format mismatch";
    default:
      return "?";
  }
}

// The resolve covers every texel the render target can hold. Anything less would leave the
// destination with part of the target only, so it is copied as before.
inline bool CoversWholeTarget(const Rect& r, uint32_t width, uint32_t height) {
  return r.x0 <= 0 && r.y0 <= 0 && uint32_t(r.x1) >= width && uint32_t(r.y1) >= height;
}

// source_format: host format of the render target, which the destination is created with.
inline Decision Decide(const Rect& rect, uint32_t source_format, const Source& source,
                       const Destination& dest) {
  if (!source.depth) return Decision::kNotDepth;
  if (source.samples > 1) return Decision::kMultisampled;
  if (!CoversWholeTarget(rect, source.width, source.height)) return Decision::kNotWholeTarget;
  if (dest.width != source.width || dest.height != source.height) return Decision::kSizeMismatch;
  if (dest.format != source_format) return Decision::kFormatMismatch;
  if (!source.holds_content) return Decision::kRestoreFirst;
  return Decision::kHandOff;
}

// The render target is about to be written. A clear that covers the whole target overwrites every
// texel, so there is nothing to bring back; any other write (a pass that loads, a partial clear, a
// resolve) needs the content restored first when it lives in a destination.
inline bool RestoreBeforeWrite(bool content_in_destination, bool whole_clear) {
  return content_in_destination && !whole_clear;
}

struct Stats {
  uint64_t handoffs = 0;
  uint64_t handoff_bytes = 0;
  uint64_t restores = 0;
  uint64_t restore_bytes = 0;
  uint64_t copies = 0;
  uint32_t declined[uint32_t(Decision::kCount)] = {};
};

inline void Count(Stats& s, Decision d, uint64_t bytes) {
  switch (d) {
    case Decision::kHandOff:
      ++s.handoffs;
      s.handoff_bytes += bytes;
      break;
    case Decision::kRestoreFirst:
      ++s.restores;
      s.restore_bytes += bytes;
      break;
    case Decision::kCopy:
      ++s.copies;
      break;
    default:
      if (uint32_t(d) < uint32_t(Decision::kCount)) ++s.declined[uint32_t(d)];
      break;
  }
}

// One line for the 10 s report, like the other "[native] <name>: ..." lines. Empty while nothing
// was decided (the renderer logs nothing for a target it never resolves).
inline std::string Format(const Stats& s) {
  bool decided = s.handoffs || s.restores || s.copies;
  for (uint32_t i = 0; i < uint32_t(Decision::kCount) && !decided; ++i) {
    decided = s.declined[i] != 0;
  }
  if (!decided) return std::string();
  char line[320];
  std::snprintf(line, sizeof(line),
                "resolve ownership: %llu without copy (%.1f MB), %llu restores (%.1f MB), "
                "%llu copied",
                static_cast<unsigned long long>(s.handoffs), double(s.handoff_bytes) / (1024.0 * 1024.0),
                static_cast<unsigned long long>(s.restores),
                double(s.restore_bytes) / (1024.0 * 1024.0),
                static_cast<unsigned long long>(s.copies));
  std::string result(line);
  // Only the reasons that happened: a long list of zeros costs a log line every 10 s.
  bool any = false;
  for (uint32_t i = 0; i < uint32_t(Decision::kCount); ++i) {
    if (!s.declined[i]) continue;
    result += any ? ", " : " (declined: ";
    any = true;
    result += Name(Decision(i));
    result += ":";
    result += std::to_string(s.declined[i]);
  }
  if (any) result += ")";
  return result;
}

}  // namespace gh3::native::ownership
