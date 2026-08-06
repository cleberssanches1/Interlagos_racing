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
    smoothedHeadingForwardWorld_ = headingForwardWorld_;
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
    camPitchInitialized_ = false;
    smoothedCamPitchDeg_ = 0;
    smoothedBoomPitchDeg_ = 0;
    state_.viewPitch = Angle::FromDegrees(Fxp::BuildRaw(0));
    // Restore camera 2 baseline before recalculating manual offset.
    chaseNearOffsetX_ = 0;
    chaseNearOffsetZ_ = -240;
    // Used only by debug view direction path.
    tuning_.targetDistance = Fxp::BuildRaw(120 << 16);
    tuning_.yawStepDeg = 4;
    Camera::RefreshAngles(state_);
    headingForwardWorld_ = ForwardFromYawDeg(cachedCarYawDeg_);
    smoothedHeadingForwardWorld_ = headingForwardWorld_;
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

    // Camera must not drive gameplay yaw in chase mode.
    // Keep car yaw ownership in gameplay/physics to avoid follow jitter
    // and side-dependent resistance during continuous steering.
    (void)allowCarYawInput;
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
    UpdateSmoothedCamPitch();
    const Vector3D defaultOffset = ResolvePresetOffsetWorld();
    Vector3D targetCameraLocation = carWorldPosition + defaultOffset;
    // Keep boom above car + pitch-scaled clearance (Y-down).
    targetCameraLocation = CameraSafety::ResolveBoomGuard(
        targetCameraLocation, carWorldPosition, BoomSafetyConfig(smoothedBoomPitchDeg_));
    // The mesh-based guard applied by GameLoop is the single authority for
    // clearance behind the car. Avoid stacking grade-derived hard clamps here.

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
            const int32_t planarBlendRaw = CameraFollowBlendRaw();
            // Arcade camera isolation: XZ stays responsive at speed, while Y
            // rejects wheel/face chatter. On an established slope Y converges
            // faster, but never inherits the near-rigid planar blend.
            // A single vertical response avoids another threshold at which
            // the camera used to change speed in the middle of the descent.
            constexpr int32_t verticalBlendRaw = 16384; // 0.25
            constexpr int32_t maxVerticalCarryRaw = 24 << 16;
            const int32_t verticalCarryRaw = std::clamp<int32_t>(
                carVerticalDeltaRaw_, -maxVerticalCarryRaw, maxVerticalCarryRaw);
            const Fxp carriedCameraY = Fxp::BuildRaw(
                lastResolvedCameraLocation_.Y.RawValue() + verticalCarryRaw);
            lastResolvedCameraLocation_ = Vector3D(
                LerpFxpRaw(lastResolvedCameraLocation_.X,
                           targetCameraLocation.X,
                           planarBlendRaw),
                LerpFxpRaw(carriedCameraY,
                           targetCameraLocation.Y,
                           verticalBlendRaw),
                LerpFxpRaw(lastResolvedCameraLocation_.Z,
                           targetCameraLocation.Z,
                           planarBlendRaw));
            lastResolvedCameraLocation_ = CameraSafety::ResolveBoomGuard(
                lastResolvedCameraLocation_, carWorldPosition,
                BoomSafetyConfig(smoothedBoomPitchDeg_));
        }
    }
    if (debugLogsEnabled_ && chasePreset_ == ChasePreset::ChaseNear)
    {
        SRL::Debug::Print(1, 26, "CAM2 y:%d pit:%d car:%d g:%d",
                          static_cast<int>(lastResolvedCameraLocation_.Y.As<int32_t>()),
                          static_cast<int>(smoothedCamPitchDeg_),
                          static_cast<int>(roadBodyPitchDeg_),
                          static_cast<int>(roadGradeTanX100_));
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

    // CameraLocation advances pitch once; reuse that exact value so location
    // and look target remain coherent within the frame.
    const auto cfg = PresetConfig(chasePreset_);
    const Vector3D headingForward = smoothedHeadingForwardWorld_;
    Vector3D chaseForward = headingForward;
    if (chasePreset_ != ChasePreset::FirstPerson)
    {
        // Use movement forward only when aligned with heading.
        constexpr int32_t kMoveDirEnableRaw = (1 << 12); // ~0.0625
        if (movementSpeedNormRaw_ > kMoveDirEnableRaw)
        {
            const int32_t dotRaw = ((headingForward.X * movementForwardWorld_.X) +
                                    (headingForward.Z * movementForwardWorld_.Z)).RawValue();
            constexpr int32_t kMinAlignedDotRaw = (1 << 15); // cos ~60 deg
            if (dotRaw >= kMinAlignedDotRaw)
            {
                chaseForward = movementForwardWorld_;
            }
        }
    }

    // Look distance along car longitudinal (preset + mild speed gain for near).
    int32_t lookAheadUnits = static_cast<int32_t>(cfg.lookAhead);
    int32_t lookHeightUnits = static_cast<int32_t>(cfg.lookHeight);
    if (chasePreset_ == ChasePreset::ChaseNear)
    {
        const Fxp speedNorm = Fxp::BuildRaw(movementSpeedNormRaw_);
        constexpr int32_t kLookAheadBaseUnits = 20;
        constexpr int32_t kLookAheadSpeedGainUnits = 10;
        lookAheadUnits = kLookAheadBaseUnits;
        lookAheadUnits += static_cast<int32_t>(
            (static_cast<int64_t>(kLookAheadSpeedGainUnits) * speedNorm.RawValue()) >> 16);
        lookAheadUnits = std::clamp<int32_t>(lookAheadUnits, 12, 56);
        // Prefer preset lookAhead when larger (calibrated chase distance).
        if (cfg.lookAhead > lookAheadUnits) lookAheadUnits = cfg.lookAhead;
        lookHeightUnits = static_cast<int32_t>(cfg.lookHeight) + 3;
    }
    else if (chasePreset_ == ChasePreset::ChaseFar)
    {
        lookAheadUnits = std::clamp<int32_t>(lookAheadUnits, 60, 160);
    }

    // First-person: look along pitched car forward from camera (hood view).
    if (chasePreset_ == ChasePreset::FirstPerson)
    {
        Fxp lookY = Fxp::BuildRaw(lookHeightUnits << 16);
        Fxp lookZ = Fxp::BuildRaw(lookAheadUnits << 16);
        ApplyLocalPitchYZ(smoothedCamPitchDeg_ * kCamPitchSign, lookY, lookZ);
        // 1P: look along pitched heading from current camera.
        const Vector3D lookOff(
            (headingForward.X * lookZ),
            lookY,
            (headingForward.Z * lookZ));
        lastResolvedLookTarget_ = lastResolvedCameraLocation_ + lookOff;
        return lastResolvedLookTarget_;
    }

    // Arcade chase: look point in body frame (ahead + height), rotated by cam pitch
    // so the view tilts with the car on declines/climbs.
    Fxp lookY = Fxp::BuildRaw(lookHeightUnits << 16);
    Fxp lookZ = Fxp::BuildRaw(lookAheadUnits << 16);
    ApplyLocalPitchYZ(smoothedCamPitchDeg_ * kCamPitchSign, lookY, lookZ);

    // Optional planar blend of chase forward for look XZ (yaw only).
    const Vector3D forward = chaseForward;
    lastResolvedLookTarget_ = Vector3D(
        carWorldPosition.X + (forward.X * lookZ),
        carWorldPosition.Y + lookY,
        carWorldPosition.Z + (forward.Z * lookZ));
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
    // Clamp chase distance to keep camera stable during tight steering loops.
    if (behindDistance > 420) behindDistance = 420;
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
    camPitchInitialized_ = false;
    smoothedCamPitchDeg_ = 0;
    smoothedBoomPitchDeg_ = 0;
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
            0,      // offsetX
            -28,    // offsetY
            40,     // offsetZ (hood / along car forward)
            220,    // lookAhead
            0,      // lookHeight
            0,      // viewPitchDeg
            90,     // pitchFollowX100
            19661,  // pitchBlendRaw ~0.30 (stable)
            8,      // baseBoomLift
            12      // minBoomClearance
        };
    case ChasePreset::ChaseFar:
        return ChasePresetConfig{
            0,      // offsetX
            -76,    // offsetY
            -480,   // offsetZ
            120,    // lookAhead
            -28,    // lookHeight
            0,      // viewPitchDeg
            35,     // pitchFollowX100 - stable arcade view; boom is independent
            13107,  // pitchBlendRaw ~0.20
            24,     // baseBoomLift
            14      // minBoomClearance
        };
    case ChasePreset::ChaseNear:
    default:
        return ChasePresetConfig{
            chaseNearOffsetX_,
            -24,    // offsetY
            chaseNearOffsetZ_,
            120,    // lookAhead
            -30,    // lookHeight
            0,      // viewPitchDeg
            40,     // pitchFollowX100 - stable arcade view; boom is independent
            16384,  // pitchBlendRaw ~0.25
            16,     // baseBoomLift
            12      // minBoomClearance
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
    // Keep chase forward aligned with gameplay yaw.
    // Apply a small angular smoothing to remove steering jitter.
    const int32_t baseHeadingYawDeg = NormalizeYawDeg(cachedCarYawDeg_ + carForwardYawOffsetDeg_);
    headingForwardWorld_ = ForwardFromYawDeg(baseHeadingYawDeg);

    if (!hasObservedCarWorldPosition_)
    {
        hasObservedCarWorldPosition_ = true;
        lastObservedCarWorldPosition_ = carWorldPosition;
        smoothedHeadingForwardWorld_ = headingForwardWorld_;
        movementForwardWorld_ = headingForwardWorld_;
        movementSpeedNormRaw_ = 0;
        carVerticalDeltaRaw_ = 0;
        return;
    }

    constexpr int32_t kHeadingSmoothBlendRaw = 18350; // 0.28
    const Vector3D blendedHeading = LerpVectorRaw(smoothedHeadingForwardWorld_,
                                                  headingForwardWorld_,
                                                  kHeadingSmoothBlendRaw);
    smoothedHeadingForwardWorld_ = NormalizeFlatDirectionRaw(
        blendedHeading.X.RawValue(),
        blendedHeading.Z.RawValue(),
        headingForwardWorld_);

    const int32_t dxRaw = carWorldPosition.X.RawValue() - lastObservedCarWorldPosition_.X.RawValue();
    carVerticalDeltaRaw_ = carWorldPosition.Y.RawValue() -
                           lastObservedCarWorldPosition_.Y.RawValue();
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

    // Exponential follow lag grows with car speed when alpha is low:
    //   lag ≈ deltaCar * (1-alpha)/alpha
    // Old fixed alpha=0.30 made the camera "fall behind" under throttle.
    // PS1/arcade chase (Ridge Racer / GT-like near cam) keeps position almost
    // rigid to the car offset and only softens a little at low speed.
    // chaseResponsePreset_ was previously unused — wire it here.
    const int32_t baseBlendRaw =
        (chaseResponsePreset_ == ChaseResponsePreset::Rigid)
            ? 45875   // ~0.70
            : 39322;  // ~0.60 (Loose still much snappier than 0.30)

    // Speed boost: as movementSpeedNorm rises, blend → near rigid so lag
    // does not scale with velocity on long straights.
    constexpr int32_t kSpeedBlendBoostRaw = 19661; // +0.30 at max speedNorm
    const int32_t boostRaw = static_cast<int32_t>(
        (static_cast<int64_t>(kSpeedBlendBoostRaw) * movementSpeedNormRaw_) >> 16);
    return ClampUnitRaw(baseBlendRaw + boostRaw);
}

