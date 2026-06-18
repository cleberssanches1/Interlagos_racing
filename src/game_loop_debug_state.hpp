#pragma once

#include <array>
#include <cstdint>

#include "car_system.hpp"
#include "track_system.hpp"

namespace GameLoopRuntime
{

struct SegmentOverlaySnapshot
{
    static constexpr uint8_t kCarXNegBit = 1u << 0;
    static constexpr uint8_t kCarYNegBit = 1u << 1;
    static constexpr uint8_t kCarZNegBit = 1u << 2;
    static constexpr uint8_t kCamXNegBit = 1u << 3;
    static constexpr uint8_t kCamYNegBit = 1u << 4;
    static constexpr uint8_t kCamZNegBit = 1u << 5;

    int16_t windowStartId = -1;
    int8_t windowDir = 1;
    uint8_t windowCount = 0;
    int16_t carSegmentId = -1;
    int16_t nearestSegmentId = -1;
    int32_t carX = 0;
    int32_t carY = 0;
    int32_t carZ = 0;
    int32_t camX = 0;
    int32_t camY = 0;
    int32_t camZ = 0;
    int32_t segY = 0;
    int32_t deltaY = 0;
    int32_t deltaX = 0;
    int32_t deltaZ = 0;
    int32_t camDirX = 0;
    int32_t camDirZ = 0;
    int32_t fwdX = 0;
    int32_t fwdZ = 0;
    uint8_t signFlags = 0u;
    std::array<int16_t, 5> seq{{-1, -1, -1, -1, -1}};

    char CarXSign() const { return (signFlags & kCarXNegBit) ? '-' : '+'; }
    char CarYSign() const { return (signFlags & kCarYNegBit) ? '-' : '+'; }
    char CarZSign() const { return (signFlags & kCarZNegBit) ? '-' : '+'; }
    char CamXSign() const { return (signFlags & kCamXNegBit) ? '-' : '+'; }
    char CamYSign() const { return (signFlags & kCamYNegBit) ? '-' : '+'; }
    char CamZSign() const { return (signFlags & kCamZNegBit) ? '-' : '+'; }
};

struct OverlayDiagnosticsSnapshot
{
    SegmentOverlaySnapshot segment{};
    Game::CarSystem::RuntimeDebugSnapshot carDebug{};
    Game::CarSystem::GameplayInputSnapshot input{};
    uint16_t submittedTrackFaces = 0u;
    uint16_t submittedCarFaces = 0u;
    uint16_t submittedFacesTotal = 0u;
    uint16_t queryCalls = 0u;
    uint16_t queryGlobalPasses = 0u;
    uint16_t queryCacheHits = 0u;
    uint16_t queryCacheMisses = 0u;
    uint16_t wallQueryCalls = 0u;
    uint16_t wallQueryHits = 0u;
};

struct OverlayEventState
{
    int16_t prevCarSegmentId = -1;
    int16_t prevWindowStartId = -1;

    bool CarSegmentChanged(const SegmentOverlaySnapshot& overlay) const
    {
        return (overlay.carSegmentId > 0) &&
               (prevCarSegmentId > 0) &&
               (overlay.carSegmentId != prevCarSegmentId);
    }

    bool WindowStartChanged(const SegmentOverlaySnapshot& overlay) const
    {
        return (overlay.windowStartId > 0) &&
               (prevWindowStartId > 0) &&
               (overlay.windowStartId != prevWindowStartId);
    }

    void Update(const SegmentOverlaySnapshot& overlay)
    {
        if (overlay.carSegmentId > 0) prevCarSegmentId = static_cast<int16_t>(overlay.carSegmentId);
        if (overlay.windowStartId > 0) prevWindowStartId = static_cast<int16_t>(overlay.windowStartId);
    }
};

#if defined(SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY) && SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY
struct HwrStageTrace
{
    struct Snapshot
    {
        uint32_t freeBytes = 0;
        uint32_t usedBlocks = 0;
        uint32_t freeBlocks = 0;
        uint32_t liveBytes = 0;
        uint32_t allocCalls = 0;
        uint32_t freeCalls = 0;
        uint32_t reallocCalls = 0;
        uint32_t failedAllocCalls = 0;
    };

