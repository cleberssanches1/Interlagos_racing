#pragma once

#include <cstdint>
#include <memory>

#include "car_system.hpp"
#include "interfaces.hpp"

// Contratos passivos do domínio do carro.
// Não devem alterar runtime por si só; servem para guiar futuras extrações
// fora do loop crítico.

namespace CarDomain
{

enum class Stage : uint8_t
{
    InputAssembly = 0,
    GameplayStateAssembly,
    SimulationApply,
    RenderStateAssembly,
    AudioInterpretation
};

struct InputPorts
{
    Game::ICarCommand* command = nullptr;
};

struct SimulationPorts
{
    Game::ICarPhysics* physics = nullptr;
    Game::ITrackCollisionQuery* trackCollision = nullptr;
};

struct RenderPorts
{
    std::unique_ptr<Game::CarSystem>* car = nullptr;
    RenderPipeline* renderPipeline = nullptr;
};

struct AudioPorts
{
    Game::IAudioEvents* audioEvents = nullptr;
};

struct GameplayAssemblyContext
{
    uint32_t frameId = 0u;
    bool autoLapEnabled = false;
    Game::CarSystem::GameplayInputSnapshot input{};
    SRL::Math::Types::Vector3D worldPosition{};
    int32_t yawDeg = 0;
};

struct SimulationApplyContext
{
    Game::GameplayFrameState frameState{};
};

struct RenderAssemblyContext
{
    SRL::Math::Types::Vector3D worldPosition{};
    int32_t gameplayYawDeg = 0;
    int32_t renderYawDeg = 0;
};

struct AudioFrameContext
{
    Game::GameplayFrameState frameState{};
};

} // namespace CarDomain
