#define DOXYGEN 1
#include "camera_system.hpp"
#undef DOXYGEN

const Fxp CameraSystem::kLookDownOffset = Fxp::Convert(6.0f);

CameraSystem::CameraSystem()
{
    state_.yawDeg = 180;
    state_.pitchDeg = -10;
    state_.viewYawDeg = 0;
    state_.viewPitchDeg = 0;
    state_.radius = Fxp(55.2f);
    state_.strafe = Vector3D(Fxp::Convert(0), Fxp::Convert(-13.6), Fxp::Convert(-2));
    state_.location = Vector3D(0.0, 0.0, -50.0f);
    state_.yaw = Angle::FromDegrees(Fxp::Convert(180));
    state_.pitch = Angle::FromDegrees(Fxp::Convert(-21));
    state_.viewYaw = Angle::FromDegrees(Fxp::Convert(0));
    state_.viewPitch = Angle::FromDegrees(Fxp::Convert(0));
    Camera::RefreshAngles(state_);
    state_.location = Camera::OrbitPosition(state_.yaw, state_.pitch, state_.radius) + state_.strafe;
}

void CameraSystem::UpdateInput(SRL::Input::Digital& pad)
{
    Camera::UpdateInput(state_, tuning_, pad);
}

void CameraSystem::OrbitAroundCar(int32_t& carYawDeg, CameraRig::OrbitState& orbitState, bool xHeld, bool lHeld, bool rHeld)
{
    CameraRig::HandleOrbitAroundCar(state_, tuning_.yawStepDeg, xHeld, lHeld, rHeld, carYawDeg, orbitState, true);
}

void CameraSystem::ResetStrafe()
{
    state_.strafe = Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0));
}

void CameraSystem::RefreshOrbit()
{
    Camera::RefreshAngles(state_);
    state_.location = Camera::OrbitPosition(state_.yaw, state_.pitch, state_.radius) + state_.strafe;
}

Vector3D CameraSystem::OrbitOffset() const
{
    return state_.location;
}

Vector3D CameraSystem::LookTarget(const Vector3D& focusPosition, bool zHeld) const
{
    Vector3D cameraLocation = focusPosition + state_.location;
    Vector3D lookTarget = focusPosition + Vector3D(Fxp::Convert(0), -kLookDownOffset, Fxp::Convert(0));
    if (zHeld)
    {
        Vector3D viewOffset = Camera::OrbitPosition(state_.viewYaw, state_.viewPitch, tuning_.targetDistance);
        lookTarget = cameraLocation + viewOffset;
    }
    return lookTarget;
}

CameraSystem::Snapshot CameraSystem::CreateSnapshot() const
{
    return Snapshot{state_, state_.location};
}
