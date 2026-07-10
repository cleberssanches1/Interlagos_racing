#pragma once

#include <srl.hpp>

#include "game_loop_track_reuse_preview_contracts.hpp"

namespace GameLoopRuntime
{

inline void PresentTrackReuseDecisionViewPacket(
    const TrackReuseDecisionViewPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    SRL::Debug::Print(
        1,
        25,
        "TRD c:%u k:%u l:%u f:%u s:%u/%u",
        static_cast<unsigned>(packet.shouldConsumeCommitted ? 1u : 0u),
        static_cast<unsigned>(packet.shouldKickProducer ? 1u : 0u),
        static_cast<unsigned>(packet.requiresLockstepWait ? 1u : 0u),
        static_cast<unsigned>(packet.requiresSynchronousFallback ? 1u : 0u),
        static_cast<unsigned>(packet.consumeSlot),
        static_cast<unsigned>(packet.dispatchSlot));
}

inline void PresentTrackReuseTelemetryViewPacket(
    const TrackReuseTelemetryViewPacket& packet)
{
    SRL::Debug::Print(
        1,
        26,
        "TRT c:%u p:%u l:%u f:%u",
        static_cast<unsigned>(packet.trackPacketsCommitted),
        static_cast<unsigned>(packet.trackPreviousFrameConsumes),
        static_cast<unsigned>(packet.trackLockstepConsumes),
        static_cast<unsigned>(packet.trackFallbacks));
}

inline void PresentTrackReusePreviewPacket(
    const TrackReusePreviewPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    PresentTrackReuseDecisionViewPacket(packet.decision);
    PresentTrackReuseTelemetryViewPacket(packet.telemetry);
}

} // namespace GameLoopRuntime
