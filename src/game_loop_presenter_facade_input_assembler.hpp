#pragma once

#include "game_loop_presenter_facade_input_contracts.hpp"
#include "game_loop_presenter_input_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterFacadeInputPacket(const PresenterInputBundle& inputBundle,
                                           PresenterFacadeInputPacket& outPacket)
{
    outPacket.valid = inputBundle.valid;
    outPacket.drivingHud = inputBundle.presentation.drivingHud;
    outPacket.periodicHud = inputBundle.presentation.periodicHud;
    outPacket.overlay = inputBundle.observabilityInput.overlayDebug;
}

inline PresenterFacadeInputPacket BuildPresenterFacadeInputPacket(
    const PresenterInputBundle& inputBundle)
{
    PresenterFacadeInputPacket packet{};
    SeedPresenterFacadeInputPacket(inputBundle, packet);
    return packet;
}

inline void SeedPresenterFacadeInputPacket(
    const PresentationDebugBundle& presentation,
    const RenderFrameDebugBundle& render,
    const PresenterObservabilityInputPacket& observability,
    PresenterFacadeInputPacket& outPacket)
{
    outPacket.valid = presentation.valid || render.valid || observability.valid;
    outPacket.drivingHud = presentation.drivingHud;
    outPacket.periodicHud = presentation.periodicHud;
    outPacket.overlay = observability.overlayDebug;
}

inline PresenterFacadeInputPacket BuildPresenterFacadeInputPacket(
    const PresentationDebugBundle& presentation,
    const RenderFrameDebugBundle& render,
    const PresenterObservabilityInputPacket& observability)
{
    PresenterFacadeInputPacket packet{};
    SeedPresenterFacadeInputPacket(presentation, render, observability, packet);
    return packet;
}

} // namespace GameLoopRuntime
