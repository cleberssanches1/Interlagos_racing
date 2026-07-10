#pragma once

#include "frame_reuse_observability_capture_ops.hpp"

#include "game_loop_simulation_reuse_runtime_bridge_assembler.hpp"
#include "game_loop_simulation_reuse_runtime_state_contracts.hpp"
#include "game_loop_track_reuse_runtime_state_ops.hpp"

namespace GameLoopRuntime
{

inline FrameReuseDomain::FrameReuseRuntimeOwnerPacket
BuildSimulationTrackReuseRuntimeOwnerPacket(
    const SimulationReuseRuntimeState& simulationState,
    uint32_t simulationRequestFrameId,
    bool slaveSimulationEnabled,
    bool simulationLockstepEnabled,
    bool simulationJobInFlight,
    const TrackReuseRuntimeState& trackState,
    bool trackLockstepEnabled = true,
    const FrameReuseDomain::FrameReuseTelemetry* telemetry = nullptr)
{
    const SimulationReuseRuntimeDecisionInputsPacket simulationInputs =
        BuildSimulationReuseRuntimeDecisionInputsPacket(simulationRequestFrameId,
                                                        slaveSimulationEnabled,
                                                        simulationLockstepEnabled,
                                                        simulationJobInFlight,
                                                        simulationState.history);
    const FrameReuseDomain::SimulationReuseDecisionPacket simulationDecision =
        BuildSimulationReuseRuntimeDecisionPacket(simulationInputs);
    const FrameReuseDomain::TrackReuseDecisionPacket trackDecision =
        BuildTrackReuseRuntimeDecisionPacket(trackState, trackLockstepEnabled);

    const FrameReuseDomain::SimulationReuseDecisionPacket* simulationDecisionPtr =
        simulationDecision.valid ? &simulationDecision : nullptr;
    const FrameReuseDomain::TrackReuseDecisionPacket* trackDecisionPtr =
        trackDecision.valid ? &trackDecision : nullptr;

    return FrameReuseDomain::CaptureFrameReuseRuntimeOwnerPacket(&simulationState.history,
                                                                 &trackState.history,
                                                                 simulationDecisionPtr,
                                                                 trackDecisionPtr,
                                                                 telemetry);
}

} // namespace GameLoopRuntime
