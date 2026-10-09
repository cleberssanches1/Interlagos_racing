#pragma once

#include <cstdint>

#include "interfaces.hpp"
#include "sh2_frt_profiler.hpp"
#include "simulation_scheduler_state.hpp"

namespace Game
{

// SH-2 cache is not coherent. Slave writes and Master reads of the sim packet
// go through the uncached mirror, or the Master keeps a stale "not done".
template <typename T>
inline T* Sh2CacheThrough(T* ptr)
{
    constexpr uintptr_t kCacheThroughMask = 0x20000000u;
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr) | kCacheThroughMask);
}

class SimulationTask final : public SRL::Types::ITask
{
public:
    void Configure(const SimulationPayload* input,
                   SimulationPayload* output,
                   Game::IGameplayTick* gameplayTick,
                   Game::ICarPhysics* carPhysics,
                   Game::ITrackCollisionQuery* trackCollision)
    {
        input_ = input;
        output_ = output;
        gameplayTick_ = gameplayTick;
        carPhysics_ = carPhysics;
        trackCollision_ = trackCollision;
    }

    uint16_t LastTicks() const
    {
        return *Sh2CacheThrough(const_cast<uint16_t*>(&lastTicks_));
    }

    void ResetTask() override
    {
        *Sh2CacheThrough(&doneFlag_) = 0u;
        SRL::Types::ITask::ResetTask();
    }

    bool IsDone() override
    {
        return *Sh2CacheThrough(&doneFlag_) != 0u;
    }

private:
    void Do() override
    {
        const SimulationPayload* const input = *Sh2CacheThrough(&input_);
        SimulationPayload* const output = *Sh2CacheThrough(&output_);
        if (!input || !output)
        {
            *Sh2CacheThrough(&doneFlag_) = 1u;
            return;
        }

        const SimulationPayload* const inputView = Sh2CacheThrough(const_cast<SimulationPayload*>(input));
        SimulationPayload* const outputView = Sh2CacheThrough(output);
        Sh2FrtProfiler::EnsureInitialized();
        const uint16_t startTicks = Sh2FrtProfiler::Now();
        auto state = inputView->frameState;
        Game::IGameplayTick* const gameplayTick = *Sh2CacheThrough(&gameplayTick_);
        Game::ICarPhysics* const carPhysics = *Sh2CacheThrough(&carPhysics_);
        Game::ITrackCollisionQuery* const trackCollision = *Sh2CacheThrough(&trackCollision_);
        if (gameplayTick)
        {
            gameplayTick->Tick(state, trackCollision);
        }
        if (carPhysics)
        {
            carPhysics->Step(state,
                             trackCollision,
                             state.carWorldPosition,
                             state.carYawDeg);
        }
        if (state.resetRequested)
        {
            state.carWorldPosition = state.respawnPosition;
            state.carYawDeg = state.respawnYawDeg;
            state.resetRequested = false;
        }
        outputView->frameState = state;
        *Sh2CacheThrough(&lastTicks_) = Sh2FrtProfiler::Elapsed(startTicks, Sh2FrtProfiler::Now());
        *Sh2CacheThrough(&doneFlag_) = 1u;
    }

    const SimulationPayload* input_ = nullptr;
    SimulationPayload* output_ = nullptr;
    Game::IGameplayTick* gameplayTick_ = nullptr;
    Game::ICarPhysics* carPhysics_ = nullptr;
    Game::ITrackCollisionQuery* trackCollision_ = nullptr;
    volatile uint16_t lastTicks_ = 0;
    volatile uint8_t doneFlag_ = 0;
};

class CarRenderPrepareTask final : public SRL::Types::ITask
{
public:
    void Configure(const int32_t* inputYawDeg, int32_t* outputYawDeg)
    {
        inputYawDeg_ = inputYawDeg;
        outputYawDeg_ = outputYawDeg;
    }

private:
    void Do() override
    {
        if (!inputYawDeg_ || !outputYawDeg_) return;
        int32_t yaw = *inputYawDeg_;
        yaw %= 360;
        if (yaw < 0) yaw += 360;
        *outputYawDeg_ = yaw;
    }

    const int32_t* inputYawDeg_ = nullptr;
    int32_t* outputYawDeg_ = nullptr;
};

} // namespace Game
