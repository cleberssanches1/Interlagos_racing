#define DOXYGEN 1
#include "camera_system.hpp"
#undef DOXYGEN

#include <algorithm>

namespace
{
using SRL::Math::Types::Fxp;
using SRL::Math::Types::Vector3D;

inline Vector3D NormalizeFlatDirectionRaw(int32_t dxRaw,
                                          int32_t dzRaw,
                                          const Vector3D& fallback)
{
    const int32_t adx = (dxRaw < 0) ? -dxRaw : dxRaw;
    const int32_t adz = (dzRaw < 0) ? -dzRaw : dzRaw;
    const int32_t maxAxis = (adx > adz) ? adx : adz;
    if (maxAxis <= 0) return fallback;

    const int64_t nxRaw = (static_cast<int64_t>(dxRaw) << 16) / maxAxis;
    const int64_t nzRaw = (static_cast<int64_t>(dzRaw) << 16) / maxAxis;
    return Vector3D(Fxp::BuildRaw(static_cast<int32_t>(nxRaw)),
                    Fxp::BuildRaw(0),
                    Fxp::BuildRaw(static_cast<int32_t>(nzRaw)));
}
} // namespace

CameraSystem::CameraSystem()
{
    state_.yawDeg = 180;
    state_.pitchDeg = -21;
    state_.viewYawDeg = 0;
    state_.viewPitchDeg = 0;
    state_.radius = Fxp::BuildRaw(46 << 16);
    // classes_old: strafe(0,-4,-2)
    state_.strafe = Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(-(4 << 16)), Fxp::BuildRaw(-(2 << 16)));
    state_.location = Vector3D(0.0, 0.0, -50.0f);
    state_.yaw = Angle::FromDegrees(Fxp::BuildRaw(180 << 16));
    state_.pitch = Angle::FromDegrees(Fxp::BuildRaw(-21 << 16));
    state_.viewYaw = Angle::FromDegrees(Fxp::BuildRaw(0));
    state_.viewPitch = Angle::FromDegrees(Fxp::BuildRaw(0));
    // Used only by debug view direction path.
    tuning_.targetDistance = Fxp::BuildRaw(120 << 16);
    tuning_.yawStepDeg = 4;
    Camera::RefreshAngles(state_);
    headingForwardWorld_ = ForwardFromYawDeg(cachedCarYawDeg_);
    ApplyChasePreset(chasePreset_, false);
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
    const Vector3D desiredCamera = ResolvePresetOffsetWorld();
    Vector3D initialOrbit = Camera::OrbitPosition(state_.yaw, state_.pitch, state_.radius);
    manualOffset_ = desiredCamera - initialOrbit;
}

void CameraSystem::ResetToDefaultView()
{
    state_.yawDeg = 180;
    state_.pitchDeg = -21;
    state_.viewYawDeg = 0;
    state_.viewPitchDeg = 0;
    state_.radius = Fxp::BuildRaw(46 << 16);
    state_.strafe = Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(-(4 << 16)), Fxp::BuildRaw(-(2 << 16)));
    state_.location = Vector3D(0.0, 0.0, -50.0f);
    state_.yaw = Angle::FromDegrees(Fxp::BuildRaw(180 << 16));
    state_.pitch = Angle::FromDegrees(Fxp::BuildRaw(-21 << 16));
    state_.viewYaw = Angle::FromDegrees(Fxp::BuildRaw(0));
    state_.viewPitch = Angle::FromDegrees(Fxp::BuildRaw(0));
    // Restore camera 2 baseline before recalculating manual offset.
    chaseNearOffsetX_ = 0;
    chaseNearOffsetZ_ = -190;
    // Used only by debug view direction path.
    tuning_.targetDistance = Fxp::BuildRaw(120 << 16);
    tuning_.yawStepDeg = 4;
    Camera::RefreshAngles(state_);
    ApplyChasePreset(chasePreset_, false);
    InitializeManualOffset();
    orbitConfig_.yawStepDeg = tuning_.yawStepDeg;
    if (mode_ != Mode::Cinematic) mode_ = Mode::Chase;
}

