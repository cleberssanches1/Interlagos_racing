#pragma once

#include <srl.hpp>

#include "camera_controller.hpp"
#include "camera_rig.hpp"
#include "camera_orbit_controller.hpp"
#include "camera_safety.hpp"

using SRL::Math::Types::Angle;
using SRL::Math::Types::Fxp;
using SRL::Math::Types::Vector3D;

class CameraSystem
{
public:
    // Controls whether PATH-based frame context is consumed by chase camera.
    // Keep false while the path-guided chase branch is disabled in runtime.
    static constexpr bool kPathGuidedChaseEnabled = false;

    // Body pitch → camera pitch sign (Y-down: +bodyPitch = nose down).
    static constexpr int32_t kCamPitchSign = 1;
    static constexpr int32_t kMaxCamPitchDeg = 18;
    // Ignore tiny seam/noise pitch so start throttle + segment joints don't bob.
    static constexpr int32_t kPitchDeadzoneDeg = 3;
    // Asymmetric cam pitch rate (F1 96 plant — leave nose-down faster).
    static constexpr int32_t kMaxCamPitchStepInDeg = 1;   // into ramp
    static constexpr int32_t kMaxCamPitchStepOutDeg = 3;  // recover level
    // Boom offset uses only a fraction of pitch (full pitch lifted the tower).
    static constexpr int32_t kBoomPitchFractionX100 = 45;
    struct PathFrameContext
    {
        bool valid = false;
        Vector3D forwardWorld{0.0, 0.0, 1.0f};
        // 16.16 fixed-point scalar in [0,1].
        int32_t speedNormRaw = 0;
        // 16.16 fixed-point scalar in [0,1].
        int32_t curvatureAbsRaw = 0;
        // 16.16 fixed-point scalar in [0,1].
        int32_t slopeAbsRaw = 0;
        // -1 = left, +1 = right, 0 = neutral/unknown.
        int8_t turnSign = 0;
    };

    enum class ChasePreset : uint8_t
    {
        FirstPerson = 0,
        ChaseNear = 1,
        ChaseFar = 2
    };

    // Response profile for chase smoothing.
    enum class ChaseResponsePreset : uint8_t
    {
        Loose = 0,
        Rigid = 1
    };

    enum class Mode : uint8_t
    {
        Chase = 0,
        Orbit = 1,
        Cinematic = 2
    };

    CameraSystem();

    // Update camera and car yaw controls from the current controller state.
    void UpdateFromPad(SRL::Input::Digital& pad,
                       int32_t& carYawDeg,
                       CameraRig::OrbitState& orbitState,
                       bool allowCarYawInput = true);

    // Build world camera location using car world position plus manual camera offset.
    Vector3D CameraLocation(const Vector3D& carWorldPosition) const;
    // Return orbit view direction based on view yaw and pitch.
    Vector3D ViewDirection() const;
    // Return camera look target used by the scene look-at call.
    Vector3D LookTarget(const Vector3D& carWorldPosition, const Vector3D& modelOffset) const;
    // Runtime mode selection for future cinematic camera tracks.
    void SetMode(Mode mode) { mode_ = mode; }
    Mode GetMode() const { return mode_; }
    // Set explicit cinematic frame.
    void SetCinematicFrame(const Vector3D& location, const Vector3D& target);
    // Configure camera 2 (CHASE) distance behind car center in world units.
    void SetChaseNearFollowDistance(int16_t behindDistance);
    // Keep camera heading synchronized with gameplay yaw when yaw is updated outside input handling.
    void SetCarYawDegrees(int32_t yawDeg) { cachedCarYawDeg_ = NormalizeYawDeg(yawDeg); }
    void SetCarForwardYawOffsetDegrees(int32_t offsetDeg)
    {
        offsetDeg = NormalizeYawDeg(offsetDeg);
        if (offsetDeg > 180) offsetDeg -= 360;
        carForwardYawOffsetDeg_ = static_cast<int16_t>(offsetDeg);
    }
    // Runtime flag to enable/disable camera debug overlay logs.
    void SetDebugLogsEnabled(bool enabled) { debugLogsEnabled_ = enabled; }
    bool DebugLogsEnabled() const { return debugLogsEnabled_; }
    // External PATH guidance for chase camera (optional, fallback-safe).
    void SetPathFrameContext(const PathFrameContext& context) { pathFrameContext_ = context; }
    // Road attitude from car physics (no track probes — boot-safe).
    // gradeTanX100: tanθ * 100, Y-down convention (positive = decline / nose down).
    // bodyPitchDeg: integer degrees of body pitch (positive = nose down after model sign).
    void SetRoadAttitude(int16_t gradeTanX100, int16_t bodyPitchDeg)
    {
        roadGradeTanX100_ = gradeTanX100;
        roadBodyPitchDeg_ = bodyPitchDeg;
    }
    int16_t SmoothedCamPitchDeg() const { return smoothedCamPitchDeg_; }
    // Feed an external terrain/occlusion correction back into the chase
    // integrator. This prevents the next frame from smoothing from a position
    // that was already inside the track mesh.
    void CommitSafetyResolvedLocation(const Vector3D& location) const
    {
        lastResolvedCameraLocation_ = location;
        cameraLocationInitialized_ = true;
    }
    ChasePreset GetChasePreset() const { return chasePreset_; }
    void SetChaseResponsePreset(ChaseResponsePreset preset) { chaseResponsePreset_ = preset; }
    ChaseResponsePreset GetChaseResponsePreset() const { return chaseResponsePreset_; }

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
    struct ChasePresetConfig
    {
        int16_t offsetX = 0;
        int16_t offsetY = 0;
        int16_t offsetZ = 0;
        int16_t lookAhead = 0;
        int16_t lookHeight = 0;
        int16_t viewPitchDeg = 0;
        // Pitch follow: 100 = 1.0 of body pitch.
        int16_t pitchFollowX100 = 100;
        // 16.16 blend toward target pitch per frame.
        int32_t pitchBlendRaw = 29491; // ~0.45
        // Extra base boom lift (Y-down units, more negative after apply).
        int16_t baseBoomLift = 0;
        // Min clearance above car (Y-down units).
        int16_t minBoomClearance = 10;
    };

