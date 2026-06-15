#pragma once

#include <cstdint>

#include "interfaces.hpp"
#include "sh2_frt_profiler.hpp"
#include "simulation_scheduler_state.hpp"

namespace Game
{

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

    uint16_t LastTicks() const { return lastTicks_; }

private:
    void Do() override
    {
        if (!input_ || !output_) return;
        Sh2FrtProfiler::EnsureInitialized();
        const uint16_t startTicks = Sh2FrtProfiler::Now();
        auto state = input_->frameState;
        if (gameplayTick_)
        {
            gameplayTick_->Tick(state, trackCollision_);
        }
        if (carPhysics_)
        {
            carPhysics_->Step(state,
                              trackCollision_,
                              state.carWorldPosition,
                              state.carYawDeg);
        }
        if (state.resetRequested)
        {
            state.carWorldPosition = state.respawnPosition;
            state.carYawDeg = state.respawnYawDeg;
            state.resetRequested = false;
        }
        output_->frameState = state;
        lastTicks_ = Sh2FrtProfiler::Elapsed(startTicks, Sh2FrtProfiler::Now());
    }

    const SimulationPayload* input_ = nullptr;
    SimulationPayload* output_ = nullptr;
    Game::IGameplayTick* gameplayTick_ = nullptr;
    Game::ICarPhysics* carPhysics_ = nullptr;
    Game::ITrackCollisionQuery* trackCollision_ = nullptr;
    volatile uint16_t lastTicks_ = 0;
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