    Snapshot begin{};
    Snapshot gameplay{};
    Snapshot autoLap{};
    Snapshot background{};
    Snapshot hud{};
    Snapshot trackDraw{};
    Snapshot trackEnd{};
    Snapshot car{};
    Snapshot preSync{};
    Snapshot postSync{};
};

struct LwrStageTrace
{
    struct Snapshot
    {
        uint32_t freeBytes = 0;
        uint32_t payloadBytes = 0;
        uint32_t overheadBytes = 0;
        uint32_t freeBlocks = 0;
        uint32_t largestFreeBytes = 0;
    };

    Snapshot begin{};
    Snapshot gameplay{};
    Snapshot autoLap{};
    Snapshot background{};
    Snapshot hud{};
    Snapshot trackDraw{};
    Snapshot trackEnd{};
    Snapshot car{};
    Snapshot preSync{};
    Snapshot postSync{};
};
#else
struct HwrStageTrace
{
    struct Snapshot
    {
        uint32_t freeBytes = 0;
    };

    Snapshot begin{};
    Snapshot gameplay{};
    Snapshot autoLap{};
    Snapshot background{};
    Snapshot hud{};
    Snapshot trackDraw{};
    Snapshot trackEnd{};
    Snapshot car{};
    Snapshot preSync{};
    Snapshot postSync{};
};

struct LwrStageTrace
{
    struct Snapshot
    {
        uint32_t freeBytes = 0;
    };

    Snapshot begin{};
    Snapshot gameplay{};
    Snapshot autoLap{};
    Snapshot background{};
    Snapshot hud{};
    Snapshot trackDraw{};
    Snapshot trackEnd{};
    Snapshot car{};
    Snapshot preSync{};
    Snapshot postSync{};
};
#endif

struct LowWorkTagGroupOverlay
{
    uint32_t initUnknown = 0;
    uint32_t gameplayAuto = 0;
    uint32_t ui = 0;
    uint32_t car = 0;
    uint32_t track = 0;
    uint32_t finishSync = 0;
};

struct LowWorkOverlayState
{
    static constexpr uint8_t kFreeValidBit = 1u << 0;
    static constexpr uint8_t kBreakdownValidBit = 1u << 1;
    static constexpr uint8_t kTagGroupValidBit = 1u << 2;
    static constexpr uint8_t kAllocatorValidBit = 1u << 3;
    uint32_t lastFreeBytes = 0;
    TrackSystem::LowWorkCategoryBreakdown lastBreakdown{};
    LowWorkTagGroupOverlay lastTagGroup{};
    uint32_t lastPayloadBytes = 0;
    uint32_t lastOverheadBytes = 0;
    uint32_t lastFreeBlocks = 0;
    uint8_t flags = 0u;

    bool FreeValid() const { return (flags & kFreeValidBit) != 0u; }
    bool BreakdownValid() const { return (flags & kBreakdownValidBit) != 0u; }
    bool TagGroupValid() const { return (flags & kTagGroupValidBit) != 0u; }
    bool AllocatorValid() const { return (flags & kAllocatorValidBit) != 0u; }
    void SetFreeValid(bool enabled)
    {
        if (enabled) flags |= kFreeValidBit;
        else flags &= static_cast<uint8_t>(~kFreeValidBit);
    }
    void SetBreakdownValid(bool enabled)
    {
        if (enabled) flags |= kBreakdownValidBit;
        else flags &= static_cast<uint8_t>(~kBreakdownValidBit);
    }
    void SetTagGroupValid(bool enabled)
    {
        if (enabled) flags |= kTagGroupValidBit;
        else flags &= static_cast<uint8_t>(~kTagGroupValidBit);
    }
    void SetAllocatorValid(bool enabled)
    {
        if (enabled) flags |= kAllocatorValidBit;
        else flags &= static_cast<uint8_t>(~kAllocatorValidBit);
    }
};

struct DisabledLowWorkOverlayState
{
};

} // namespace GameLoopRuntime
