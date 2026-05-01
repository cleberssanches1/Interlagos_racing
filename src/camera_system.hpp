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
    void SetVehicleDynamics(int16_t pitchDeg, int16_t rollDeg, const Vector3D& surfaceNormal);
    // Runtime flag to enable/disable camera debug overlay logs.
    void SetDebugLogsEnabled(bool enabled) { debugLogsEnabled_ = enabled; }
    bool DebugLogsEnabled() const { return debugLogsEnabled_; }
    // External PATH guidance for chase camera (optional, fallback-safe).
    void SetPathFrameContext(const PathFrameContext& context) { pathFrameContext_ = context; }
    ChasePreset GetChasePreset() const { return chasePreset_; }

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
    };

    // Initialize manual camera offset so initial framing matches expected setup.
    void InitializeManualOffset();
    // Restore camera orientation and manual offset to startup defaults.
    void ResetToDefaultView();
    void ApplyChasePreset(ChasePreset preset, bool logPreset);
    ChasePresetConfig PresetConfig(ChasePreset preset) const;
    static Vector3D ForwardFromYawDeg(int32_t yawDeg);
    static int32_t NormalizeYawDeg(int32_t yawDeg);
    void UpdateHeadingFromCarMotion(const Vector3D& carWorldPosition) const;
    Vector3D ResolvePresetOffsetWorld() const;

    Camera::State state_;
    Camera::Tuning tuning_;
    Vector3D manualOffset_{};
    mutable Vector3D lastResolvedCameraLocation_{};
    mutable Vector3D lastResolvedLookTarget_{};
    Vector3D cinematicLocation_{};
    Vector3D cinematicTarget_{};
    Mode mode_ = Mode::Chase;
    CameraOrbitController orbitController_{};
    CameraOrbitController::Config orbitConfig_{};
    CameraSafety::Config safetyConfig_{};
    ChasePreset chasePreset_ = ChasePreset::ChaseNear;
    int32_t cachedCarYawDeg_ = 180;
    int16_t carForwardYawOffsetDeg_ = 180;
    int16_t vehiclePitchDeg_ = 0;
    int16_t vehicleRollDeg_ = 0;
    Vector3D vehicleSurfaceNormal_{0.0, -1.0, 0.0};
    mutable Vector3D headingForwardWorld_{0.0, 0.0, 1.0f};
    mutable Vector3D movementForwardWorld_{0.0, 0.0, 1.0f};
    mutable Vector3D lookForwardWorld_{0.0, 0.0, 1.0f};
    mutable Vector3D resolvedOffsetForwardWorld_{0.0, 0.0, 1.0f};
    mutable Vector3D lastObservedCarWorldPosition_{0.0, 0.0, 0.0};
    mutable bool hasObservedCarWorldPosition_ = false;
    PathFrameContext pathFrameContext_{};
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
    int16_t chaseNearOffsetZ_ = -190;
    uint8_t chaseNearCalibRepeatFrames_ = 0;
    bool debugLogsEnabled_ = false;
};
