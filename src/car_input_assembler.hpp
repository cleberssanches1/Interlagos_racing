#pragma once

#include "car_domain_boundaries.hpp"

// Assembler passivo de input do carro.
// Fica fora do runtime crítico até ser explicitamente integrado.

namespace CarDomain
{

struct InputAssemblyInputs
{
    bool accelerateHeld = false;
    bool brakeHeld = false;
    bool steerLeftHeld = false;
    bool steerRightHeld = false;
    bool shiftDownHeld = false;
    bool shiftUpHeld = false;
    bool shiftLockHeld = false;
};

inline Game::CarSystem::GameplayInputSnapshot BuildGameplayInputSnapshot(
    const InputAssemblyInputs& inputs)
{
    Game::CarSystem::GameplayInputSnapshot snapshot{};
    snapshot.SetAccelerateHeld(inputs.accelerateHeld);
    snapshot.SetBrakeHeld(inputs.brakeHeld);
    snapshot.SetSteerLeftHeld(inputs.steerLeftHeld);
    snapshot.SetSteerRightHeld(inputs.steerRightHeld);
    snapshot.SetShiftDownHeld(inputs.shiftDownHeld);
    snapshot.SetShiftUpHeld(inputs.shiftUpHeld);
    snapshot.SetShiftLockHeld(inputs.shiftLockHeld);
    return snapshot;
}

} // namespace CarDomain