void CameraSystem::UpdateSmoothedCamPitch() const
{
    const auto cfg = PresetConfig(chasePreset_);
    // degrees ~= tan(theta) * 57; gradeTanX100 = tan * 100 (attitude channel).
    const int32_t gradePitch = static_cast<int32_t>(
        (static_cast<int32_t>(roadGradeTanX100_) * 57) / 100);
    const int32_t bodyPitch = static_cast<int32_t>(roadBodyPitchDeg_);

    // Chase: use the milder of grade attitude vs body so hold/lag cannot
    // freeze the boom nose-down after the ramp softens (F1 96 plant).
    int32_t targetPitch = 0;
    if (chasePreset_ == ChasePreset::FirstPerson)
    {
        targetPitch = (std::abs(bodyPitch) > std::abs(gradePitch))
            ? bodyPitch
            : gradePitch;
    }
    else
    {
        // Average then pull toward the smaller magnitude (anti-tower).
        const int32_t avg = (gradePitch + bodyPitch) / 2;
        if (std::abs(gradePitch) < std::abs(bodyPitch))
        {
            targetPitch = (avg + gradePitch) / 2;
        }
        else
        {
            targetPitch = (avg + bodyPitch) / 2;
        }
    }

    constexpr int32_t kStableSpeedNormRaw = (1 << 12); // ~0.0625
    if (movementSpeedNormRaw_ < kStableSpeedNormRaw &&
        std::abs(targetPitch) < (kPitchDeadzoneDeg + 2))
    {
        targetPitch = 0;
    }

    // Boom follows a reduced pitch so ApplyLocalPitchYZ does not lift the tower.
    int32_t boomTargetPitch =
        (targetPitch * kBoomPitchFractionX100) / 100;
    if (std::abs(boomTargetPitch) < kPitchDeadzoneDeg)
    {
        boomTargetPitch = 0;
    }
    boomTargetPitch = std::clamp<int32_t>(
        boomTargetPitch, -kMaxCamPitchDeg, kMaxCamPitchDeg);

    targetPitch = (targetPitch * static_cast<int32_t>(cfg.pitchFollowX100)) / 100;
    if (std::abs(targetPitch) < kPitchDeadzoneDeg)
    {
        targetPitch = 0;
    }
    targetPitch = std::clamp<int32_t>(targetPitch, -kMaxCamPitchDeg, kMaxCamPitchDeg);

    if (!camPitchInitialized_)
    {
        smoothedCamPitchDeg_ = 0;
        smoothedBoomPitchDeg_ = 0;
        camPitchInitialized_ = true;
        return;
    }

    auto stepPitch = [](int32_t cur, int32_t target, int32_t alpha) -> int32_t
    {
        const int32_t delta = target - cur;
        int32_t step = static_cast<int32_t>(
            (static_cast<int64_t>(delta) * alpha) >> 16);
        // +pitch = nose-down: enter slow, leave fast.
        const bool enteringNoseDown = (delta > 0);
        const int32_t maxIn = kMaxCamPitchStepInDeg;
        const int32_t maxOut = kMaxCamPitchStepOutDeg;
        if (enteringNoseDown)
        {
            if (step > maxIn) step = maxIn;
            if (step < -maxOut) step = -maxOut;
        }
        else
        {
            // Toward level or nose-up: allow faster recovery from +pitch.
            if (step > maxOut) step = maxOut;
            if (step < -maxIn) step = -maxIn;
            // Explicit: leaving positive pitch toward 0.
            if (cur > 0 && target < cur && step > -maxOut)
            {
                if (step > -1 && delta <= -3) step = -1;
            }
        }
        if (step == 0 && std::abs(delta) >= 3)
        {
            step = (delta > 0) ? 1 : -((maxOut > 1) ? 2 : 1);
        }
        return std::clamp<int32_t>(cur + step, -kMaxCamPitchDeg, kMaxCamPitchDeg);
    };

    const int32_t alpha = ClampUnitRaw(cfg.pitchBlendRaw);
    smoothedCamPitchDeg_ = static_cast<int16_t>(
        stepPitch(smoothedCamPitchDeg_, targetPitch, alpha));
    smoothedBoomPitchDeg_ = static_cast<int16_t>(
        stepPitch(smoothedBoomPitchDeg_, boomTargetPitch, alpha));
}

