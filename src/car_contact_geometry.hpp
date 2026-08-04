#pragma once

#include <cstdint>

namespace Game::CarPhysics::ContactGeometry
{
// CAR1.NYA wheel centers are approximately X +/-22.4 and Z +37/-38.
// Collision probes and visual attitude must use that same world scale.
static constexpr int32_t kHalfWheelBaseRaw = 0x00258000; // 37.5
static constexpr int32_t kHalfTrackRaw = 0x00166666;     // ~22.4
static constexpr int32_t kWheelbaseRaw = kHalfWheelBaseRaw * 2; // 75.0
static constexpr int32_t kTrackRaw = kHalfTrackRaw * 2;          // ~44.8

static_assert(kWheelbaseRaw > (70 << 16) && kWheelbaseRaw < (80 << 16),
              "CAR1 contact wheelbase must remain in model/world units");
static_assert(kTrackRaw > (40 << 16) && kTrackRaw < (50 << 16),
              "CAR1 contact track must remain in model/world units");
} // namespace Game::CarPhysics::ContactGeometry
