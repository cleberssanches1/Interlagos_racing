#define DOXYGEN 1
#include "camera_system.hpp"
#undef DOXYGEN

#include <algorithm>

namespace
{
// Chase camera preset.
// Increase kChaseOffsetZ to move camera farther from the car.
constexpr int32_t kChaseOffsetY = -52;
constexpr int32_t kChaseOffsetZ = 140;
}

CameraSystem::CameraSystem()
{
    state_.yawDeg = 180;
    state_.pitchDeg = -7;
    state_.viewYawDeg = 0;
    state_.viewPitchDeg = 13;
    state_.radius = Fxp::BuildRaw(86 << 16);
    // Keep neutral strafe at startup so chase distance is controlled by desiredCamera.
    state_.strafe = Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(0));
    state_.location = Vector3D(0.0, 0.0, -50.0f);
    state_.yaw = Angle::FromDegrees(Fxp::BuildRaw(180 << 16));
    state_.pitch = Angle::FromDegrees(Fxp::BuildRaw(-7 << 16));
    state_.viewYaw = Angle::FromDegrees(Fxp::BuildRaw(0));
    state_.viewPitch = Angle::FromDegrees(Fxp::BuildRaw(13 << 16));
    // Used only by debug view direction path.
    tuning_.targetDistance = Fxp::BuildRaw(120 << 16);
    tuning_.yawStepDeg = 4;
    Camera::RefreshAngles(state_);
    InitializeManualOffset();
    orbitConfig_.yawStepDeg = tuning_.yawStepDeg;
    mode_ = Mode::Chase;
    cinematicLocation_ = Vector3D(0.0, 0.0, 0.0);
    cinematicTarget_ = Vector3D(0.0, 0.0, 0.0);
    lastResolvedCameraLocation_ = state_.location + manualOffset_;
    lastResolvedLookTarget_ = Vector3D(0.0, 0.0, 0.0);
}

void CameraSystem::InitializeManualOffset()
{
    // Base chase framing with moderate pullback.
    const Vector3D desiredCamera(0.0, Fxp::BuildRaw(kChaseOffsetY << 16), Fxp::BuildRaw(kChaseOffsetZ << 16));
    Vector3D initialOrbit = Camera::OrbitPosition(state_.yaw, state_.pitch, state_.radius);
    manualOffset_ = desiredCamera - initialOrbit;
}

void CameraSystem::ResetToDefaultView()
{
    state_.yawDeg = 180;
    state_.pitchDeg = -7;
    state_.viewYawDeg = 0;
    state_.viewPitchDeg = 13;
    state_.radius = Fxp::BuildRaw(86 << 16);
    state_.strafe = Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(0));
    state_.location = Vector3D(0.0, 0.0, -50.0f);
    state_.yaw = Angle::FromDegrees(Fxp::BuildRaw(180 << 16));
    state_.pitch = Angle::FromDegrees(Fxp::BuildRaw(-7 << 16));
    state_.viewYaw = Angle::FromDegrees(Fxp::BuildRaw(0));
    state_.viewPitch = Angle::FromDegrees(Fxp::BuildRaw(13 << 16));
    // Used only by debug view direction path.
    tuning_.targetDistance = Fxp::BuildRaw(120 << 16);
    tuning_.yawStepDeg = 4;
    Camera::RefreshAngles(state_);
    InitializeManualOffset();
    orbitConfig_.yawStepDeg = tuning_.yawStepDeg;
    if (mode_ != Mode::Cinematic) mode_ = Mode::Chase;
}

