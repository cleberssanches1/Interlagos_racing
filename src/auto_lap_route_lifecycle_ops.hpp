#pragma once

#include <type_traits>

#include "auto_lap_route_runtime_state.hpp"

namespace AutoLapRouteDomain
{

inline bool HasRetainedAutoLapRouteStorage(const AutoLapRouteState& state)
{
    return state.Built() ||
           state.Initialized() ||
           state.ids.capacity() > 0u ||
           state.centers.capacity() > 0u ||
           state.yawDeg.capacity() > 0u ||
           state.offDeg.capacity() > 0u;
}

inline void ResetAutoLapRouteFlags(AutoLapRouteState& state)
{
    state.SetInitialized(false);
    state.SetBuilt(false);
    state.SetStartupYawAligned(false);
}

inline void ResetAutoLapRouteScalars(AutoLapRouteState& state)
{
    state.index = 0;
    state.baseYawDeg = 0;
    state.currentOffDeg = 0;
    state.selectedGuideLine = -1;
}

inline void ResetAutoLapRouteState(AutoLapRouteState& state)
{
    ResetAutoLapRouteFlags(state);
    ResetAutoLapRouteScalars(state);
}

template <typename TVector>
inline void ClearAndReleaseAutoLapVector(TVector& ioVector)
{
    using VectorType = std::remove_reference_t<TVector>;
    ioVector.clear();
    VectorType{}.swap(ioVector);
}

inline void ClearAutoLapRouteBuffers(AutoLapRouteState& state)
{
    ClearAndReleaseAutoLapVector(state.ids);
    ClearAndReleaseAutoLapVector(state.centers);
    ClearAndReleaseAutoLapVector(state.yawDeg);
    ClearAndReleaseAutoLapVector(state.offDeg);
}

inline void ClearAutoLapGuideLines(AutoLapRouteState& state)
{
    for (size_t i = 0; i < state.guideLines.size(); ++i)
    {
        using GuideLineVector = std::remove_reference_t<decltype(state.guideLines[i])>;
        GuideLineVector{}.swap(state.guideLines[i]);
    }
}

inline void ReleaseAutoLapRouteStorage(AutoLapRouteState& state)
{
    ClearAutoLapRouteBuffers(state);
    ClearAutoLapGuideLines(state);
    ResetAutoLapRouteState(state);
}

} // namespace AutoLapRouteDomain
