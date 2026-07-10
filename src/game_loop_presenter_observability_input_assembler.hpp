#pragma once

#include "game_loop_presenter_observability_input_contracts.hpp"
#include "game_loop_presenter_overlay_debug_assembler.hpp"
#include "game_loop_scheduler_reuse_debug_telemetry_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const SchedulerReuseDebugTelemetryPacket& schedulerReuseDebug,
    PresenterObservabilityInputPacket& outPacket)
{
    outPacket.valid = overlay.valid || observability.valid || schedulerReuseDebug.valid;
    outPacket.overlayDebug = BuildPresenterOverlayDebugPacket(overlay, observability);
    outPacket.hasObservability = observability.valid;
    outPacket.hasSchedulerReuseDebug = schedulerReuseDebug.valid;
}

inline void SeedPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    PresenterObservabilityInputPacket& outPacket)
{
    SeedPresenterObservabilityInputPacket(
        overlay,
        observability,
        SchedulerReuseDebugTelemetryPacket{},
        outPacket);
}

inline PresenterObservabilityInputPacket BuildPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability)
{
    PresenterObservabilityInputPacket packet{};
    SeedPresenterObservabilityInputPacket(overlay, observability, packet);
    return packet;
}

inline PresenterObservabilityInputPacket BuildPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const SchedulerReuseDebugTelemetryPacket& schedulerReuseDebug)
{
    PresenterObservabilityInputPacket packet{};
    SeedPresenterObservabilityInputPacket(
        overlay,
        observability,
        schedulerReuseDebug,
        packet);
    return packet;
}

} // namespace GameLoopRuntime