void CameraSystem::ApplyLocalPitchYZ(int32_t pitchDeg, Fxp& ioOffY, Fxp& ioOffZ)
{
    if (pitchDeg == 0) return;
    const Angle pitch = Angle::FromDegrees(Fxp::BuildRaw(pitchDeg << 16));
    const Fxp s = SRL::Math::Trigonometry::Sin(pitch);
    const Fxp c = SRL::Math::Trigonometry::Cos(pitch);
    // Y-down, +pitch = nose down:
    //   Y' = Y cos + Z sin
    //   Z' = -Y sin + Z cos
    // Rear (Z<0, Y high/neg) rises in altitude when nose-down — boom clears asphalt.
    const Fxp y = ioOffY;
    const Fxp z = ioOffZ;
    ioOffY = (y * c) + (z * s);
    ioOffZ = (z * c) - (y * s);
}

Vector3D CameraSystem::ResolveLocalOffsetWorld(int32_t offsetXUnits,
                                               int32_t offsetYUnits,
                                               int32_t offsetZUnits,
                                               int32_t pitchDeg) const
{
    Fxp offX = Fxp::BuildRaw(offsetXUnits * (1 << 16));
    Fxp offY = Fxp::BuildRaw(offsetYUnits * (1 << 16));
    Fxp offZ = Fxp::BuildRaw(offsetZUnits * (1 << 16));
    ApplyLocalPitchYZ(pitchDeg, offY, offZ);

    const Vector3D forward = smoothedHeadingForwardWorld_;
    const Vector3D right(Fxp::BuildRaw(-forward.Z.RawValue()),
                         Fxp::BuildRaw(0),
                         forward.X);
    return Vector3D((right.X * offX) + (forward.X * offZ),
                    offY,
                    (right.Z * offX) + (forward.Z * offZ));
}

