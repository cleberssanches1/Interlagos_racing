#pragma once

#include <cstddef>
#include <cstdint>

#include "car_system.hpp"
#include "game_loop_telemetry_contracts.hpp"

namespace GameLoopRuntime
{

struct DrivingHudTextPacket
{
    bool valid = false;
    int16_t speedKmh = 0;
    char gearChar = 'N';
    int16_t engineRpm = 0;
    uint8_t shiftFrames = 0u;
    int16_t shiftRpmBefore = 0;
    int16_t shiftRpmAfter = 0;
};

struct PeriodicHudStatsPacket
{
    bool valid = false;
    uint32_t frameCounter = 0u;
    bool logTrack = false;
    bool logCar = false;
    uint32_t faceCount = 0u;
    uint32_t vertexCount = 0u;
    uint32_t submittedTrackFaces = 0u;
    uint32_t submittedCarFaces = 0u;
    uint32_t submittedFacesNow = 0u;
    uint32_t avgFaces = 0u;
    uint32_t vdp1Used = 0u;
    uint32_t peakVdp1Used = 0u;
    uint32_t vdp1Free = 0u;
    uint32_t vdp1Pct = 0u;
    uint32_t peakPct = 0u;
    uint32_t vdp1HeapPctFiltered = 0u;
    uint16_t vdp1TexCount = 0u;
    int32_t hwrUsed = 0;
    int32_t hwrFree = 0;
    int32_t hwrPct10 = 0;
};

struct PresentationDebugBundle
{
    bool valid = false;
    DrivingHudTextPacket drivingHud{};
    PeriodicHudStatsPacket periodicHud{};
};

} // namespace GameLoopRuntime
