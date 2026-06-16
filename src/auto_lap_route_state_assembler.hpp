#pragma once

#include "auto_lap_route_contracts.hpp"

namespace AutoLapRouteDomain
{

inline void SeedAutoLapFrameContext(bool autoLapEnabled,
                                    bool trackReady,
                                    int16_t stepUnits,
                                    int16_t latestActiveSegmentId,
                                    const SRL::Math::Types::Vector3D& trackSegOffset,
                                    const SRL::Math::Types::Vector3D& referenceCarWorldPosition,
                                    const SRL::Math::Types::Vector3D& carWorldPosition,
                                    int32_t carYawDeg,
                                    AutoLapFrameContext& outContext)
{
    outContext.autoLapEnabled = autoLapEnabled;
    outContext.trackReady = trackReady;
    outContext.stepUnits = stepUnits;
    outContext.latestActiveSegmentId = latestActiveSegmentId;
    outContext.trackSegOffset = trackSegOffset;
    outContext.referenceCarWorldPosition = referenceCarWorldPosition;
    outContext.carWorldPosition = carWorldPosition;
    outContext.carYawDeg = carYawDeg;
}

inline void SeedAutoLapRouteStorageSnapshot(bool initialized,
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
                                            const std::array<uint16_t, 3>& guideLinePointCounts,
                                            AutoLapRouteStorageSnapshot& outSnapshot)
{
    outSnapshot.initialized = initialized;
    outSnapshot.built = built;
    outSnapshot.startupYawAligned = startupYawAligned;
    outSnapshot.index = index;
    outSnapshot.idCount = idCount;
    outSnapshot.centerCount = centerCount;
    outSnapshot.yawCount = yawCount;
    outSnapshot.offCount = offCount;
    outSnapshot.baseYawDeg = baseYawDeg;
    outSnapshot.currentOffDeg = currentOffDeg;
    outSnapshot.selectedGuideLine = selectedGuideLine;
    outSnapshot.guideLinePointCounts = guideLinePointCounts;
}

inline void SeedAutoLapGuideLoadPacket(bool attempted,
                                       bool loaded,
                                       bool parsed,
                                       const char* loadedCandidate,
                                       uint32_t byteCount,
                                       uint32_t parsedVersion,
                                       const std::array<uint16_t, 3>& parsedLinePointCounts,
                                       AutoLapGuideLoadPacket& outPacket)
{
    outPacket.attempted = attempted;
    outPacket.loaded = loaded;
    outPacket.parsed = parsed;
    outPacket.loadedCandidate = loadedCandidate;
    outPacket.byteCount = byteCount;
    outPacket.parsedVersion = parsedVersion;
    outPacket.parsedLinePointCounts = parsedLinePointCounts;
}

inline void SeedAutoLapRouteBuildPacket(bool valid,
                                        bool usedGuidePath,
                                        bool normalizedDirection,
                                        int8_t selectedGuideLine,
                                        uint16_t routePointCount,
                                        uint16_t mappedSegmentCount,
                                        AutoLapRouteBuildPacket& outPacket)
{
    outPacket.valid = valid;
    outPacket.usedGuidePath = usedGuidePath;
    outPacket.normalizedDirection = normalizedDirection;
    outPacket.selectedGuideLine = selectedGuideLine;
    outPacket.routePointCount = routePointCount;
    outPacket.mappedSegmentCount = mappedSegmentCount;
}

inline void SeedAutoLapRouteStepPacket(bool valid,
                                       uint16_t routeIndex,
                                       int16_t observedSegmentId,
                                       int32_t carYawDeg,
                                       const SRL::Math::Types::Vector3D& carWorldPosition,
                                       AutoLapRouteStepPacket& outPacket)
{
    outPacket.valid = valid;
    outPacket.routeIndex = routeIndex;
    outPacket.observedSegmentId = observedSegmentId;
    outPacket.carYawDeg = carYawDeg;
    outPacket.carWorldPosition = carWorldPosition;
}

} // namespace AutoLapRouteDomain
