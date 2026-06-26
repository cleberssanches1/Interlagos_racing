#pragma once

#include "game_loop_presenter_observability_input_assembler.hpp"
#include "game_loop_presenter_overlay_debug_assembler.hpp"
#include "game_loop_presenter_render_debug_assembler.hpp"
#include "game_loop_presenter_input_summary_assembler.hpp"
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
    outBundle.render = render;
    outBundle.renderDebug = BuildPresenterRenderDebugPacket(render);
    outBundle.observabilityInput = BuildPresenterObservabilityInputPacket(overlay, observability);
    outBundle.overlay = overlay;
    outBundle.overlayDebug = BuildPresenterOverlayDebugPacket(overlay, observability);
    outBundle.observability = observability;
    outBundle.summary = BuildPresenterInputSummaryPacket(outBundle);
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
    const PresenterObservabilityInputPacket& observabilityInput,
    PresenterInputBundle& outBundle)
{
    outBundle.valid = true;
    outBundle.presentation = presentation;
    outBundle.render = render;
    outBundle.renderDebug = BuildPresenterRenderDebugPacket(render);
    outBundle.observabilityInput = observabilityInput;
    outBundle.overlay = observabilityInput.overlay;
    outBundle.overlayDebug = observabilityInput.overlayDebug;
    outBundle.observability = observabilityInput.observability;
    outBundle.summary = BuildPresenterInputSummaryPacket(outBundle);
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