void CameraSystem::UpdateFromPad(SRL::Input::Digital& pad, int32_t& carYawDeg, CameraRig::OrbitState& orbitState)
{
    // Keep chase rig stable frame to frame.
    // Generic input update can modify strafe/location and destabilize the camera matrix.
    // We handle only explicit controls below (orbit and look).

    const bool lHeld = pad.IsHeld(SRL::Input::Digital::Button::L);
    const bool rHeld = pad.IsHeld(SRL::Input::Digital::Button::R);
    const bool upHeld = pad.IsHeld(SRL::Input::Digital::Button::Up);
    const bool downHeld = pad.IsHeld(SRL::Input::Digital::Button::Down);
    const bool startHeld = pad.IsHeld(SRL::Input::Digital::Button::START);
    zHeld_ = pad.IsHeld(SRL::Input::Digital::Button::Z);
    const bool orbitControlActive = zHeld_ && (lHeld || rHeld || upHeld || downHeld);
    const bool xHeld = pad.IsHeld(SRL::Input::Digital::Button::X);

    // One-shot reset of camera framing.
    if (startHeld && !startHeldPrev_)
    {
        ResetToDefaultView();
    }
    startHeldPrev_ = startHeld;

    // Allow L/R car yaw while accelerating/braking; only block in explicit orbit/edit modes.
    if (!orbitControlActive && !xHeld)
    {
        if (lHeld) carYawDeg -= carYawStepDeg_;
        if (rHeld) carYawDeg += carYawStepDeg_;
        if (carYawDeg < 0) carYawDeg += 360;
        if (carYawDeg >= 360) carYawDeg -= 360;
    }

    if (zHeld_)
    {
        int16_t newPitch = state_.viewPitchDeg;
        if (upHeld) newPitch -= orbitPitchStepDeg_;
        if (downHeld) newPitch += orbitPitchStepDeg_;
        newPitch = std::clamp<int16_t>(newPitch,
                                       static_cast<int16_t>(-orbitPitchLimitDeg_),
                                       static_cast<int16_t>(orbitPitchLimitDeg_));
        state_.viewPitchDeg = newPitch;
        if (lHeld) state_.viewYawDeg -= orbitYawStepDeg_;
        if (rHeld) state_.viewYawDeg += orbitYawStepDeg_;
        state_.viewYawDeg = static_cast<int16_t>((state_.viewYawDeg + 360) % 360);
        state_.viewPitch = Angle::FromDegrees(Fxp::BuildRaw(state_.viewPitchDeg << 16));
        state_.viewYaw = Angle::FromDegrees(Fxp::BuildRaw(state_.viewYawDeg << 16));
    }

    orbitConfig_.yawStepDeg = tuning_.yawStepDeg;
    const Vector3D currentOffset = state_.location + manualOffset_;
    orbitController_.Update(pad, orbitConfig_, currentOffset);
    if (mode_ != Mode::Cinematic)
    {
        mode_ = orbitController_.Active() ? Mode::Orbit : Mode::Chase;
    }

    (void)orbitState;

}

Vector3D CameraSystem::CameraLocation(const Vector3D& carWorldPosition) const
{
    if (mode_ == Mode::Cinematic)
    {
        lastResolvedCameraLocation_ = cinematicLocation_;
        return lastResolvedCameraLocation_;
    }

    const Vector3D defaultOffset = state_.location + manualOffset_;
    const Vector3D activeOffset = orbitController_.ResolveOffset(defaultOffset);
    lastResolvedCameraLocation_ = carWorldPosition + activeOffset;
    return lastResolvedCameraLocation_;
}

Vector3D CameraSystem::ViewDirection() const
{
    return Camera::OrbitPosition(state_.viewYaw, state_.viewPitch, tuning_.targetDistance);
}

Vector3D CameraSystem::LookTarget(const Vector3D& carWorldPosition, const Vector3D& modelOffset) const
{
    (void)modelOffset;
    lastResolvedLookTarget_ = (mode_ == Mode::Cinematic) ? cinematicTarget_ : carWorldPosition;
    return lastResolvedLookTarget_;
}

void CameraSystem::SetCinematicFrame(const Vector3D& location, const Vector3D& target)
{
    cinematicLocation_ = location;
    cinematicTarget_ = target;
}

CameraSystem::Snapshot CameraSystem::CreateSnapshot() const
{
    return Snapshot{state_, manualOffset_};
}
