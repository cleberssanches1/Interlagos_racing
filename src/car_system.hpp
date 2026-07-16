#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

#include <srl.hpp>

#include "mesh_renderer.hpp"
#include "renderable.hpp"
#include "interfaces.hpp"
#include "modelObject.hpp"
#include "car_wheel_rig.hpp"

using SRL::Math::Types::Angle;
using SRL::Math::Types::Fxp;
using SRL::Math::Types::Vector3D;

class RenderPipeline;

namespace Game
{
class CarSystem : public IRenderInstance
{
public:
    struct Config
    {
        Vector3D modelCenter;
        Vector3D lightDirection;
        std::array<size_t, 5> drawOrder;
        size_t orderCount{0};
        bool wireframeOnly{false};
    };

    explicit CarSystem(ModelObject* carObj, bool smooth, const Config& config);

    bool Valid() const { return renderer_ != nullptr; }
    ModelObject* Model() { return carObj_; }
    const ModelObject* Model() const { return carObj_; }

    // Update wheel spin state from external start/stop events.
    void UpdateWheels(bool start, bool stop);

    // Advance one frame of command state smoothing.
    void TickCommandState();

    void SubmitRender(class RenderPipeline& pipeline, bool logStats = false);
    void SetRuntimeFrameState(const GameplayFrameState& frameState);
    struct GameplayInputSnapshot
    {
        enum : uint8_t
        {
            kAccelerateHeld = 1u << 0,
            kBrakeHeld = 1u << 1,
            kSteerLeftHeld = 1u << 2,
            kSteerRightHeld = 1u << 3,
            kShiftDownHeld = 1u << 4,
            kShiftUpHeld = 1u << 5,
            kShiftLockHeld = 1u << 6
        };

        uint8_t flags = 0u;

        bool AccelerateHeld() const { return (flags & kAccelerateHeld) != 0u; }
        bool BrakeHeld() const { return (flags & kBrakeHeld) != 0u; }
        bool SteerLeftHeld() const { return (flags & kSteerLeftHeld) != 0u; }
        bool SteerRightHeld() const { return (flags & kSteerRightHeld) != 0u; }
        bool ShiftDownHeld() const { return (flags & kShiftDownHeld) != 0u; }
        bool ShiftUpHeld() const { return (flags & kShiftUpHeld) != 0u; }
        bool ShiftLockHeld() const { return (flags & kShiftLockHeld) != 0u; }

        void SetAccelerateHeld(bool enabled) { SetFlag(kAccelerateHeld, enabled); }
        void SetBrakeHeld(bool enabled) { SetFlag(kBrakeHeld, enabled); }
        void SetSteerLeftHeld(bool enabled) { SetFlag(kSteerLeftHeld, enabled); }
        void SetSteerRightHeld(bool enabled) { SetFlag(kSteerRightHeld, enabled); }
        void SetShiftDownHeld(bool enabled) { SetFlag(kShiftDownHeld, enabled); }
        void SetShiftUpHeld(bool enabled) { SetFlag(kShiftUpHeld, enabled); }
        void SetShiftLockHeld(bool enabled) { SetFlag(kShiftLockHeld, enabled); }

    private:
        void SetFlag(uint8_t bit, bool enabled)
        {
            if (enabled) flags |= bit;
            else flags = static_cast<uint8_t>(flags & static_cast<uint8_t>(~bit));
        }
    };
    GameplayInputSnapshot LastGameplayInput() const { return lastGameplayInput_; }
    void ApplyGameplayInput(const GameplayInputSnapshot& input,
                            uint32_t frameCounter,
                            GameplayFrameState& ioFrameState);
    void PrepareGameplayFrameState(const GameplayInputSnapshot* input,
                                   uint32_t frameCounter,
                                   const Vector3D& worldPosition,
                                   int32_t yawDeg,
                                   bool autoLapEnabled,
                                   GameplayFrameState& ioFrameState);
    void ApplySimulationFrameState(const GameplayFrameState& frameState);
    void SyncRenderState(const Vector3D& renderPosition, int32_t gameplayYawDeg);
    struct RuntimeDebugSnapshot
    {
        enum : uint8_t
        {
            kBrakingBit = 1u << 0,
            kWallHitBit = 1u << 1
        };

