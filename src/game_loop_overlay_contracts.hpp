#pragma once

#include <cstdint>

#include "car_system.hpp"
#include "game_loop_debug_state.hpp"
#include "track_render_contracts.hpp"

class TrackSystem;

namespace GameLoopOverlayDomain
{

enum class Stage : uint8_t
{
    SegmentAssembly = 0,
    WindowAssembly,
    NearestSegmentAssembly,
    DiagnosticsAssembly,
    EventTracking
};

struct Ports
{
    TrackSystem* track = nullptr;
    Game::CarSystem* car = nullptr;
};

struct SegmentFrameContext
{
    int16_t latestActiveSegmentId = -1;
    int32_t carYawDeg = 0;
    SRL::Math::Types::Vector3D carWorldPosition{};
    SRL::Math::Types::Vector3D trackSegOffset{};
    SRL::Math::Types::Vector3D cameraLocation{};
    SRL::Math::Types::Vector3D cameraLookTarget{};
};

struct SegmentOverlayPacket
{
    bool valid = false;
    bool windowValid = false;
    bool centerValid = false;
    GameLoopRuntime::SegmentOverlaySnapshot snapshot{};
};

struct WindowOverlayPacket
{
    bool valid = false;
    bool windowValid = false;
    int16_t windowStartId = -1;
    int8_t windowDir = 1;
    uint8_t windowCount = 0;
    std::array<int16_t, 5> sequence{{-1, -1, -1, -1, -1}};
};

struct NearestSegmentPacket
{
    bool valid = false;
    int16_t activeSegmentId = -1;
    int16_t nearestSegmentId = -1;
};

struct OverlayDiagnosticsPacket
{
    bool valid = false;
    bool hasTrackTelemetry = false;
    uint32_t submittedTrackFaces = 0u;
    uint32_t submittedCarFaces = 0u;
    Game::CarSystem::RuntimeDebugSnapshot carDebug{};
    Game::CarSystem::GameplayInputSnapshot input{};
    GameLoopRuntime::OverlayDiagnosticsSnapshot snapshot{};
    TrackRenderDomain::TrackRenderTelemetry trackTelemetry{};
};

struct OverlayEventPacket
{
    bool valid = false;
    bool carSegmentChanged = false;
    bool windowStartChanged = false;
    int16_t prevCarSegmentId = -1;
    int16_t nextCarSegmentId = -1;
    int16_t prevWindowStartId = -1;
    int16_t nextWindowStartId = -1;
};

} // namespace GameLoopOverlayDomain