void CameraSystem::UpdateFromPad(SRL::Input::Digital& pad,
                                 int32_t& carYawDeg,
                                 CameraRig::OrbitState& orbitState,
                                 bool allowCarYawInput)
{
    // Keep chase rig stable frame to frame.
    // Generic input update can modify strafe/location and destabilize the camera matrix.
    // We handle only explicit controls below (orbit and look).

    const bool lHeld = pad.IsHeld(SRL::Input::Digital::Button::L);
    const bool rHeld = pad.IsHeld(SRL::Input::Digital::Button::R);
    const bool leftHeld = pad.IsHeld(SRL::Input::Digital::Button::Left);
    const bool rightHeld = pad.IsHeld(SRL::Input::Digital::Button::Right);
    const bool upHeld = pad.IsHeld(SRL::Input::Digital::Button::Up);
    const bool downHeld = pad.IsHeld(SRL::Input::Digital::Button::Down);
    const bool startHeld = pad.IsHeld(SRL::Input::Digital::Button::START);
    const bool aHeld = pad.IsHeld(SRL::Input::Digital::Button::A);
    zHeld_ = pad.IsHeld(SRL::Input::Digital::Button::Z);
    const bool orbitControlActive = zHeld_ && (lHeld || rHeld || upHeld || downHeld);
    const bool xHeld = pad.IsHeld(SRL::Input::Digital::Button::X);
    const bool chaseNearCalibActive = xHeld && (chasePreset_ == ChasePreset::ChaseNear);

    if (chaseNearCalibActive)
    {
        if (chaseNearCalibRepeatFrames_ > 0) --chaseNearCalibRepeatFrames_;
        if (chaseNearCalibRepeatFrames_ == 0)
        {
            int16_t nextOffsetX = chaseNearOffsetX_;
            int16_t nextOffsetZ = chaseNearOffsetZ_;
            if (leftHeld) --nextOffsetX;
            if (rightHeld) ++nextOffsetX;
            if (upHeld) ++nextOffsetZ;    // frente
            if (downHeld) --nextOffsetZ;  // tras
            nextOffsetX = std::clamp<int16_t>(nextOffsetX, -120, 120);
            nextOffsetZ = std::clamp<int16_t>(nextOffsetZ, -220, 60);
            if (nextOffsetX != chaseNearOffsetX_ || nextOffsetZ != chaseNearOffsetZ_)
            {
                chaseNearOffsetX_ = nextOffsetX;
                chaseNearOffsetZ_ = nextOffsetZ;
                InitializeManualOffset();
            }
            chaseNearCalibRepeatFrames_ = 2;
        }
    }
    else
    {
        chaseNearCalibRepeatFrames_ = 0;
    }

    if (aHeld && !aHeldPrev_)
    {
        const uint8_t nextPreset =
            static_cast<uint8_t>((static_cast<uint8_t>(chasePreset_) + 1u) % 3u);
        ApplyChasePreset(static_cast<ChasePreset>(nextPreset), true);
    }
    aHeldPrev_ = aHeld;

    // One-shot reset of camera framing.
    if (startHeld && !startHeldPrev_)
    {
        ResetToDefaultView();
    }
    startHeldPrev_ = startHeld;

    // Allow L/R car yaw while accelerating/braking; only block in explicit orbit/edit modes.
    if (allowCarYawInput && !orbitControlActive && !xHeld)
    {
        if (lHeld) carYawDeg -= carYawStepDeg_;
        if (rHeld) carYawDeg += carYawStepDeg_;
        if (carYawDeg < 0) carYawDeg += 360;
        if (carYawDeg >= 360) carYawDeg -= 360;
    }
    cachedCarYawDeg_ = NormalizeYawDeg(carYawDeg);

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
    const Vector3D currentOffset = ResolvePresetOffsetWorld();
    orbitController_.Update(pad, orbitConfig_, currentOffset, !chaseNearCalibActive);
    if (mode_ != Mode::Cinematic)
    {
        mode_ = orbitController_.Active() ? Mode::Orbit : Mode::Chase;
    }

    (void)orbitState;

    if (chasePreset_ == ChasePreset::ChaseNear)
    {
        const auto cfg = PresetConfig(chasePreset_);
        SRL::Debug::Print(1, 24, "CAM2 off x:%d y:%d z:%d    ",
                          static_cast<int>(cfg.offsetX),
                          static_cast<int>(cfg.offsetY),
                          static_cast<int>(cfg.offsetZ));
        SRL::Debug::Print(1, 25, "X+U/D:Z  X+L/R:X         ");
    }
    else
    {
        SRL::Debug::Print(1, 24, "                          ");
        SRL::Debug::Print(1, 25, "                          ");
        SRL::Debug::Print(1, 26, "                          ");
    }

}

