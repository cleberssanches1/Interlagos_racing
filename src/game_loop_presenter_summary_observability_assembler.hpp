#pragma once

#include "game_loop_presenter_input_contracts.hpp"
#include "game_loop_presenter_input_summary_assembler.hpp"
#include "game_loop_presenter_summary_observability_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedPresenterSummaryObservabilityPacket(
    const PresenterInputSummaryPacket& summary,
    const PresenterObservabilityInputPacket& observabilityInput,
    PresenterSummaryObservabilityPacket& outPacket)
{
    outPacket.valid = summary.valid || observabilityInput.valid;
    outPacket.summary = summary;
    outPacket.observabilityInput = observabilityInput;
}

inline PresenterSummaryObservabilityPacket BuildPresenterSummaryObservabilityPacket(
    const PresenterInputSummaryPacket& summary,
    const PresenterObservabilityInputPacket& observabilityInput)
{
    PresenterSummaryObservabilityPacket packet{};
    SeedPresenterSummaryObservabilityPacket(summary, observabilityInput, packet);
    return packet;
}

inline PresenterSummaryObservabilityPacket BuildPresenterSummaryObservabilityPacket(
    const PresenterInputBundle& inputBundle)
{
    return BuildPresenterSummaryObservabilityPacket(
        BuildPresenterInputSummaryPacket(inputBundle),
        inputBundle.observabilityInput);
}

} // namespace GameLoopRuntime
