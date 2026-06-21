#pragma once

#include <cstdint>

namespace GameLoopRuntime
{

struct PresenterInputSummaryPacket
{
    bool valid = false;
    bool hasPresentation = false;
    bool hasDrivingHud = false;
    bool hasPeriodicHud = false;
    bool hasRender = false;
    bool hasRenderableWork = false;
    bool hasTrack = false;
    bool hasCar = false;
    bool hasOverlay = false;
    bool hasObservability = false;
    bool hasMemoryDebug = false;
    int16_t speedKmh = 0;
    char gearChar = 'N';
    int16_t engineRpm = 0;
    uint16_t submittedTrackFaces = 0u;
    uint16_t submittedCarFaces = 0u;
    uint16_t submittedFacesTotal = 0u;
    uint16_t queryCalls = 0u;
    uint16_t wallQueryCalls = 0u;
    uint32_t frameId = 0u;
};

} // namespace GameLoopRuntime