Vector3D CameraSystem::CameraLocation(const Vector3D& carWorldPosition) const
{
    if (mode_ == Mode::Cinematic)
    {
        lastResolvedCameraLocation_ = cinematicLocation_;
        return lastResolvedCameraLocation_;
    }

    UpdateHeadingFromCarMotion(carWorldPosition);
    const Vector3D defaultOffset = ResolvePresetOffsetWorld();
    const Vector3D activeOffset = orbitController_.ResolveOffset(defaultOffset);
    lastResolvedCameraLocation_ = carWorldPosition + activeOffset;
    if (chasePreset_ == ChasePreset::ChaseNear)
    {
        SRL::Debug::Print(1, 26, "CAM2 pos x:%d y:%d z:%d    ",
                          static_cast<int>(lastResolvedCameraLocation_.X.As<int32_t>()),
                          static_cast<int>(lastResolvedCameraLocation_.Y.As<int32_t>()),
                          static_cast<int>(lastResolvedCameraLocation_.Z.As<int32_t>()));
    }
    return lastResolvedCameraLocation_;
}

Vector3D CameraSystem::ViewDirection() const
{
    return Camera::OrbitPosition(state_.viewYaw, state_.viewPitch, tuning_.targetDistance);
}

Vector3D CameraSystem::LookTarget(const Vector3D& carWorldPosition, const Vector3D& modelOffset) const
{
    (void)modelOffset;
    if (mode_ == Mode::Cinematic)
    {
        lastResolvedLookTarget_ = cinematicTarget_;
        return lastResolvedLookTarget_;
    }

    const auto cfg = PresetConfig(chasePreset_);
    const Fxp lookAhead = Fxp::BuildRaw(static_cast<int32_t>(cfg.lookAhead) << 16);
    const Fxp zero = Fxp::BuildRaw(0);
    const Fxp one = Fxp::BuildRaw(1 << 16);

    // Keep first-person deterministic and rigidly forward.
    if (chasePreset_ == ChasePreset::FirstPerson)
    {
        const int32_t lookYawDeg = NormalizeYawDeg(cachedCarYawDeg_ + 180);
        const Angle yaw = Angle::FromDegrees(Fxp::BuildRaw(lookYawDeg << 16));
        const Vector3D forward(lookAhead * SRL::Math::Trigonometry::Sin(yaw),
                               zero,
                               lookAhead * SRL::Math::Trigonometry::Cos(yaw));
        lastResolvedLookTarget_ = lastResolvedCameraLocation_ + forward;
        return lastResolvedLookTarget_;
    }

    // Daytona-style predictive look:
    // camera position remains behind chassis; look target blends chassis-forward and movement-forward.
    const Vector3D chassisForward = headingForwardWorld_;
    const Vector3D velocityForward = movementForwardWorld_;

    Fxp dot = (chassisForward.X * velocityForward.X) + (chassisForward.Z * velocityForward.Z);
    if (dot > one) dot = one;
    if (dot < -one) dot = -one;

    Fxp slip = one - dot;
    if (slip < zero) slip = zero;
    if (slip > one) slip = one;

    const Fxp speedNorm = Fxp::BuildRaw(movementSpeedNormRaw_);

    // alpha = base + kSlip*slip + kSpeed*speedNorm
    const Fxp alphaBase = Fxp::BuildRaw(6554);      // 0.10
    const Fxp alphaSlipGain = Fxp::BuildRaw(36045); // 0.55
    const Fxp alphaSpeedGain = Fxp::BuildRaw(13107);// 0.20
    const Fxp alphaMax = Fxp::BuildRaw(52428);      // 0.80
    Fxp alpha = alphaBase + (alphaSlipGain * slip) + (alphaSpeedGain * speedNorm);
    if (alpha < zero) alpha = zero;
    if (alpha > alphaMax) alpha = alphaMax;

    const Fxp invAlpha = one - alpha;
    const Vector3D blendedForward((chassisForward.X * invAlpha) + (velocityForward.X * alpha),
                                  zero,
                                  (chassisForward.Z * invAlpha) + (velocityForward.Z * alpha));
    const Vector3D blendedNorm = NormalizeFlatDirectionRaw(blendedForward.X.RawValue(),
                                                           blendedForward.Z.RawValue(),
                                                           chassisForward);

    // Mild smoothing to avoid jitter at 30fps.
    const Fxp smooth = Fxp::BuildRaw(13107); // 0.20
    const Fxp invSmooth = one - smooth;
    const Vector3D smoothedForward((lookForwardWorld_.X * invSmooth) + (blendedNorm.X * smooth),
                                   zero,
                                   (lookForwardWorld_.Z * invSmooth) + (blendedNorm.Z * smooth));
    lookForwardWorld_ = NormalizeFlatDirectionRaw(smoothedForward.X.RawValue(),
                                                  smoothedForward.Z.RawValue(),
                                                  blendedNorm);

    const Vector3D forward(lookForwardWorld_.X * lookAhead,
                           zero,
                           lookForwardWorld_.Z * lookAhead);
    lastResolvedLookTarget_ = carWorldPosition + forward;
    // Keep camera mostly horizon-locked.
    lastResolvedLookTarget_.Y = lastResolvedCameraLocation_.Y +
                                Fxp::BuildRaw(static_cast<int32_t>(cfg.lookHeight) << 16);
    return lastResolvedLookTarget_;
}

