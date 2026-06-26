#pragma once

#include "game_loop_presenter_facade_contracts.hpp"
#include "game_loop_presenter_facade_input_contracts.hpp"
#include "game_loop_presenter_input_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterFacadePacket(const PresenterInputBundle& inputBundle,
                                      PresenterFacadePacket& outPacket)
{
    outPacket.valid = inputBundle.valid;
    outPacket.summary = inputBundle.summary;
    outPacket.drivingHud = inputBundle.presentation.drivingHud;
    outPacket.periodicHud = inputBundle.presentation.periodicHud;
    outPacket.render = inputBundle.renderDebug;
    outPacket.overlay = inputBundle.overlayDebug;
}

inline PresenterFacadePacket BuildPresenterFacadePacket(const PresenterInputBundle& inputBundle)
{
    PresenterFacadePacket packet{};
    SeedPresenterFacadePacket(inputBundle, packet);
    return packet;
}

inline void SeedPresenterFacadePacket(const PresenterFacadeInputPacket& facadeInput,
                                      PresenterFacadePacket& outPacket)
{
    outPacket.valid = facadeInput.valid;
    outPacket.summary = facadeInput.summary;
    outPacket.drivingHud = facadeInput.drivingHud;
    outPacket.periodicHud = facadeInput.periodicHud;
    outPacket.render = facadeInput.render;
    outPacket.overlay = facadeInput.observability.overlayDebug;
}

inline PresenterFacadePacket BuildPresenterFacadePacket(
    const PresenterFacadeInputPacket& facadeInput)
{
    PresenterFacadePacket packet{};
    SeedPresenterFacadePacket(facadeInput, packet);
    return packet;
}

} // namespace GameLoopRuntime
