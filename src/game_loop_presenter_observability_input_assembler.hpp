#pragma once

#include "game_loop_presenter_observability_input_contracts.hpp"
#include "game_loop_presenter_overlay_debug_assembler.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    PresenterObservabilityInputPacket& outPacket)
{
    outPacket.valid = overlay.valid || observability.valid;
    outPacket.overlayDebug = BuildPresenterOverlayDebugPacket(overlay, observability);
    outPacket.hasObservability = observability.valid;
}

inline PresenterObservabilityInputPacket BuildPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability)
{
    PresenterObservabilityInputPacket packet{};
    SeedPresenterObservabilityInputPacket(overlay, observability, packet);
    return packet;
}

} // namespace GameLoopRuntime
