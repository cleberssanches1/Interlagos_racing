#pragma once

#include "game_loop_presenter_compile_only_preview_contracts.hpp"
#include "game_loop_presenter_facade_decision_bridge_assembler.hpp"
#include "game_loop_presenter_facade_decision_input_assembler.hpp"
#include "game_loop_presenter_frame_end_preview_assembler.hpp"
#include "game_loop_presenter_hud_telemetry_preview_assembler.hpp"
#include "game_loop_presenter_input_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterCompileOnlyPreviewPacket(
    const PresenterFrameEndPreviewPacket& frameEnd,
    const PresenterHudTelemetryPreviewPacket& hudTelemetry,
    PresenterCompileOnlyPreviewPacket& outPacket)
{
    outPacket.valid = frameEnd.valid || hudTelemetry.valid;
    outPacket.frameEnd = frameEnd;
    outPacket.hudTelemetry = hudTelemetry;
}

inline PresenterCompileOnlyPreviewPacket BuildPresenterCompileOnlyPreviewPacket(
    const PresenterFrameEndPreviewPacket& frameEnd,
    const PresenterHudTelemetryPreviewPacket& hudTelemetry)
{
    PresenterCompileOnlyPreviewPacket packet{};
    SeedPresenterCompileOnlyPreviewPacket(frameEnd, hudTelemetry, packet);
    return packet;
}

inline PresenterCompileOnlyPreviewPacket BuildPresenterCompileOnlyPreviewPacket(
    const PresenterInputBundle& inputBundle,
    const PresenterHudTelemetryDecisionInputPacket& hudTelemetryInput)
{
    const PresenterFacadeDecisionBridgePacket decisionBridge =
        BuildPresenterFacadeDecisionBridgePacket(
            BuildPresenterFacadeDecisionInputPacket(inputBundle),
            hudTelemetryInput);
    return BuildPresenterCompileOnlyPreviewPacket(
        BuildPresenterFrameEndPreviewPacket(decisionBridge),
        BuildPresenterHudTelemetryPreviewPacket(decisionBridge));
}

inline PresenterCompileOnlyPreviewPacket BuildPresenterCompileOnlyPreviewPacket(
    const PresenterFacadeInputPacket& facadeInput,
    const PresenterHudTelemetryDecisionInputPacket& hudTelemetryInput)
{
    const PresenterFacadeDecisionBridgePacket decisionBridge =
        BuildPresenterFacadeDecisionBridgePacket(
            BuildPresenterFacadeDecisionInputPacket(facadeInput),
            hudTelemetryInput);
    return BuildPresenterCompileOnlyPreviewPacket(
        BuildPresenterFrameEndPreviewPacket(decisionBridge),
        BuildPresenterHudTelemetryPreviewPacket(decisionBridge));
}

} // namespace GameLoopRuntime
