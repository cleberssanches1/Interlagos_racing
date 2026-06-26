#pragma once

#include "game_loop_presenter_facade_input_contracts.hpp"
#include "game_loop_presenter_input_contracts.hpp"
#include "game_loop_presenter_input_summary_assembler.hpp"
#include "game_loop_presenter_observability_input_assembler.hpp"
#include "game_loop_presenter_render_debug_assembler.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterFacadeInputPacket(const PresenterInputBundle& inputBundle,
                                           PresenterFacadeInputPacket& outPacket)
{
    outPacket.valid = inputBundle.valid;
    outPacket.summary = inputBundle.summary;
    outPacket.drivingHud = inputBundle.presentation.drivingHud;
    outPacket.periodicHud = inputBundle.presentation.periodicHud;
    outPacket.render = inputBundle.renderDebug;
    outPacket.observability = inputBundle.observabilityInput;
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
    PresenterInputBundle summaryInput{};
    summaryInput.valid = presentation.valid || render.valid || observability.valid;
    summaryInput.presentation = presentation;
    summaryInput.render = render;
    summaryInput.renderDebug = BuildPresenterRenderDebugPacket(render);
    summaryInput.observabilityInput = observability;
    summaryInput.overlay = observability.overlay;
    summaryInput.overlayDebug = observability.overlayDebug;
    summaryInput.observability = observability.observability;
    summaryInput.summary = PresenterInputSummaryPacket{};

    outPacket.valid = presentation.valid || render.valid || observability.valid;
    outPacket.summary = BuildPresenterInputSummaryPacket(summaryInput);
    outPacket.drivingHud = presentation.drivingHud;
    outPacket.periodicHud = presentation.periodicHud;
    outPacket.render = summaryInput.renderDebug;
    outPacket.observability = observability;
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
