#pragma once

#include "game_loop_presenter_observability_input_assembler.hpp"
#include "game_loop_presenter_render_debug_assembler.hpp"
#include "game_loop_scheduler_reuse_debug_telemetry_contracts.hpp"
#include "game_loop_presenter_input_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterInputBundle(
    const PresentationDebugBundle& presentation,
    const RenderFrameDebugBundle& render,
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    PresenterInputBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.presentation = presentation;
    outBundle.renderDebug = BuildPresenterRenderDebugPacket(render);
    outBundle.observabilityInput = BuildPresenterObservabilityInputPacket(overlay, observability);
}

inline PresenterInputBundle BuildPresenterInputBundle(
    const PresentationDebugBundle& presentation,
    const RenderFrameDebugBundle& render,
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability)
{
    PresenterInputBundle bundle{};
    SeedPresenterInputBundle(presentation, render, overlay, observability, bundle);
    return bundle;
}

inline void SeedPresenterInputBundle(
    const PresentationDebugBundle& presentation,
    const RenderFrameDebugBundle& render,
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const SchedulerReuseDebugTelemetryPacket& schedulerReuseDebug,
    PresenterInputBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.presentation = presentation;
    outBundle.renderDebug = BuildPresenterRenderDebugPacket(render);
    outBundle.observabilityInput = BuildPresenterObservabilityInputPacket(
        overlay,
        observability,
        schedulerReuseDebug);
}

inline PresenterInputBundle BuildPresenterInputBundle(
    const PresentationDebugBundle& presentation,
    const RenderFrameDebugBundle& render,
    const GameLoopObservabilityDomain::OverlayDebugBundle& overlay,
    const GameLoopObservabilityDomain::ObservabilityDebugBundle& observability,
    const SchedulerReuseDebugTelemetryPacket& schedulerReuseDebug)
{
    PresenterInputBundle bundle{};
    SeedPresenterInputBundle(
        presentation,
        render,
        overlay,
        observability,
        schedulerReuseDebug,
        bundle);
    return bundle;
}

inline void SeedPresenterInputBundle(
    const PresentationDebugBundle& presentation,
    const RenderFrameDebugBundle& render,
    const PresenterObservabilityInputPacket& observabilityInput,
    PresenterInputBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.presentation = presentation;
    outBundle.renderDebug = BuildPresenterRenderDebugPacket(render);
    outBundle.observabilityInput = observabilityInput;
}

inline PresenterInputBundle BuildPresenterInputBundle(
    const PresentationDebugBundle& presentation,
    const RenderFrameDebugBundle& render,
    const PresenterObservabilityInputPacket& observabilityInput)
{
    PresenterInputBundle bundle{};
    SeedPresenterInputBundle(presentation, render, observabilityInput, bundle);
    return bundle;
}

} // namespace GameLoopRuntime