CameraSafety::Config CameraSystem::BoomSafetyConfig(int32_t pitchDeg) const
{
    const auto cfg = PresetConfig(chasePreset_);
    CameraSafety::Config boom = safetyConfig_;
    int32_t clearance = static_cast<int32_t>(cfg.minBoomClearance);
    // Small pitch term only — old +1 unit/degree made the chase "tower" on
    // steep descents (video 23-05). Mesh guard still owns hard anti-asphalt.
    const int32_t absPitch = (pitchDeg < 0) ? -pitchDeg : pitchDeg;
    if (absPitch > kPitchDeadzoneDeg)
    {
        int32_t extra = (absPitch - kPitchDeadzoneDeg) / 4; // 0.25 u/deg
        if (extra > 4) extra = 4;
        clearance += extra;
    }
    if (clearance < 8) clearance = 8;
    if (clearance > 20) clearance = 20;
    boom.minCameraHeightAboveTarget = Fxp::BuildRaw(clearance << 16);
    return boom;
}

Vector3D CameraSystem::ApplyGradeBehindClearance(const Vector3D& cameraPos,
                                                 const Vector3D& carWorldPosition,
                                                 int32_t behindUnitsAbs) const
{
    if (behindUnitsAbs <= 0) return cameraPos;
    // Strict gate: only a clearly established downhill. Use filtered grade
    // directly so clearance engages while camera pitch is still catching up.
    if (roadGradeTanX100_ < 10)
    {
        return cameraPos;
    }
    // tan ≈ gradeTanX100/100; use smoothed pitch primarily (less seam noise).
    const int32_t grade = std::max<int32_t>(
        static_cast<int32_t>(smoothedCamPitchDeg_) * 2,
        static_cast<int32_t>(roadGradeTanX100_));
    // Attenuate: full behind distance over-lifted and bobbed on long Z chase.
    // Nose-down uses more of the boom length so clearance tracks steeper grades.
    const int32_t behindScale =
        (smoothedCamPitchDeg_ >= 12) ? 4 : 2; // /4 vs /3 of behind
    const int32_t effectiveBehind = (behindUnitsAbs * behindScale) / 5;
    const int32_t dyUnits = (grade * effectiveBehind) / 100;
    if (dyUnits < 4) return cameraPos;
    // Estimated ground Y behind car (higher altitude = smaller Y).
    const int32_t groundBehindY =
        (carWorldPosition.Y.RawValue() >> 16) - dyUnits;
    const auto boom = BoomSafetyConfig(smoothedCamPitchDeg_);
    const int32_t clearance = boom.minCameraHeightAboveTarget.RawValue() >> 16;
    const int32_t maxCamY = groundBehindY - clearance;
    Vector3D out = cameraPos;
    if ((out.Y.RawValue() >> 16) > maxCamY)
    {
        out.Y = Fxp::BuildRaw(maxCamY << 16);
    }
    return out;
}

