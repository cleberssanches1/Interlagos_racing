#pragma once

#include "auto_lap_route_state_assembler.hpp"
#include "auto_lap_route_runtime_state.hpp"

namespace AutoLapRouteDomain
{

inline AutoLapFrameContext BuildAutoLapFrameContext(bool autoLapEnabled,
                                                    bool trackReady,
                                                    int16_t stepUnits,
                                                    int16_t latestActiveSegmentId,
                                                    const SRL::Math::Types::Vector3D& trackSegOffset,
                                                    const SRL::Math::Types::Vector3D& referenceCarWorldPosition,
                                                    const SRL::Math::Types::Vector3D& carWorldPosition,
                                                    int32_t carYawDeg)
{
    AutoLapFrameContext context{};
    SeedAutoLapFrameContext(autoLapEnabled,
                            trackReady,
                            stepUnits,
                            latestActiveSegmentId,
                            trackSegOffset,
                            referenceCarWorldPosition,
                            carWorldPosition,
                            carYawDeg,
                            context);
    return context;
}

inline AutoLapRouteStorageSnapshot BuildAutoLapRouteStorageSnapshot(
    bool initialized,
    bool built,
    bool startupYawAligned,
    uint16_t index,
    uint16_t idCount,
    uint16_t centerCount,
    uint16_t yawCount,
    uint16_t offCount,
    int16_t baseYawDeg,
    int16_t currentOffDeg,
    int8_t selectedGuideLine,
    const std::array<uint16_t, 3>& guideLinePointCounts)
{
    AutoLapRouteStorageSnapshot snapshot{};
    SeedAutoLapRouteStorageSnapshot(initialized,
                                    built,
                                    startupYawAligned,
                                    index,
                                    idCount,
                                    centerCount,
                                    yawCount,
                                    offCount,
                                    baseYawDeg,
                                    currentOffDeg,
                                    selectedGuideLine,
                                    guideLinePointCounts,
                                    snapshot);
    return snapshot;
}

inline AutoLapRouteStorageSnapshot BuildAutoLapRouteStorageSnapshot(
    const AutoLapRouteState& state)
{
    std::array<uint16_t, 3> guideLinePointCounts{};
    for (size_t i = 0; i < guideLinePointCounts.size(); ++i)
    {
        guideLinePointCounts[i] = static_cast<uint16_t>(state.guideLines[i].size());
    }

    return BuildAutoLapRouteStorageSnapshot(
        state.Initialized(),
        state.Built(),
        state.StartupYawAligned(),
        state.index,
        static_cast<uint16_t>(state.ids.size()),
        static_cast<uint16_t>(state.centers.size()),
        static_cast<uint16_t>(state.yawDeg.size()),
        static_cast<uint16_t>(state.offDeg.size()),
        state.baseYawDeg,
        state.currentOffDeg,
        state.selectedGuideLine,
        guideLinePointCounts);
}

inline AutoLapGuideLoadPacket BuildAutoLapGuideLoadPacket(
    bool attempted,
    bool loaded,
    bool parsed,
    const char* loadedCandidate,
    uint32_t byteCount,
    uint32_t parsedVersion,
    const std::array<uint16_t, 3>& parsedLinePointCounts)
{
    AutoLapGuideLoadPacket packet{};
    SeedAutoLapGuideLoadPacket(attempted,
                               loaded,
                               parsed,
                               loadedCandidate,
                               byteCount,
                               parsedVersion,
                               parsedLinePointCounts,
                               packet);
    return packet;
}

inline AutoLapRouteBuildPacket BuildAutoLapRouteBuildPacket(bool valid,
                                                            bool usedGuidePath,
                                                            bool normalizedDirection,
                                                            int8_t selectedGuideLine,
                                                            uint16_t routePointCount,
                                                            uint16_t mappedSegmentCount)
{
    AutoLapRouteBuildPacket packet{};
    SeedAutoLapRouteBuildPacket(valid,
                                usedGuidePath,
                                normalizedDirection,
                                selectedGuideLine,
                                routePointCount,
                                mappedSegmentCount,
                                packet);
    return packet;
}

inline AutoLapRouteBuildPacket BuildAutoLapRouteBuildPacket(const AutoLapRouteState& state,
                                                            bool valid,
                                                            bool usedGuidePath,
                                                            bool normalizedDirection)
{
    return BuildAutoLapRouteBuildPacket(valid,
                                        usedGuidePath,
                                        normalizedDirection,
                                        state.selectedGuideLine,
                                        static_cast<uint16_t>(state.centers.size()),
                                        static_cast<uint16_t>(state.ids.size()));
}

inline AutoLapRouteStepPacket BuildAutoLapRouteStepPacket(
    bool valid,
    uint16_t routeIndex,
    int16_t observedSegmentId,
    int32_t carYawDeg,
    const SRL::Math::Types::Vector3D& carWorldPosition)
{
    AutoLapRouteStepPacket packet{};
    SeedAutoLapRouteStepPacket(valid,
                               routeIndex,
                               observedSegmentId,
                               carYawDeg,
                               carWorldPosition,
                               packet);
    return packet;
}

inline AutoLapRouteStepPacket BuildAutoLapRouteStepPacket(const AutoLapRouteState& state,
                                                          int16_t observedSegmentId,
                                                          int32_t carYawDeg,
                                                          const SRL::Math::Types::Vector3D& carWorldPosition,
                                                          bool valid = true)
{
    return BuildAutoLapRouteStepPacket(valid,
                                       state.index,
                                       observedSegmentId,
                                       carYawDeg,
                                       carWorldPosition);
}

inline void ClearAutoLapSelection(AutoLapRouteStorageSnapshot& ioSnapshot)
{
    ioSnapshot.index = 0u;
    ioSnapshot.selectedGuideLine = -1;
    ioSnapshot.currentOffDeg = 0;
}

inline void MarkAutoLapBuilt(AutoLapRouteStorageSnapshot& ioSnapshot, bool built)
{
    ioSnapshot.built = built;
}

inline void MarkAutoLapInitialized(AutoLapRouteStorageSnapshot& ioSnapshot, bool initialized)
{
    ioSnapshot.initialized = initialized;
}

} // namespace AutoLapRouteDomain
