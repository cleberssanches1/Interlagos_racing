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

inline int32_t ClampUnitRaw(int32_t valueRaw)
{
    if (valueRaw < 0) return 0;
    if (valueRaw > (1 << 16)) return (1 << 16);
    return valueRaw;
}

inline Fxp LerpFxpRaw(const Fxp& from, const Fxp& to, int32_t alphaRaw)
{
    const Fxp alpha = Fxp::BuildRaw(ClampUnitRaw(alphaRaw));
    const Fxp invAlpha = Fxp::BuildRaw((1 << 16) - alpha.RawValue());
    return (from * invAlpha) + (to * alpha);
}

inline Vector3D LerpVectorRaw(const Vector3D& from, const Vector3D& to, int32_t alphaRaw)
{
    return Vector3D(LerpFxpRaw(from.X, to.X, alphaRaw),
                    LerpFxpRaw(from.Y, to.Y, alphaRaw),
                    LerpFxpRaw(from.Z, to.Z, alphaRaw));
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
    cameraLocationInitialized_ = false;
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
    chaseNearOffsetZ_ = -240;
    // Used only by debug view direction path.
    tuning_.targetDistance = Fxp::BuildRaw(120 << 16);
    tuning_.yawStepDeg = 4;
    Camera::RefreshAngles(state_);
    ApplyChasePreset(chasePreset_, false);
    InitializeManualOffset();
    cameraLocationInitialized_ = false;
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
    // Keep chase calibration disabled in normal gameplay to avoid runtime drift.
    const bool chaseNearCalibActive =
        debugLogsEnabled_ && xHeld && (chasePreset_ == ChasePreset::ChaseNear);

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
            // Keep camera 2 always behind the car (negative Z in local-forward space).
            nextOffsetZ = std::clamp<int16_t>(nextOffsetZ, -320, -80);
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
    // Arcade camera lock: keep chase mode and disable orbit-around-car behavior.
    if (mode_ != Mode::Cinematic)
    {
        mode_ = Mode::Chase;
    }

    (void)orbitState;

    if (debugLogsEnabled_ && chasePreset_ == ChasePreset::ChaseNear)
    {
        const auto cfg = PresetConfig(chasePreset_);
        SRL::Debug::Print(1, 24, "CAM2 off x:%d y:%d z:%d    ",
                          static_cast<int>(cfg.offsetX),
                          static_cast<int>(cfg.offsetY),
                          static_cast<int>(cfg.offsetZ));
        SRL::Debug::Print(1, 25, "X+U/D:Z  X+L/R:X         ");
    }
    else if (debugLogsEnabled_)
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
    const Vector3D activeOffset = defaultOffset;
    const Vector3D targetCameraLocation = carWorldPosition + activeOffset;
    const bool allowSmoothing =
        (mode_ != Mode::Orbit) &&
        (chasePreset_ != ChasePreset::FirstPerson);
    if (!allowSmoothing)
    {
        lastResolvedCameraLocation_ = targetCameraLocation;
        cameraLocationInitialized_ = true;
    }
    else
    {
        if (!cameraLocationInitialized_)
        {
            lastResolvedCameraLocation_ = targetCameraLocation;
            cameraLocationInitialized_ = true;
        }
        else
        {
            lastResolvedCameraLocation_ = LerpVectorRaw(lastResolvedCameraLocation_,
                                                        targetCameraLocation,
                                                        CameraFollowBlendRaw());
        }
    }
    if (debugLogsEnabled_ && chasePreset_ == ChasePreset::ChaseNear)
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
    const Fxp lookAheadBase = Fxp::BuildRaw(static_cast<int32_t>(cfg.lookAhead) << 16);
    const Fxp zero = Fxp::BuildRaw(0);
    const int32_t headingYawDeg = NormalizeYawDeg(cachedCarYawDeg_ + carForwardYawOffsetDeg_);
    const Vector3D headingForward = ForwardFromYawDeg(headingYawDeg);
    Vector3D chaseForward = headingForward;
    if (chasePreset_ != ChasePreset::FirstPerson)
    {
        constexpr int32_t kMoveDirEnableRaw = (1 << 12); // ~0.0625
        if (movementSpeedNormRaw_ > kMoveDirEnableRaw)
        {
            chaseForward = movementForwardWorld_;
        }
    }

    // First-person camera keeps a rigid look vector aligned with car yaw.
    if (chasePreset_ == ChasePreset::FirstPerson)
    {
        const Vector3D forward(headingForward.X * lookAheadBase,
                               zero,
                               headingForward.Z * lookAheadBase);
        lastResolvedLookTarget_ = lastResolvedCameraLocation_ + forward;
        return lastResolvedLookTarget_;
    }

    // Arcade chase (rigid): keep target tightly anchored to the car so the
    // vehicle stays framed even on long straights and 180 turns.
    const Fxp speedNorm = Fxp::BuildRaw(movementSpeedNormRaw_);
    constexpr int32_t kLookAheadBaseUnits = 20;
    constexpr int32_t kLookAheadSpeedGainUnits = 10;
    constexpr int32_t kLookAheadMinUnits = 12;
    constexpr int32_t kLookAheadMaxUnits = 56;
    int32_t dynamicLookAheadUnits = kLookAheadBaseUnits;
    dynamicLookAheadUnits += static_cast<int32_t>(
        (static_cast<int64_t>(kLookAheadSpeedGainUnits) * speedNorm.RawValue()) >> 16);
    dynamicLookAheadUnits = std::clamp<int32_t>(
        dynamicLookAheadUnits,
        kLookAheadMinUnits,
        kLookAheadMaxUnits);
    const Fxp dynamicLookAhead = Fxp::BuildRaw(dynamicLookAheadUnits << 16);

    const Vector3D forward(chaseForward.X * dynamicLookAhead,
                           zero,
                           chaseForward.Z * dynamicLookAhead);
    lastResolvedLookTarget_ = carWorldPosition + forward;
    lastResolvedLookTarget_.Y = carWorldPosition.Y +
                                Fxp::BuildRaw(static_cast<int32_t>(cfg.lookHeight + 3) << 16);
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
    cameraLocationInitialized_ = false;

    if (!logPreset || !debugLogsEnabled_) return;
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
            -320, // offsetZ (farther behind)
            180,  // lookAhead
            0,    // lookHeight
            0     // viewPitchDeg
        };
    case ChasePreset::ChaseNear:
    default:
        // Camera 2 distance is calibrated at runtime from car bounds.
        return ChasePresetConfig{
            chaseNearOffsetX_,
            -16,  // offsetY
            chaseNearOffsetZ_,
            120,  // lookAhead
            -30,  // lookHeight (extra lift so CAM2 pitch change is visible)
            0     // viewPitchDeg
        };
    }
}

