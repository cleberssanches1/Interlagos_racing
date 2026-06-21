#pragma once

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
    outBundle.overlay = overlay;
    outBundle.observability = observability;
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

} // namespace GameLoopRuntime
