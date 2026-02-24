#pragma once

#include <cstddef>
#include <cstdint>

namespace SegmentComponent
{
static constexpr uint32_t kGeoMagic = 0x314F4547; // "GEO1"
static constexpr uint32_t kMatMagic = 0x3154414D; // "MAT1"

enum class FaceKind : uint8_t
{
    Triangle = 3,
    Quad = 4
};

// Common header used by GEO/MAT component files.
struct FileHeader
{
    uint32_t magic = 0;
    uint16_t version = 1;
    uint16_t reserved = 0;
    uint32_t segmentId = 0;
    uint32_t payloadBytes = 0;
};

// GEO payload summary.
struct GeoHeader
{
    uint32_t vertexCount = 0;
    uint32_t faceCount = 0;
};

// Vertex kept in fixed-point space compatible with runtime.
struct GeoVertex
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

// Face record references vertex indices and UV per corner.
struct GeoFace
{
    uint16_t vertex[4]{};
    int16_t u[4]{};
    int16_t v[4]{};
    uint8_t kind = static_cast<uint8_t>(FaceKind::Quad);
    uint8_t reservedA = 0;
    uint16_t reservedB = 0;
};

// MAT payload summary.
struct MatHeader
{
    uint32_t faceCount = 0;
};

// Face -> material map (global face index order).
struct MatFaceBinding
{
    uint32_t materialId = 0;
};
} // namespace SegmentComponent