Vector3D CameraSystem::ForwardFromYawDeg(int32_t yawDeg)
{
    const int32_t n = NormalizeYawDeg(yawDeg);
    const Angle yaw = Angle::FromDegrees(Fxp::BuildRaw(n << 16));
    // Match gameplay convention:
    // yaw 0 = -Z, 90 = +X, 180 = +Z, 270 = -X.
    return Vector3D(SRL::Math::Trigonometry::Sin(yaw),
                    Fxp::BuildRaw(0),
                    Fxp::BuildRaw(-SRL::Math::Trigonometry::Cos(yaw).RawValue()));
}

void CameraSystem::UpdateHeadingFromCarMotion(const Vector3D& carWorldPosition) const
{
    // Keep chase forward aligned with gameplay yaw (physics heading).
    const int32_t baseHeadingYawDeg = NormalizeYawDeg(cachedCarYawDeg_ + carForwardYawOffsetDeg_);
    headingForwardWorld_ = ForwardFromYawDeg(baseHeadingYawDeg);

    if (!hasObservedCarWorldPosition_)
    {
        hasObservedCarWorldPosition_ = true;
        lastObservedCarWorldPosition_ = carWorldPosition;
        movementForwardWorld_ = headingForwardWorld_;
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
        // Keep the last movement heading while stopped.
        // This prevents yaw jitter from orbiting the chase camera around the car.
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

int32_t CameraSystem::CameraFollowBlendRaw() const
{
    if (chasePreset_ == ChasePreset::FirstPerson)
    {
        return (1 << 16);
    }

    // Arcade lock: keep the chase camera rigidly behind the car.
    return (1 << 16);
}

Vector3D CameraSystem::ResolvePresetOffsetWorld() const
{
    const auto cfg = PresetConfig(chasePreset_);
    int32_t offsetXUnits = cfg.offsetX;
    int32_t offsetYUnits = cfg.offsetY;
    int32_t offsetZUnits = cfg.offsetZ;

    const int32_t baseHeadingYawDeg = NormalizeYawDeg(cachedCarYawDeg_ + carForwardYawOffsetDeg_);
    const Vector3D headingForward = ForwardFromYawDeg(baseHeadingYawDeg);
    // Use real movement direction while moving to prevent chase drift when
    // gameplay yaw and displacement temporarily diverge.
    Vector3D forward = headingForward;
    if (chasePreset_ != ChasePreset::FirstPerson)
    {
        constexpr int32_t kMoveDirEnableRaw = (1 << 12); // ~0.0625
        if (movementSpeedNormRaw_ > kMoveDirEnableRaw)
        {
            forward = movementForwardWorld_;
        }
    }
    offsetXUnits = std::clamp<int32_t>(offsetXUnits, -140, 140);
    // Requested tuning:
    // - global Y shift: -20
    // - camera 2 (ChaseNear): extra -20 (total -40)
    offsetYUnits -= 20;
    if (chasePreset_ == ChasePreset::ChaseNear)
    {
        offsetYUnits -= 20;
    }
    offsetYUnits = std::clamp<int32_t>(offsetYUnits, -56, 20);
    // Chase cameras must stay behind the car to avoid forward drift/overshoot.
    if (chasePreset_ == ChasePreset::FirstPerson)
    {
        offsetZUnits = std::clamp<int32_t>(offsetZUnits, -40, 80);
    }
    else
    {
        offsetZUnits = std::clamp<int32_t>(offsetZUnits, -320, -80);
    }
    // Hard rule for arcade chase: camera must stay behind the car.
    if (chasePreset_ != ChasePreset::FirstPerson)
    {
        offsetZUnits = -std::abs(offsetZUnits);
    }

    const Fxp offX = Fxp::BuildRaw(offsetXUnits * (1 << 16));
    const Fxp offY = Fxp::BuildRaw(offsetYUnits * (1 << 16));
    const Fxp offZ = Fxp::BuildRaw(offsetZUnits * (1 << 16));
    const Vector3D right(Fxp::BuildRaw(-forward.Z.RawValue()),
                         Fxp::BuildRaw(0),
                         forward.X);

    return Vector3D((right.X * offX) + (forward.X * offZ),
                    offY,
                    (right.Z * offX) + (forward.Z * offZ));
}
