#pragma once

#include <cstdint>

namespace AutoLapRouteSelectionPolicy
{
constexpr int32_t kMiddleGuideLineIndex = 1;

inline int32_t Select(bool autoLapEnabled, bool middleLineUsable,
                      int32_t nearestGuideLineIndex)
{
    if (autoLapEnabled)
        return middleLineUsable ? kMiddleGuideLineIndex : -1;
    return nearestGuideLineIndex;
}
} // namespace AutoLapRouteSelectionPolicy