void CameraSystem::SetCinematicFrame(const Vector3D& location, const Vector3D& target)
{
    cinematicLocation_ = location;
    cinematicTarget_ = target;
}

void CameraSystem::SetChaseNearFollowDistance(int16_t behindDistance)
{
    if (behindDistance < 1) behindDistance = 1;
    chaseNearOffsetZ_ = static_cast<int16_t>(-behindDistance);
    if (chasePreset_ == ChasePreset::ChaseNear)
    {
        InitializeManualOffset();
    }
}

CameraSystem::Snapshot CameraSystem::CreateSnapshot() const
{
    return Snapshot{state_, manualOffset_};
}

void CameraSystem::ApplyChasePreset(ChasePreset preset, bool logPreset)
{
    chasePreset_ = preset;
    const auto cfg = PresetConfig(preset);
    state_.viewYawDeg = 0;
    state_.viewPitchDeg = cfg.viewPitchDeg;
    state_.viewYaw = Angle::FromDegrees(Fxp::BuildRaw(state_.viewYawDeg << 16));
    state_.viewPitch = Angle::FromDegrees(Fxp::BuildRaw(state_.viewPitchDeg << 16));
    InitializeManualOffset();

    if (!logPreset) return;
    switch (preset)
    {
    case ChasePreset::FirstPerson:
        SRL::Debug::Print(1, 22, "CAM 1: 1P");
        break;
    case ChasePreset::ChaseNear:
        SRL::Debug::Print(1, 22, "CAM 2: CHASE");
        break;
    case ChasePreset::ChaseFar:
        SRL::Debug::Print(1, 22, "CAM 3: FAR");
        break;
    default:
        break;
    }
}