Vector3D CameraSystem::ResolvePresetOffsetWorld() const
{
    const auto cfg = PresetConfig(chasePreset_);
    int32_t offsetXUnits = cfg.offsetX;
    int32_t offsetYUnits = cfg.offsetY;
    int32_t offsetZUnits = cfg.offsetZ;

    offsetXUnits = std::clamp<int32_t>(offsetXUnits, -140, 140);
    // Global chase lift + per-preset base boom (Y-down: more negative = higher).
    if (chasePreset_ == ChasePreset::FirstPerson)
    {
        offsetYUnits -= 8;
    }
    else
    {
        offsetYUnits -= 20;
    }
    if (chasePreset_ == ChasePreset::ChaseNear)
    {
        offsetYUnits -= 12;
    }
    offsetYUnits -= static_cast<int32_t>(cfg.baseBoomLift);

    // Do not stack additional grade/pitch lifts. Boom rotation supplies the
    // mild arcade attitude; the mesh guard owns actual road clearance.

    const int32_t yMin =
        (chasePreset_ == ChasePreset::ChaseFar) ? -200 :
        (chasePreset_ == ChasePreset::FirstPerson) ? -64 : -120;
    const int32_t yMax = 20;
    offsetYUnits = std::clamp<int32_t>(offsetYUnits, yMin, yMax);

    if (chasePreset_ == ChasePreset::FirstPerson)
    {
        offsetZUnits = std::clamp<int32_t>(offsetZUnits, -40, 80);
    }
    else if (chasePreset_ == ChasePreset::ChaseFar)
    {
        offsetZUnits = std::clamp<int32_t>(offsetZUnits, -560, -200);
    }
    else
    {
        offsetZUnits = std::clamp<int32_t>(offsetZUnits, -320, -80);
    }
    if (chasePreset_ != ChasePreset::FirstPerson)
    {
        offsetZUnits = -std::abs(offsetZUnits);
    }

    // Boom pitch already reduced in UpdateSmoothedCamPitch (fraction of road).
    // Full body pitch here lifted the rear boom into a tower on declines.
    const int32_t pitchDeg = static_cast<int32_t>(smoothedBoomPitchDeg_) * kCamPitchSign;
    return ResolveLocalOffsetWorld(offsetXUnits, offsetYUnits, offsetZUnits, pitchDeg);
}
