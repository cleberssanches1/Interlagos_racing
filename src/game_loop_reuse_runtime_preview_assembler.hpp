#pragma once

#include "game_loop_reuse_runtime_debug_bridge_assembler.hpp"
#include "game_loop_reuse_runtime_preview_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline bool HasFrameReuseRuntimeOwnerData(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    return runtimeOwnerPacket.hasSimulationHistory || runtimeOwnerPacket.hasTrackHistory
        || runtimeOwnerPacket.hasSimulationDecision || runtimeOwnerPacket.hasTrackDecision
        || runtimeOwnerPacket.hasTelemetry;
}

inline void SeedReuseRuntimePreviewPacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket,
    const ReuseObservabilityDebugBundle& debugBundle,
    ReuseRuntimePreviewPacket& outPacket)
{
    outPacket.valid = HasFrameReuseRuntimeOwnerData(runtimeOwnerPacket) || debugBundle.valid;
    outPacket.runtimeOwner = runtimeOwnerPacket;
    outPacket.debugBundle = debugBundle;
}

inline ReuseRuntimePreviewPacket BuildReuseRuntimePreviewPacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket,
    const ReuseObservabilityDebugBundle& debugBundle)
{
    ReuseRuntimePreviewPacket packet{};
    SeedReuseRuntimePreviewPacket(runtimeOwnerPacket, debugBundle, packet);
    return packet;
}

inline ReuseRuntimePreviewPacket BuildReuseRuntimePreviewPacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    ReuseObservabilityDebugBundle debugBundle{};
    TryBuildReuseObservabilityDebugBundle(runtimeOwnerPacket, debugBundle);
    return BuildReuseRuntimePreviewPacket(runtimeOwnerPacket, debugBundle);
}

} // namespace GameLoopObservabilityDomain
