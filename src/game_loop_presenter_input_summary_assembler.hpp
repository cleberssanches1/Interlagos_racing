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
    outPacket.hasRender = inputBundle.render.valid;
    outPacket.hasRenderableWork = inputBundle.renderDebug.hasRenderableWork;
    outPacket.hasTrack = inputBundle.renderDebug.hasTrack;
    outPacket.hasCar = inputBundle.renderDebug.hasCar;
    outPacket.hasOverlay = inputBundle.overlayDebug.valid;
    outPacket.hasObservability = inputBundle.observability.valid;
    outPacket.hasSchedulerReuseDebug = inputBundle.observabilityInput.schedulerReuse.valid;
    outPacket.hasMemoryDebug = inputBundle.overlayDebug.hasMemoryDebug;
    outPacket.speedKmh = inputBundle.presentation.drivingHud.speedKmh;
    outPacket.gearChar = inputBundle.presentation.drivingHud.gearChar;
    outPacket.engineRpm = inputBundle.presentation.drivingHud.engineRpm;
    outPacket.submittedTrackFaces = inputBundle.presentation.frame.submittedTrackFaces;
    outPacket.submittedCarFaces = inputBundle.presentation.frame.submittedCarFaces;
    outPacket.submittedFacesTotal = inputBundle.overlayDebug.submittedFacesTotal;
    outPacket.queryCalls = inputBundle.overlayDebug.queryCalls;
    outPacket.wallQueryCalls = inputBundle.overlayDebug.wallQueryCalls;
    outPacket.frameId = inputBundle.overlayDebug.frameId;
}

inline PresenterInputSummaryPacket BuildPresenterInputSummaryPacket(
    const PresenterInputBundle& inputBundle)
{
    PresenterInputSummaryPacket packet{};
    SeedPresenterInputSummaryPacket(inputBundle, packet);
    return packet;
}

} // namespace GameLoopRuntime