CameraSystem::ChasePresetConfig CameraSystem::PresetConfig(ChasePreset preset) const
{
    switch (preset)
    {
    case ChasePreset::FirstPerson:
        return ChasePresetConfig{
            0,    // offsetX
            -26,  // offsetY (up)
            14,   // offsetZ (cockpit/hood)
            220,  // lookAhead
            0,    // lookHeight
            0     // viewPitchDeg
        };
    case ChasePreset::ChaseFar:
        return ChasePresetConfig{
            0,    // offsetX
            -26,  // offsetY
            -90,  // offsetZ (far behind)
            140,  // lookAhead
            0,    // lookHeight
            0     // viewPitchDeg
        };
    case ChasePreset::ChaseNear:
    default:
        // Camera 2 distance is calibrated at runtime from car bounds.
        return ChasePresetConfig{
            chaseNearOffsetX_,
            -20,  // offsetY
            chaseNearOffsetZ_,
            120,  // lookAhead
            0,    // lookHeight
            0     // viewPitchDeg
        };
    }
}

Vector3D CameraSystem::ForwardFromYawDeg(int32_t yawDeg)
{
    const int32_t n = NormalizeYawDeg(yawDeg);
    const Angle yaw = Angle::FromDegrees(Fxp::BuildRaw(n << 16));
    // Keep camera forward aligned with gameplay/car forward axis.
    return Vector3D(SRL::Math::Trigonometry::Sin(yaw),
                    Fxp::BuildRaw(0),
                    SRL::Math::Trigonometry::Cos(yaw));
}

void CameraSystem::UpdateHeadingFromCarMotion(const Vector3D& carWorldPosition) const
{
    // Keep camera-offset forward aligned with the same visual-forward convention
    // used by LookTarget (model axis requires 180deg flip vs physics yaw).
    headingForwardWorld_ = ForwardFromYawDeg(NormalizeYawDeg(cachedCarYawDeg_ + 180));

    if (!hasObservedCarWorldPosition_)
    {
        hasObservedCarWorldPosition_ = true;
        lastObservedCarWorldPosition_ = carWorldPosition;
        movementForwardWorld_ = headingForwardWorld_;
        lookForwardWorld_ = headingForwardWorld_;
        movementSpeedNormRaw_ = 0;
        return;
    }

    const int32_t dxRaw = carWorldPosition.X.RawValue() - lastObservedCarWorldPosition_.X.RawValue();
    const int32_t dzRaw = carWorldPosition.Z.RawValue() - lastObservedCarWorldPosition_.Z.RawValue();
    const int32_t adx = (dxRaw < 0) ? -dxRaw : dxRaw;
    const int32_t adz = (dzRaw < 0) ? -dzRaw : dzRaw;
    const int32_t maxAxis = (adx > adz) ? adx : adz;
    constexpr int32_t kMovementEpsilonRaw = (1 << 12);
    if (maxAxis > kMovementEpsilonRaw)
    {
        movementForwardWorld_ = NormalizeFlatDirectionRaw(dxRaw, dzRaw, movementForwardWorld_);
        constexpr int32_t kSpeedForMaxBlendRaw = (14 << 16);
        const int32_t speedNormRaw = static_cast<int32_t>(
            (static_cast<int64_t>(maxAxis) << 16) / kSpeedForMaxBlendRaw);
        movementSpeedNormRaw_ = std::clamp<int32_t>(speedNormRaw, 0, (1 << 16));
    }
    else
    {
        movementSpeedNormRaw_ = 0;
    }
    lastObservedCarWorldPosition_ = carWorldPosition;
}

int32_t CameraSystem::NormalizeYawDeg(int32_t yawDeg)
{
    yawDeg %= 360;
    if (yawDeg < 0) yawDeg += 360;
    return yawDeg;
}

Vector3D CameraSystem::ResolvePresetOffsetWorld() const
{
    const auto cfg = PresetConfig(chasePreset_);
    const Fxp offX = Fxp::BuildRaw(static_cast<int32_t>(cfg.offsetX) << 16);
    const Fxp offY = Fxp::BuildRaw(static_cast<int32_t>(cfg.offsetY) << 16);
    const Fxp offZ = Fxp::BuildRaw(static_cast<int32_t>(cfg.offsetZ) << 16);

    const Vector3D forward = headingForwardWorld_;
    const Vector3D right(forward.Z, Fxp::BuildRaw(0), -forward.X);

    return Vector3D((right.X * offX) + (forward.X * offZ),
                    offY,
                    (right.Z * offX) + (forward.Z * offZ));
}
