#pragma once

#include "game_loop_presenter_input_contracts.hpp"
#include "game_loop_presenter_input_summary_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterInputSummaryPacket(const PresenterInputBundle& inputBundle,
                                            PresenterInputSummaryPacket& outPacket)
{
    outPacket.valid = inputBundle.valid;
    outPacket.hasPresentation = inputBundle.presentation.valid;
    outPacket.hasDrivingHud = inputBundle.presentation.drivingHud.valid;
    outPacket.hasPeriodicHud = inputBundle.presentation.periodicHud.valid;
    outPacket.hasRender = inputBundle.renderDebug.valid;
    outPacket.hasRenderableWork = inputBundle.renderDebug.hasRenderableWork;
    outPacket.hasTrack = inputBundle.renderDebug.hasTrack;
    outPacket.hasCar = inputBundle.renderDebug.hasCar;
    outPacket.hasOverlay = inputBundle.observabilityInput.overlayDebug.valid;
    outPacket.hasObservability = inputBundle.observabilityInput.hasObservability;
    outPacket.hasMemoryDebug = inputBundle.observabilityInput.overlayDebug.hasMemoryDebug;
    outPacket.speedKmh = inputBundle.presentation.drivingHud.speedKmh;
    outPacket.gearChar = inputBundle.presentation.drivingHud.gearChar;
    outPacket.engineRpm = inputBundle.presentation.drivingHud.engineRpm;
    outPacket.submittedTrackFaces =
        static_cast<uint16_t>(inputBundle.presentation.periodicHud.submittedTrackFaces);
    outPacket.submittedCarFaces =
        static_cast<uint16_t>(inputBundle.presentation.periodicHud.submittedCarFaces);
    outPacket.submittedFacesTotal = inputBundle.observabilityInput.overlayDebug.submittedFacesTotal;
    outPacket.queryCalls = inputBundle.observabilityInput.overlayDebug.queryCalls;
    outPacket.wallQueryCalls = inputBundle.observabilityInput.overlayDebug.wallQueryCalls;
    outPacket.frameId = inputBundle.observabilityInput.overlayDebug.frameId;
}

inline PresenterInputSummaryPacket BuildPresenterInputSummaryPacket(
    const PresenterInputBundle& inputBundle)
{
    PresenterInputSummaryPacket packet{};
    SeedPresenterInputSummaryPacket(inputBundle, packet);
    return packet;
}

} // namespace GameLoopRuntime
