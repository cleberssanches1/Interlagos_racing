#pragma once

#include <cstdint>

#include "frame_pipeline_contracts.hpp"
#include "interfaces.hpp"
#include "simulation_scheduler_state.hpp"

namespace SimulationSchedulerDomain
{

enum class Stage : uint8_t
{
    FrameContextAssembly = 0,
    DispatchAssembly,
    DrainAssembly,
    CompletionAssembly,
    TelemetryAssembly
};

struct Ports
{
    Game::IGameplayTick* gameplayTick = nullptr;
    Game::ICarPhysics* carPhysics = nullptr;
    Game::ITrackCollisionQuery* trackCollision = nullptr;
};

struct SimulationFrameContext
{
    uint32_t frameId = 0u;
    Game::GameplayFrameState frameState{};
    Game::SimulationSchedulerPolicy policy{};
    Game::SimulationDispatchMode dispatchMode = Game::SimulationDispatchMode::Synchronous;
    bool slaveSimulationEnabled = false;
    bool slaveLockstep = true;
    bool trackProducerBusy = false;
    bool carPrepareBusy = false;
};

struct SimulationDispatchPacket
{
    bool valid = false;
    bool useSlave = false;
    bool blockedByBackoff = false;
    bool blockedByTrackBusy = false;
    bool blockedByInFlightJob = false;
    bool blockedByCarPrepare = false;
    bool requiresLockstepDrain = false;
    uint8_t targetSlot = 0u;
    Game::SimulationPayload payload{};
};

struct SimulationDrainPacket
{
    bool valid = false;
    bool mandatoryWait = false;
    uint32_t softSpinLimit = 0u;
    uint32_t hardSpinLimit = 0u;
};

struct SimulationCompletionPacket
{
    bool valid = false;
    bool jobInFlight = false;
    bool hasCompleted = false;
    uint8_t inFlightIdx = 0u;
    uint8_t completedIdx = 0u;
};

struct SimulationSchedulerTelemetry
{
    uint32_t slaveDispatchCount = 0u;
    uint32_t slaveDispatchSkipsTrackBusy = 0u;
    uint32_t slaveDispatchSkipsBackoff = 0u;
    uint32_t drainSoftTimeouts = 0u;
    uint32_t drainHardWaits = 0u;
    uint16_t masterWaitTicksThisFrame = 0u;
    uint16_t slaveLastJobTicksThisFrame = 0u;
    uint8_t slaveBackoffFrames = 0u;
    bool jobInFlight = false;
    bool hasCompleted = false;
};

} // namespace SimulationSchedulerDomain
