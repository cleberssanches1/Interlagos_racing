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

    // Update wheel spin state from external start/stop events.
    void UpdateWheels(bool start, bool stop);

    // Advance one frame of command state smoothing.
    void TickCommandState();

    void SubmitRender(class RenderPipeline& pipeline, bool logStats = false);
    void SetRuntimeFrameState(const GameplayFrameState& frameState);
    struct GameplayInputSnapshot
    {
        bool accelerateHeld = false;
        bool brakeHeld = false;
        bool steerLeftHeld = false;
        bool steerRightHeld = false;
        bool shiftDownHeld = false;
        bool shiftUpHeld = false;
        bool shiftLockHeld = false;
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
        int16_t speedProxy = 0;
        int16_t speedKmh = 0;
        int16_t engineRpm = 0;
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
        bool braking = false;
        bool wallHit = false;
    };
    struct DrivetrainDebugSnapshot
    {
        char gearChar = '1';
        int16_t throttle = 0;
        bool braking = false;
        int16_t speedProxy = 0;
        int16_t speedKmh = 0;
        int16_t engineRpm = 0;
        int16_t steeringCommand = 0;
        int16_t yawRateDeg = 0;
        int16_t yawStepDeg = 0;
        int16_t planarDx = 0;
        int16_t netDz = 0;
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
            bool throttleHeld = false;
            bool brakeHeld = false;
            bool steerHeld = false;
            uint8_t accelHoldFrames = 0;
            uint8_t brakeHoldFrames = 0;
        };

        int16_t throttle = 0;
        int16_t steering = 0;
        int8_t steerDirection = 0; // -1 left, +1 right, 0 neutral
        bool braking = false;
        bool wheelsSpinning = false;
        uint32_t wheelSpinTicks = 0;
        InputLatchState latches{};
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
        bool leftHeldPrev = false;
        bool rightHeldPrev = false;
        bool shiftDownHeldPrev = false;
        bool shiftUpHeldPrev = false;
        uint32_t lastLeftPressFrame = 0;
        uint32_t lastRightPressFrame = 0;
        int8_t brakeSteerDirWhileHeld = 0;
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
