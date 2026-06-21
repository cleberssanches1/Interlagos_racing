#pragma once

#include <cstdint>

#include "game_loop_presenter_facade_contracts.hpp"

namespace GameLoopRuntime
{

enum class PresenterFacadePhase : uint8_t
{
    None = 0,
    Summary,
    Hud,
    Render,
    Overlay
};

struct PresenterFacadeRequestPacket
{
    bool valid = false;
    PresenterFacadePacket facade{};
};

struct PresenterFacadeDecisionPacket
{
    bool valid = false;
    PresenterFacadePhase phase = PresenterFacadePhase::None;
    bool shouldPresentHud = false;
    bool shouldPresentPeriodicHud = false;
    bool shouldPresentRenderDebug = false;
    bool shouldPresentOverlayDebug = false;
    bool shouldPresentMemoryDebug = false;
};

} // namespace GameLoopRuntime
