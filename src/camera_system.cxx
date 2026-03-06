#define DOXYGEN 1
#include "camera_system.hpp"
#undef DOXYGEN

#include <algorithm>

CameraSystem::CameraSystem()
{
    state_.yawDeg = 180;
    state_.pitchDeg = -10;
    state_.viewYawDeg = 0;
    state_.viewPitchDeg = 13;
    state_.radius = Fxp::BuildRaw(0x0043BD70); // ~67.74
    // Start already at the same max distance reached by Y + Up (kStrafeLimit on Z).
    state_.strafe = Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(20 << 16));
    state_.location = Vector3D(0.0, 0.0, -50.0f);
    state_.yaw = Angle::FromDegrees(Fxp::BuildRaw(180 << 16));
    state_.pitch = Angle::FromDegrees(Fxp::BuildRaw(-10 << 16));
    state_.viewYaw = Angle::FromDegrees(Fxp::BuildRaw(0));
    state_.viewPitch = Angle::FromDegrees(Fxp::BuildRaw(13 << 16));
    tuning_.targetDistance = Fxp::BuildRaw(1174 << 16);
    tuning_.yawStepDeg = 4;
    Camera::RefreshAngles(state_);
    InitializeManualOffset();
}

void CameraSystem::InitializeManualOffset()
{
    // Chase camera framing tuned for ~30deg vertical look angle.
    const Vector3D desiredCamera(0.0, Fxp::BuildRaw(-44 << 16), Fxp::BuildRaw(70 << 16));
    Vector3D initialOrbit = Camera::OrbitPosition(state_.yaw, state_.pitch, state_.radius);
    manualOffset_ = desiredCamera - initialOrbit;
}

void CameraSystem::ResetToDefaultView()
{
    state_.yawDeg = 180;
    state_.pitchDeg = -10;
    state_.viewYawDeg = 0;
    state_.viewPitchDeg = 13;
    state_.radius = Fxp::BuildRaw(0x0043BD70); // ~67.74
    state_.strafe = Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(20 << 16));
    state_.location = Vector3D(0.0, 0.0, -50.0f);
    state_.yaw = Angle::FromDegrees(Fxp::BuildRaw(180 << 16));
    state_.pitch = Angle::FromDegrees(Fxp::BuildRaw(-10 << 16));
    state_.viewYaw = Angle::FromDegrees(Fxp::BuildRaw(0));
    state_.viewPitch = Angle::FromDegrees(Fxp::BuildRaw(13 << 16));
    tuning_.targetDistance = Fxp::BuildRaw(1174 << 16);
    tuning_.yawStepDeg = 4;
    Camera::RefreshAngles(state_);
    InitializeManualOffset();
}

void CameraSystem::UpdateFromPad(SRL::Input::Digital& pad, int32_t& carYawDeg, CameraRig::OrbitState& orbitState)
{
    Camera::UpdateInput(state_, tuning_, pad);

    const bool xHeld = pad.IsHeld(SRL::Input::Digital::Button::X);
    const bool lHeld = pad.IsHeld(SRL::Input::Digital::Button::L);
    const bool rHeld = pad.IsHeld(SRL::Input::Digital::Button::R);
    const bool upHeld = pad.IsHeld(SRL::Input::Digital::Button::Up);
    const bool downHeld = pad.IsHeld(SRL::Input::Digital::Button::Down);
    const bool leftArrowHeld = pad.IsHeld(SRL::Input::Digital::Button::Left);
    const bool rightArrowHeld = pad.IsHeld(SRL::Input::Digital::Button::Right);
    const bool startHeld = pad.IsHeld(SRL::Input::Digital::Button::START);
    zHeld_ = pad.IsHeld(SRL::Input::Digital::Button::Z);
    const bool orbitControlActive = zHeld_ && (lHeld || rHeld || upHeld || downHeld);

    // One-shot reset of camera framing.
    if (startHeld && !startHeldPrev_)
    {
        ResetToDefaultView();
    }
    startHeldPrev_ = startHeld;

    // Allow L/R car yaw while accelerating/braking; only block in explicit orbit/edit modes.
    if (!orbitControlActive && !xHeld)
    {
        if (lHeld) carYawDeg -= tuning_.yawStepDeg;
        if (rHeld) carYawDeg += tuning_.yawStepDeg;
        if (carYawDeg < 0) carYawDeg += 360;
        if (carYawDeg >= 360) carYawDeg -= 360;
    }

    if (!xHeld)
    {
        CameraRig::HandleOrbitAroundCar(state_, tuning_.yawStepDeg, xHeld, lHeld, rHeld, carYawDeg, orbitState, true);
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

    if (xHeld)
    {
        const Fxp cameraMoveStep = Fxp::BuildRaw(4 << 16);
        if (upHeld) manualOffset_.Y -= cameraMoveStep;
        if (downHeld) manualOffset_.Y += cameraMoveStep;
        if (leftArrowHeld) manualOffset_.X -= cameraMoveStep;
        if (rightArrowHeld) manualOffset_.X += cameraMoveStep;
    }

}

Vector3D CameraSystem::CameraLocation(const Vector3D& carWorldPosition) const
{
    return state_.location + carWorldPosition + manualOffset_;
}

Vector3D CameraSystem::ViewDirection() const
{
    return Camera::OrbitPosition(state_.viewYaw, state_.viewPitch, tuning_.targetDistance);
}

Vector3D CameraSystem::LookTarget(const Vector3D& carWorldPosition, const Vector3D& modelOffset) const
{
    (void)modelOffset;
    // Hard lock: gameplay rule is that car stays as camera center target.
    return carWorldPosition;
}

CameraSystem::Snapshot CameraSystem::CreateSnapshot() const
{
    return Snapshot{state_, manualOffset_};
}