        int16_t speedProxy = 0;
        int16_t speedKmh = 0;
        int16_t engineRpm = 0;
        int16_t shiftRpmBefore = 0;
        int16_t shiftRpmAfter = 0;
        uint8_t shiftFrames = 0;
        int16_t groundRearY = 0;
        int16_t groundFrontY = 0;
        int16_t groundTargetY = 0;
        int16_t groundFaceIndex = 0;
        int16_t wallPushX = 0;
        int16_t wallPushZ = 0;
        int16_t yawRateDeg = 0;
        int16_t yawStepDeg = 0;
        int16_t planarDx = 0;
        int16_t netDz = 0;
        int8_t gear = 0;
        uint8_t groundMask = 0;
        uint8_t groundSurfaceType = 0;
        uint8_t groundFamilyId = 0;
        uint8_t flags = 0u;

        bool Braking() const { return (flags & kBrakingBit) != 0u; }
        bool WallHit() const { return (flags & kWallHitBit) != 0u; }
        void SetBraking(bool enabled) { SetFlag(kBrakingBit, enabled); }
        void SetWallHit(bool enabled) { SetFlag(kWallHitBit, enabled); }

    private:
        void SetFlag(uint8_t bit, bool enabled)
        {
            if (enabled) flags |= bit;
            else flags &= static_cast<uint8_t>(~bit);
        }
    };
    struct DrivetrainDebugSnapshot
    {
        enum : uint8_t
        {
            kBrakingBit = 1u << 0
        };

        char gearChar = 'N';
        uint8_t flags = 0u;
        int16_t throttle = 0;
        int16_t speedProxy = 0;
        int16_t speedKmh = 0;
        int16_t engineRpm = 0;
        int16_t shiftRpmBefore = 0;
        int16_t shiftRpmAfter = 0;
        uint8_t shiftFrames = 0;
        int16_t steeringCommand = 0;
        int16_t yawRateDeg = 0;
        int16_t yawStepDeg = 0;
        int16_t planarDx = 0;
        int16_t netDz = 0;

        bool Braking() const { return (flags & kBrakingBit) != 0u; }
        void SetBraking(bool enabled)
        {
            if (enabled) flags |= kBrakingBit;
            else flags &= static_cast<uint8_t>(~kBrakingBit);
        }
    };
    const RuntimeDebugSnapshot& RuntimeDebug() const { return runtimeDebug_; }
    DrivetrainDebugSnapshot BuildDrivetrainDebugSnapshot() const;
    void WriteCommandsToFrameState(GameplayFrameState& ioFrameState, bool enabled = true) const;

    void SetWorldPosition(const Vector3D& pos) { worldPosition_ = pos; }

    Vector3D WorldPosition() const { return worldPosition_; }

    Game::ICarCommand* Command() { return &command_; }

    struct CommandSnapshot
    {
        struct InputLatchState
        {
            uint8_t accelHoldFrames = 0;
            uint8_t brakeHoldFrames = 0;
            uint8_t flags = 0u;

            enum : uint8_t
            {
                kThrottleHeld = 1u << 0,
                kBrakeHeld = 1u << 1,
                kSteerHeld = 1u << 2
            };

            bool ThrottleHeld() const { return (flags & kThrottleHeld) != 0u; }
            bool BrakeHeld() const { return (flags & kBrakeHeld) != 0u; }
            bool SteerHeld() const { return (flags & kSteerHeld) != 0u; }
            void SetThrottleHeld(bool enabled) { SetFlag(kThrottleHeld, enabled); }
            void SetBrakeHeld(bool enabled) { SetFlag(kBrakeHeld, enabled); }
            void SetSteerHeld(bool enabled) { SetFlag(kSteerHeld, enabled); }

        private:
            void SetFlag(uint8_t bit, bool enabled)
            {
                if (enabled) flags |= bit;
                else flags = static_cast<uint8_t>(flags & static_cast<uint8_t>(~bit));
            }
        };

        uint32_t wheelSpinTicks = 0;
        int16_t throttle = 0;
        int16_t steering = 0;
        int8_t steerDirection = 0; // -1 left, +1 right, 0 neutral
        InputLatchState latches{};
        uint8_t flags = 0u;

        enum : uint8_t
        {
            kBraking = 1u << 0,
            kWheelsSpinning = 1u << 1
        };

        bool Braking() const { return (flags & kBraking) != 0u; }
        bool WheelsSpinning() const { return (flags & kWheelsSpinning) != 0u; }
        void SetBraking(bool enabled) { SetFlag(kBraking, enabled); }
        void SetWheelsSpinning(bool enabled) { SetFlag(kWheelsSpinning, enabled); }

    private:
        void SetFlag(uint8_t bit, bool enabled)
        {
            if (enabled) flags |= bit;
            else flags = static_cast<uint8_t>(flags & static_cast<uint8_t>(~bit));
        }
    };

