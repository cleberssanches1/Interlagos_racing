#pragma once

#include "car_domain_boundaries.hpp"

// Assembler passivo do frame state de gameplay do carro.
// Prepara o formato alvo da futura extração sem alterar runtime nesta etapa.

namespace CarDomain
{

inline void SeedGameplayFrameState(const GameplayAssemblyContext& context,
                                   Game::GameplayFrameState& outFrameState)
{
    outFrameState.frameId = context.frameId;
    outFrameState.carWorldPosition = context.worldPosition;
    outFrameState.carYawDeg = context.yawDeg;
}

inline void ResetCommandOutputs(Game::GameplayFrameState& outFrameState)
{
    outFrameState.throttle = 0;
    outFrameState.steering = 0;
    outFrameState.braking = false;
    outFrameState.shiftUpRequested = false;
    outFrameState.shiftDownRequested = false;
    outFrameState.wheelsSpinning = false;
    outFrameState.brakeHoldFrames = 0;
}

} // namespace CarDomain
