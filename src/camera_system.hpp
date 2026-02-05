#pragma once

#include <srl.hpp>

#include "camera_controller.hpp"
#include "camera_rig.hpp"

using SRL::Math::Types::Angle;
using SRL::Math::Types::Fxp;
using SRL::Math::Types::Vector3D;

class CameraSystem
{
public:
    CameraSystem();

    void UpdateInput(SRL::Input::Digital& pad);

    void OrbitAroundCar(int32_t& carYawDeg, CameraRig::OrbitState& orbitState, bool xHeld, bool lHeld, bool rHeld);

    void ResetStrafe();

    void RefreshOrbit();

    Vector3D OrbitOffset() const;

    Vector3D LookTarget(const Vector3D& focusPosition, bool zHeld) const;

    const Camera::State& State() const { return state_; }

    int16_t YawStepDeg() const { return tuning_.yawStepDeg; }

    struct Snapshot
    {
        Camera::State state;
        Vector3D orbitOffset;
    };

    Snapshot CreateSnapshot() const;

    static const Fxp kLookDownOffset;

private:
    Camera::State state_;
    Camera::Tuning tuning_;
};