    const CommandSnapshot& Commands() const { return commandState_; }
    int16_t SteeringCommand() const { return commandState_.steering; }

    // IRenderInstance
    MeshRenderer* Renderer() override { return renderer_.get(); }
    Vector3D Position() const override { return worldPosition_; }
    Angle Yaw() const override { return Angle::FromDegrees(SRL::Math::Types::Fxp::BuildRaw(CurrentRenderYawDeg() << 16)); }
    const char* Name() const override { return name_; }

    void SetYawDegrees(int32_t yawDeg) { yawDeg_ = NormalizeYawDeg(yawDeg); }
    void SetVisualYawOffsetDegrees(int32_t offsetDeg) { visualYawOffsetDeg_ = NormalizeSignedYawDeg(offsetDeg); }
    int32_t VisualYawOffsetDegrees() const { return visualYawOffsetDeg_; }
    int32_t RenderYawDegrees() const { return CurrentRenderYawDeg(); }
    // Visual chassis attitude (16.16 degrees) for camera/debug terrain follow.
    int32_t BodyPitchDegX16() const { return wheelRig_.BodyPitchDegX16(); }
    int32_t BodyRollDegX16() const { return wheelRig_.BodyRollDegX16(); }

    // Re-bind fixed scene light onto the mesh renderer each frame (SGL may mutate light).
    void SetLightDirection(const Vector3D& dir)
    {
        config_.lightDirection = dir;
        if (renderer_)
        {
            renderer_->SetLightDirection(dir);
        }
    }

private:
    static int32_t NormalizeYawDeg(int32_t yawDeg)
    {
        yawDeg %= 360;
        if (yawDeg < 0) yawDeg += 360;
        return yawDeg;
    }
    static int32_t NormalizeSignedYawDeg(int32_t yawDeg)
    {
        yawDeg = NormalizeYawDeg(yawDeg);
        if (yawDeg > 180) yawDeg -= 360;
        return yawDeg;
    }
    int32_t CurrentRenderYawDeg() const
    {
        return NormalizeYawDeg(yawDeg_ + visualYawOffsetDeg_);
    }

    struct CarCommandAdapter : Game::ICarCommand
    {
        Vector3D* position;
        CommandSnapshot* state;
        explicit CarCommandAdapter(Vector3D* pos, CommandSnapshot* cmdState)
            : position(pos), state(cmdState) {}
        // Apply a discrete acceleration request.
        void Accelerate() override;
        // Apply a discrete brake request.
        void Brake() override;
        // Apply a discrete steering request to the left.
        void SteerLeft() override;
        // Apply a discrete steering request to the right.
        void SteerRight() override;
        Vector3D WorldPosition() const override
        {
            return position ? *position : Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(0));
        }
    };

    static constexpr int16_t kThrottleStep = 12;
    static constexpr int16_t kThrottleStepBoostMax = 18;
    static constexpr int16_t kThrottleMax = 100;
    static constexpr int16_t kSteeringStep = 24;
    static constexpr int16_t kSteeringMax = 100;
    static constexpr int16_t kSteeringDecay = 18;
    static constexpr int16_t kSteeringCrossCenterStep = 40;
    static constexpr int16_t kThrottleDecay = 4;
    static constexpr int16_t kBrakeReleaseDecay = 10;
    // At very low speed, snap steering to commanded side while accelerating.
    // This mirrors reverse behavior and avoids launch side-slip from steer lag.
    static constexpr int16_t kLaunchSteerSnapSpeedKmh = 8;

    struct InputHistoryState
    {
        uint32_t lastLeftPressFrame = 0;
        uint32_t lastRightPressFrame = 0;
        int8_t brakeSteerDirWhileHeld = 0;
        bool leftHeldPrev = false;
        bool rightHeldPrev = false;
        bool shiftDownHeldPrev = false;
        bool shiftUpHeldPrev = false;
    };

    CommandSnapshot commandState_{};
    CarCommandAdapter command_{&worldPosition_, &commandState_};
    std::unique_ptr<MeshRenderer> renderer_;
    Vector3D worldPosition_{Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(0))};
    Config config_;
    ModelObject* carObj_{nullptr};
    bool isSmooth_{false};
    CarWheelRig wheelRig_{};
    CarWheelRig::Input wheelInput_{};
    GameplayInputSnapshot lastGameplayInput_{};
    RuntimeDebugSnapshot runtimeDebug_{};
    static constexpr size_t kCrashSkipMesh = SIZE_MAX;
    int32_t yawDeg_{0};
    int32_t visualYawOffsetDeg_{0};
    InputHistoryState inputHistory_{};
    char name_[32]{};
};
} // namespace Game
