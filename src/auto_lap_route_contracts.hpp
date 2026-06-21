#pragma once

#include <array>
#include <cstdint>

#include "track_system.hpp"

namespace AutoLapRouteDomain
{

enum class Stage : uint8_t
{
    GuideLoad = 0,
    RouteBuild,
    RouteInitialize,
    RouteAdvance,
    Lifecycle
};

struct Ports
{
    TrackSystem* track = nullptr;
};

struct AutoLapFrameContext
{
    bool autoLapEnabled = false;
    bool trackReady = false;
    int16_t stepUnits = 0;
    int16_t latestActiveSegmentId = -1;
    SRL::Math::Types::Vector3D trackSegOffset{};
    SRL::Math::Types::Vector3D referenceCarWorldPosition{};
    SRL::Math::Types::Vector3D carWorldPosition{};
    int32_t carYawDeg = 0;
};

struct AutoLapRouteStorageSnapshot
{
    bool initialized = false;
    bool built = false;
    bool startupYawAligned = false;
    uint16_t index = 0u;
    uint16_t idCount = 0u;
    uint16_t centerCount = 0u;
    uint16_t yawCount = 0u;
    uint16_t offCount = 0u;
    int16_t baseYawDeg = 0;
    int16_t currentOffDeg = 0;
    int8_t selectedGuideLine = -1;
    std::array<uint16_t, 3> guideLinePointCounts{};
};

struct AutoLapGuideLoadPacket
{
    bool attempted = false;
    bool loaded = false;
    bool parsed = false;
    const char* loadedCandidate = nullptr;
    uint32_t byteCount = 0u;
    uint32_t parsedVersion = 0u;
    std::array<uint16_t, 3> parsedLinePointCounts{};
};

struct AutoLapRouteBuildPacket
{
    bool valid = false;
    bool usedGuidePath = false;
    bool normalizedDirection = false;
    int8_t selectedGuideLine = -1;
    uint16_t routePointCount = 0u;
    uint16_t mappedSegmentCount = 0u;
};

struct AutoLapGuideRouteTrace
{
    bool usedGuidePath = false;
    bool normalizedDirection = false;
    int8_t selectedGuideLine = -1;
    uint16_t rawPointCount = 0u;
    uint16_t outputPointCount = 0u;
};

struct AutoLapRouteStepPacket
{
    bool valid = false;
    uint16_t routeIndex = 0u;
    int16_t observedSegmentId = -1;
    int32_t carYawDeg = 0;
    SRL::Math::Types::Vector3D carWorldPosition{};
};

} // namespace AutoLapRouteDomain
