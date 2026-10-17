#pragma once

// Primitives the host has no topology for (point sprites, rectangle lists,
// quad lists) on devices without geometry shaders (MoltenVK). Quad lists
// become triangle lists through their indices; points and rectangles become
// two-triangle strips that the vertex shader expands
// (Shader::HostVertexShaderType::k*AsTriangleStrip, the same fallback the
// SDK's PrimitiveProcessor uses). With geometry shaders (NVK on the Switch)
// the geometry shaders stay.

#include <algorithm>
#include <cstdint>

namespace gh3::native::expand {

enum class Primitive { kPointList, kRectangleList, kQuadList, kOther };

enum class Expansion {
  kNone,              // drawn as it is
  kGeometryShader,    // PipelineGeometryShader
  kQuadToTriangles,   // triangle list through converted indices
  kVertexShaderStrip  // two-triangle strips expanded in the vertex shader
};

inline Expansion Choose(Primitive primitive, bool geometry_shaders) {
  if (primitive == Primitive::kOther) return Expansion::kNone;
  if (geometry_shaders) return Expansion::kGeometryShader;
  return primitive == Primitive::kQuadList ? Expansion::kQuadToTriangles
                                           : Expansion::kVertexShaderStrip;
}

// Guest primitives drawn as strips: 3 vertices per rectangle, 1 per point.
inline uint32_t ExpandedPrimitiveCount(Primitive primitive, uint32_t guest_count) {
  return primitive == Primitive::kRectangleList ? guest_count / 3 : guest_count;
}

// PrimitiveProcessor::GetTwoTriangleStripIndexCount.
inline uint32_t TwoTriangleStripIndexCount(uint32_t strips) {
  return 4 * strips + (std::max(strips, uint32_t(1)) - 1);
}

// 4 vertices per strip and a primitive restart between strips: the vertex
// shader takes the primitive from index >> 2 and the corner from index & 3.
inline void WriteTwoTriangleStrips(uint32_t strips, uint32_t* out) {
  for (uint32_t i = 0; i < strips; ++i) {
    if (i) *out++ = UINT32_MAX;
    for (uint32_t corner = 0; corner < 4; ++corner) *out++ = i * 4 + corner;
  }
}

inline uint32_t QuadListIndexCount(uint32_t guest_count) { return guest_count / 4 * 6; }

// (0 1 2) (0 2 3) per quad, as the SDK's built-in quad list index buffer.
template <typename Read, typename Put>
void QuadListToTriangleList(uint32_t guest_count, Read read, Put put) {
  for (uint32_t q = 0; q < guest_count / 4; ++q) {
    uint32_t v0 = read(q * 4), v1 = read(q * 4 + 1), v2 = read(q * 4 + 2),
             v3 = read(q * 4 + 3);
    put(q * 6 + 0, v0);
    put(q * 6 + 1, v1);
    put(q * 6 + 2, v2);
    put(q * 6 + 3, v0);
    put(q * 6 + 4, v2);
    put(q * 6 + 5, v3);
  }
}

}  // namespace gh3::native::expand
