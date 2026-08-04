#pragma once

#include <cstddef>

// Compile-time track residency profile. Override these values from the
// Makefile (or directly with -D) without touching the runtime code.
#ifndef TRACK_LOD0_SEGMENTS
#define TRACK_LOD0_SEGMENTS 2
#endif

#ifndef TRACK_LOD1_SEGMENTS
#define TRACK_LOD1_SEGMENTS 8
#endif

#ifndef TRACK_LOD2_SEGMENTS
#define TRACK_LOD2_SEGMENTS 6
#endif

namespace TrackLodConfig
{
static constexpr size_t kLod0Segments = TRACK_LOD0_SEGMENTS;
static constexpr size_t kLod1Segments = TRACK_LOD1_SEGMENTS;
static constexpr size_t kLod2Segments = TRACK_LOD2_SEGMENTS;

// lod_0 and lod_1 use 64x64 textures; lod_2 uses 32x32 textures.
static constexpr size_t kTexture64Segments = kLod0Segments + kLod1Segments;
static constexpr size_t kTexture32Segments = kLod2Segments;
static constexpr size_t kVisibleSegments =
    kLod0Segments + kLod1Segments + kLod2Segments;

static_assert(kVisibleSegments > 0u,
              "At least one visible track segment is required.");
static_assert(kVisibleSegments <= 48u,
              "Visible track segments exceed TrackSystem's fixed pool (48).");
static_assert(kLod0Segments <= kTexture64Segments,
              "lod_0 must be contained in the 64x64 texture band.");
}
