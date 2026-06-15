#pragma once

#include <memory>

#include "background_manager.hpp"
#include "camera_system.hpp"
#include "car_system.hpp"
#include "hud_system.hpp"
#include "interfaces.hpp"
#include "render_pipeline.hpp"
#include "track_system.hpp"

// Contratos passivos para documentar e estabilizar as fronteiras entre
// componentes antes de qualquer refatoração no caminho crítico do frame.
// Este arquivo não deve alterar comportamento por si só.

namespace RuntimeBoundaries
{

struct FrameOrchestratorPorts
{
    BackgroundManager* background = nullptr;
    CameraSystem* camera = nullptr;
    TrackSystem* track = nullptr;
    std::unique_ptr<Game::CarSystem>* car = nullptr;
    RenderPipeline* renderPipeline = nullptr;
    HudSystem* hud = nullptr;
};

struct SimulationPorts
{
    Game::IGameplayTick* gameplayTick = nullptr;
    Game::ICarPhysics* carPhysics = nullptr;
    Game::ITrackCollisionQuery* trackCollision = nullptr;
};

struct AudioPorts
{
    Game::IAudioEvents* audioEvents = nullptr;
};

struct CarPorts
{
    std::unique_ptr<Game::CarSystem>* car = nullptr;
};

struct TrackPorts
{
    TrackSystem* track = nullptr;
    Game::ITrackCollisionQuery* trackCollision = nullptr;
};

struct PresentationPorts
{
    BackgroundManager* background = nullptr;
    CameraSystem* camera = nullptr;
    RenderPipeline* renderPipeline = nullptr;
    HudSystem* hud = nullptr;
};

} // namespace RuntimeBoundaries
