#pragma once

#include "game_loop_presenter_observability_input_contracts.hpp"
#include "game_loop_presenter_overlay_debug_assembler.hpp"
#include "game_loop_scheduler_reuse_debug_telemetry_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const GameLoopTelemetryDomain::SchedulerReuseDebugTelemetryPacket& schedulerReuse,
    PresenterObservabilityInputPacket& outPacket)
{
    outPacket.valid = overlay.valid || observability.valid || schedulerReuse.valid;
    outPacket.overlayDebug = BuildPresenterOverlayDebugPacket(overlay, observability);
    outPacket.hasObservability = observability.valid;
    outPacket.hasSchedulerReuseDebug = schedulerReuse.valid;
}

inline PresenterObservabilityInputPacket BuildPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const GameLoopTelemetryDomain::SchedulerReuseDebugTelemetryPacket& schedulerReuse)
{
    PresenterObservabilityInputPacket packet{};
    SeedPresenterObservabilityInputPacket(overlay, observability, schedulerReuse, packet);
    return packet;
}

inline PresenterObservabilityInputPacket BuildPresenterObservabilityInputPacket(
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability)
{
    return BuildPresenterObservabilityInputPacket(
        overlay,
        observability,
        GameLoopTelemetryDomain::SchedulerReuseDebugTelemetryPacket{});
}

} // namespace GameLoopRuntime