    // Initialize manual camera offset so initial framing matches expected setup.
    void InitializeManualOffset();
    // Restore camera orientation and manual offset to startup defaults.
    void ResetToDefaultView();
    void ApplyChasePreset(ChasePreset preset, bool logPreset);
    ChasePresetConfig PresetConfig(ChasePreset preset) const;
    static Vector3D ForwardFromYawDeg(int32_t yawDeg);
    static int32_t NormalizeYawDeg(int32_t yawDeg);
    int32_t CameraFollowBlendRaw() const;
    void UpdateHeadingFromCarMotion(const Vector3D& carWorldPosition) const;
    void UpdateSmoothedCamPitch() const;
    // Rotate local (Y,Z) by camera pitch (Y-down, +pitch = nose down).
    static void ApplyLocalPitchYZ(int32_t pitchDeg,
                                  Fxp& ioOffY,
                                  Fxp& ioOffZ);
    Vector3D ResolvePresetOffsetWorld() const;
    Vector3D ResolveLocalOffsetWorld(int32_t offsetXUnits,
                                     int32_t offsetYUnits,
                                     int32_t offsetZUnits,
                                     int32_t pitchDeg) const;
    CameraSafety::Config BoomSafetyConfig(int32_t pitchDeg) const;
    // Retained as a passive helper for A/B rollback; chase runtime now uses
    // the mesh surface guard as its single road-clearance authority.
    Vector3D ApplyGradeBehindClearance(const Vector3D& cameraPos,
                                       const Vector3D& carWorldPosition,
                                       int32_t behindUnitsAbs) const;
    Camera::State state_;
    Camera::Tuning tuning_;
    Vector3D manualOffset_{};
    mutable Vector3D lastResolvedCameraLocation_{};
    mutable bool cameraLocationInitialized_ = false;
    mutable Vector3D lastResolvedLookTarget_{};
    Vector3D cinematicLocation_{};
    Vector3D cinematicTarget_{};
    Mode mode_ = Mode::Chase;
    CameraOrbitController orbitController_{};
    CameraOrbitController::Config orbitConfig_{};
    CameraSafety::Config safetyConfig_{};
    ChasePreset chasePreset_ = ChasePreset::ChaseNear;
    int32_t cachedCarYawDeg_ = 0;
    int16_t carForwardYawOffsetDeg_ = 0;
    mutable Vector3D headingForwardWorld_{0.0, 0.0, 1.0f};
    mutable Vector3D smoothedHeadingForwardWorld_{0.0, 0.0, 1.0f};
    mutable Vector3D movementForwardWorld_{0.0, 0.0, 1.0f};
    mutable Vector3D lastObservedCarWorldPosition_{0.0, 0.0, 0.0};
    mutable bool hasObservedCarWorldPosition_ = false;
    // Per-frame chassis displacement, carried into the chase Y integrator so
    // a continuous grade does not create a permanent camera-height error.
    mutable int32_t carVerticalDeltaRaw_ = 0;
    PathFrameContext pathFrameContext_{};
    // Road grade / body pitch (from car, not PATH probes).
    int16_t roadGradeTanX100_ = 0;
    int16_t roadBodyPitchDeg_ = 0;
    // Smoothed chase pitch (degrees, same sign as body: + = nose down).
    mutable int16_t smoothedCamPitchDeg_ = 0;
    // Positional boom follows the road frame almost fully. Keeping it separate
    // lets the view retain an arcade horizon without changing camera distance.
    mutable int16_t smoothedBoomPitchDeg_ = 0;
    mutable bool camPitchInitialized_ = false;
    // 16.16 fixed-point scalar in [0,1] based on per-frame movement magnitude.
    mutable int32_t movementSpeedNormRaw_ = 0;
    bool zHeld_ = false;
    bool aHeldPrev_ = false;
    bool startHeldPrev_ = false;
    int16_t carYawStepDeg_ = 4;
    int16_t orbitYawStepDeg_ = 4;
    int16_t orbitPitchStepDeg_ = 2;
    int16_t orbitPitchLimitDeg_ = 40;
    int16_t chaseNearOffsetX_ = 0;
    int16_t chaseNearOffsetZ_ = -240;
    uint8_t chaseNearCalibRepeatFrames_ = 0;
    ChaseResponsePreset chaseResponsePreset_ = ChaseResponsePreset::Loose;
    bool debugLogsEnabled_ = false;
};
