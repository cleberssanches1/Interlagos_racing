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

    // Update camera and car yaw controls from the current controller state.
    void UpdateFromPad(SRL::Input::Digital& pad, int32_t& carYawDeg, CameraRig::OrbitState& orbitState);

    // Build world camera location using car world position plus manual camera offset.
    Vector3D CameraLocation(const Vector3D& carWorldPosition) const;
    // Return orbit view direction based on view yaw and pitch.
    Vector3D ViewDirection() const;
    // Return camera look target used by the scene look-at call.
    Vector3D LookTarget(const Vector3D& carWorldPosition, const Vector3D& modelOffset) const;

    const Camera::State& State() const { return state_; }
    bool IsZHeld() const { return zHeld_; }
    int16_t ViewYawDeg() const { return state_.viewYawDeg; }
    int16_t ViewPitchDeg() const { return state_.viewPitchDeg; }

    struct Snapshot
    {
        Camera::State state;
        Vector3D manualOffset;
    };

    Snapshot CreateSnapshot() const;

private:
    // Initialize manual camera offset so initial framing matches expected setup.
    void InitializeManualOffset();

    Camera::State state_;
    Camera::Tuning tuning_;
    Vector3D manualOffset_{};
    bool zHeld_ = false;
    int16_t orbitYawStepDeg_ = 4;
    int16_t orbitPitchStepDeg_ = 2;
    int16_t orbitPitchLimitDeg_ = 40;
};
