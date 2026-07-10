#pragma once

#include <srl.hpp>

#include "game_loop_presenter_boundary_text_assembler.hpp"
#include "game_loop_presenter_boundary_view_contracts.hpp"

namespace GameLoopRuntime
{

inline void PresentPresenterBoundaryStatusTextPacket(
    const PresenterBoundaryStatusTextPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    SRL::Debug::Print(
        1,
        31,
        "PV %c %3d %5d o:%u s:%u",
        packet.gearChar,
        static_cast<int>(packet.speedKmh),
        static_cast<int>(packet.engineRpm),
        static_cast<unsigned>(packet.hasObservability ? 1u : 0u),
        static_cast<unsigned>(packet.hasSchedulerReuseDebug ? 1u : 0u));
}

inline void PresentPresenterBoundaryDecisionTextPacket(
    const PresenterBoundaryDecisionTextPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    SRL::Debug::Print(
        1,
        32,
        "PD h:%u p:%u r:%u ov:%u sr:%u",
        static_cast<unsigned>(packet.shouldPresentDrivingHud ? 1u : 0u),
        static_cast<unsigned>(packet.shouldPresentPeriodicHud ? 1u : 0u),
        static_cast<unsigned>(packet.shouldPresentRenderDebug ? 1u : 0u),
        static_cast<unsigned>(packet.shouldPresentOverlayDebug ? 1u : 0u),
        static_cast<unsigned>(packet.shouldPresentSchedulerReuseDebug ? 1u : 0u));
}

inline void PresentPresenterBoundaryTextPacket(
    const PresenterBoundaryTextPacket& packet)
{
    if (!packet.valid)
    {
        return;
    }

    PresentPresenterBoundaryStatusTextPacket(packet.status);
    PresentPresenterBoundaryDecisionTextPacket(packet.decision);
}

inline void PresentPresenterBoundaryViewPacket(const PresenterBoundaryViewPacket& packet)
{
    PresentPresenterBoundaryTextPacket(BuildPresenterBoundaryTextPacket(packet));
}

inline void PresentPresenterBoundaryDecisionPacket(const PresenterBoundaryViewPacket& packet)
{
    PresentPresenterBoundaryDecisionTextPacket(
        BuildPresenterBoundaryDecisionTextPacket(packet));
}

} // namespace GameLoopRuntime
