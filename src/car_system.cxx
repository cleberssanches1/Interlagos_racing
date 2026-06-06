#include "car_system.hpp"
#include "exception_stubs.hpp"
#include "render_pipeline.hpp"

#include <algorithm>
#include <cstdlib>

namespace Game
{
void CarSystem::CarCommandAdapter::Accelerate()
{
    if (!state) return;
    state->latches.throttleHeld = true;
    state->braking = false;
    if (state->latches.accelHoldFrames < 255u) ++state->latches.accelHoldFrames;
    // Digital button pressure simulation:
    // longer hold increases acceleration step progressively.
    const int16_t boost = static_cast<int16_t>(
        std::min<int32_t>(CarSystem::kThrottleStepBoostMax,
                          static_cast<int32_t>(state->latches.accelHoldFrames / 6u)));
    const int16_t step = static_cast<int16_t>(CarSystem::kThrottleStep + boost);
    int16_t next = static_cast<int16_t>(state->throttle + step);
    state->throttle = std::min<int16_t>(next, CarSystem::kThrottleMax);
}

void CarSystem::CarCommandAdapter::Brake()
{
    if (!state) return;
    state->latches.brakeHeld = true;
    state->braking = true;
    if (state->latches.brakeHoldFrames < 255u) ++state->latches.brakeHoldFrames;
    state->latches.accelHoldFrames = 0u;
    int16_t next = static_cast<int16_t>(state->throttle - (CarSystem::kThrottleStep * 2));
    state->throttle = std::max<int16_t>(next, 0);
}

void CarSystem::CarCommandAdapter::SteerLeft()
{
    if (!state) return;
    state->latches.steerHeld = true;
    state->steerDirection = -1;
}

void CarSystem::CarCommandAdapter::SteerRight()
{
    if (!state) return;
    state->latches.steerHeld = true;
    state->steerDirection = 1;
}

CarSystem::CarSystem(ModelObject* carObj, bool smooth, const Config& config)
    : config_(config)
    , carObj_(carObj)
    , isSmooth_(smooth)
{
    std::snprintf(name_, sizeof(name_), "Car");
    if (!carObj) return;

    MeshRenderer::Config rendererConfig;
    rendererConfig.modelCenter = config_.modelCenter;
    rendererConfig.lightDirection = config_.lightDirection;
    // Match legacy car orientation and keep the chassis upright.
    rendererConfig.rotateModelX180 = true;
    rendererConfig.rotateModelZ180 = true;
    const size_t meshCount = carObj->GetMeshCount();
    size_t orderCount = std::min(config_.orderCount, rendererConfig.drawOrder.size());
    size_t validOrderCount = 0u;
    for (size_t i = 0; i < orderCount; ++i)
    {
        const size_t meshId = config_.drawOrder[i];
        if (meshId >= meshCount) continue;
        rendererConfig.drawOrder[validOrderCount++] = meshId;
    }
    // If no explicit draw order was configured, MeshRenderer will draw all meshes.
    rendererConfig.drawOrderCount = validOrderCount;
    rendererConfig.wireframeOnly = config_.wireframeOnly;

    renderer_ = std::make_unique<MeshRenderer>(*carObj, smooth, rendererConfig);
    if (!renderer_) return;
    renderer_->SetSkipMesh(kCrashSkipMesh);
    // Keep car proportions at original model scale.
    renderer_->SetScale(SRL::Math::Types::Fxp::BuildRaw(1 << 16));
    const auto& meshCenters = renderer_->MeshCenters();
    if (wheelRig_.Initialize(*carObj_, isSmooth_, meshCenters.data(), meshCenters.size()))
    {
        const auto ids = wheelRig_.WheelMeshIds();
        SRL::Debug::Print(1, 20, "WHEEL fl:%d fr:%d rl:%d rr:%d",
                          static_cast<int>(ids[0]),
                          static_cast<int>(ids[1]),
                          static_cast<int>(ids[2]),
                          static_cast<int>(ids[3]));
        const auto wheelCoord = [&](size_t id, int32_t& outX, int32_t& outZ)
        {
            outX = 0;
            outZ = 0;
            if (id >= meshCenters.size()) return;
            outX = static_cast<int32_t>(meshCenters[id].X.RawValue() >> 16);
            outZ = static_cast<int32_t>(meshCenters[id].Z.RawValue() >> 16);
        };
        int32_t flx = 0, flz = 0, frx = 0, frz = 0, rlx = 0, rlz = 0, rrx = 0, rrz = 0;
        wheelCoord(ids[0], flx, flz);
        wheelCoord(ids[1], frx, frz);
        wheelCoord(ids[2], rlx, rlz);
        wheelCoord(ids[3], rrx, rrz);
        SRL::Debug::Print(1, 21, "WHL xy fl:%d/%d fr:%d/%d", flx, flz, frx, frz);
        SRL::Debug::Print(1, 22, "WHL xy rl:%d/%d rr:%d/%d", rlx, rlz, rrx, rrz);
    }
    else
    {
        SRL::Debug::Print(1, 20, "WHEEL detect fail");
    }
}

void CarSystem::UpdateWheels(bool start, bool stop)
{
    if (start)
    {
        commandState_.wheelsSpinning = true;
    }
    if (stop)
    {
        commandState_.wheelsSpinning = false;
    }
    if (commandState_.wheelsSpinning)
    {
        ++commandState_.wheelSpinTicks;
    }
}

void CarSystem::TickCommandState()
{
    // Steering model:
    // - while held: move toward full lock in requested direction
    // - released: return smoothly to center
    if (commandState_.latches.steerHeld && commandState_.steerDirection != 0)
    {
        const int16_t target =
            (commandState_.steerDirection > 0)
                ? CarSystem::kSteeringMax
                : static_cast<int16_t>(-CarSystem::kSteeringMax);
        // Snap also when steering is crossing center (opposite sign) with throttle —
        // the cross-center ramp would deliver wrong-sign steering to physics for 1-2 frames,
        // causing a visible arc in the wrong direction before the intended turn.
        const bool steeringCrossing =
            (commandState_.steerDirection < 0 && commandState_.steering > 0) ||
            (commandState_.steerDirection > 0 && commandState_.steering < 0);
        const bool launchSteerSnap =
            !commandState_.braking &&
            commandState_.latches.throttleHeld &&
            ((wheelInput_.speedKmh <= CarSystem::kLaunchSteerSnapSpeedKmh) ||
             (std::abs(commandState_.steering) <= CarSystem::kSteeringStep) ||
             steeringCrossing);
        // In brake/reverse mode steering follows the current arrow directly.
        // No cross-center smoothing or one-shot limits.
        if (commandState_.braking || launchSteerSnap)
        {
            commandState_.steering = target;
        }
        else
        {
        // If the player changed side (left->right or right->left), recenter fast first.
        if ((commandState_.steerDirection > 0 && commandState_.steering < 0) ||
            (commandState_.steerDirection < 0 && commandState_.steering > 0))
        {
            // Forward mode keeps smoother cross-center behavior.
            if (commandState_.steering > 0)
            {
                commandState_.steering = static_cast<int16_t>(
                    std::max<int16_t>(0, static_cast<int16_t>(commandState_.steering - CarSystem::kSteeringCrossCenterStep)));
            }
            else
            {
                commandState_.steering = static_cast<int16_t>(
                    std::min<int16_t>(0, static_cast<int16_t>(commandState_.steering + CarSystem::kSteeringCrossCenterStep)));
            }
        }
        if (commandState_.steering < target)
        {
            const int16_t step = CarSystem::kSteeringStep;
            commandState_.steering = static_cast<int16_t>(
                std::min<int16_t>(target, static_cast<int16_t>(commandState_.steering + step)));
        }
        else if (commandState_.steering > target)
        {
            const int16_t step = CarSystem::kSteeringStep;
            commandState_.steering = static_cast<int16_t>(
                std::max<int16_t>(target, static_cast<int16_t>(commandState_.steering - step)));
        }
        }
    }
    else
    {
        if (commandState_.steering > 0)
        {
            commandState_.steering = static_cast<int16_t>(
                std::max<int16_t>(0, commandState_.steering - CarSystem::kSteeringDecay));
        }
        else if (commandState_.steering < 0)
        {
            commandState_.steering = static_cast<int16_t>(
                std::min<int16_t>(0, commandState_.steering + CarSystem::kSteeringDecay));
        }
        if (std::abs(commandState_.steering) <= CarSystem::kSteeringDecay)
        {
            commandState_.steering = 0;
        }
    }

    // Idle throttle decay only when accelerator is not held this frame.
    if (!commandState_.latches.throttleHeld &&
        !commandState_.braking &&
        commandState_.throttle > 0)
    {
        commandState_.throttle = static_cast<int16_t>(std::max<int16_t>(0, commandState_.throttle - CarSystem::kThrottleDecay));
    }
    if (!commandState_.latches.throttleHeld)
    {
        commandState_.latches.accelHoldFrames = 0u;
    }

    // Brake decays when button is released, allowing natural transition to reverse.
    if (commandState_.braking)
    {
        if (!commandState_.latches.brakeHeld)
        {
            commandState_.throttle = static_cast<int16_t>(
                std::max<int16_t>(0, commandState_.throttle - CarSystem::kBrakeReleaseDecay));
            if (commandState_.throttle == 0)
            {
                commandState_.braking = false;
            }
            commandState_.latches.brakeHoldFrames = 0u;
        }
    }
    else
    {
        commandState_.latches.brakeHoldFrames = 0u;
    }

    // Reset per-frame latches.
    commandState_.latches.throttleHeld = false;
    commandState_.latches.brakeHeld = false;
    commandState_.latches.steerHeld = false;
    commandState_.steerDirection = 0;
}

void CarSystem::SetRuntimeFrameState(const GameplayFrameState& frameState)
{
    wheelInput_.speedKmh = frameState.speedProxy;
    wheelInput_.steering = frameState.steering;
    wheelInput_.yawStepDeg = frameState.debugYawStepDeg;
    wheelInput_.groundRearY = frameState.debugGroundYRear;
    wheelInput_.groundFrontY = frameState.debugGroundYFront;
    wheelInput_.groundRearYRaw = frameState.debugGroundYRearRaw;
    wheelInput_.groundFrontYRaw = frameState.debugGroundYFrontRaw;
    wheelInput_.groundMask = frameState.debugGroundMask;
}

void CarSystem::ApplyGameplayInput(const GameplayInputSnapshot& input,
                                   uint32_t frameCounter,
                                   GameplayFrameState& ioFrameState)
{
    const bool leftJustPressed = input.steerLeftHeld && !inputHistory_.leftHeldPrev;
    const bool rightJustPressed = input.steerRightHeld && !inputHistory_.rightHeldPrev;
    const bool lJustPressed = input.shiftDownHeld && !inputHistory_.shiftDownHeldPrev;
    const bool rJustPressed = input.shiftUpHeld && !inputHistory_.shiftUpHeldPrev;
    if (leftJustPressed) inputHistory_.lastLeftPressFrame = frameCounter;
    if (rightJustPressed) inputHistory_.lastRightPressFrame = frameCounter;

    if (input.accelerateHeld) command_.Accelerate();

    int8_t desiredSteerDir = 0;
    if (input.steerLeftHeld && !input.steerRightHeld)
    {
        desiredSteerDir = -1;
    }
    else if (input.steerRightHeld && !input.steerLeftHeld)
    {
        desiredSteerDir = 1;
    }
    else if (input.steerLeftHeld && input.steerRightHeld)
    {
        const int16_t currentSteer = commandState_.steering;
        if (currentSteer < 0) desiredSteerDir = -1;
        else if (currentSteer > 0) desiredSteerDir = 1;
        else if (inputHistory_.lastLeftPressFrame > inputHistory_.lastRightPressFrame) desiredSteerDir = -1;
        else if (inputHistory_.lastRightPressFrame > inputHistory_.lastLeftPressFrame) desiredSteerDir = 1;
    }

    if (desiredSteerDir < 0) command_.SteerLeft();
    else if (desiredSteerDir > 0) command_.SteerRight();

    bool suppressBrakeThisFrame = false;
    if (input.brakeHeld)
    {
        if (desiredSteerDir != 0 &&
            inputHistory_.brakeSteerDirWhileHeld != 0 &&
            desiredSteerDir != inputHistory_.brakeSteerDirWhileHeld)
        {
            suppressBrakeThisFrame = true;
        }
        if (desiredSteerDir != 0)
        {
            inputHistory_.brakeSteerDirWhileHeld = desiredSteerDir;
        }
    }
    else
    {
        inputHistory_.brakeSteerDirWhileHeld = 0;
    }

    if (!suppressBrakeThisFrame && input.brakeHeld)
    {
        command_.Brake();
    }

    UpdateWheels(input.accelerateHeld, input.brakeHeld);
    if (!input.shiftLockHeld)
    {
        ioFrameState.shiftDownRequested = lJustPressed;
        ioFrameState.shiftUpRequested = rJustPressed;
    }
    else
    {
        ioFrameState.shiftDownRequested = false;
        ioFrameState.shiftUpRequested = false;
    }

    inputHistory_.leftHeldPrev = input.steerLeftHeld;
    inputHistory_.rightHeldPrev = input.steerRightHeld;
    inputHistory_.shiftDownHeldPrev = input.shiftDownHeld;
    inputHistory_.shiftUpHeldPrev = input.shiftUpHeld;
}

void CarSystem::PrepareGameplayFrameState(const GameplayInputSnapshot* input,
                                          uint32_t frameCounter,
                                          const Vector3D& worldPosition,
                                          int32_t yawDeg,
                                          bool autoLapEnabled,
                                          GameplayFrameState& ioFrameState)
{
    ioFrameState.frameId = frameCounter;
    ioFrameState.carWorldPosition = worldPosition;
    ioFrameState.carYawDeg = yawDeg;

    if (!autoLapEnabled)
    {
        if (input)
        {
            ApplyGameplayInput(*input, frameCounter, ioFrameState);
        }
    }
    else
    {
        UpdateWheels(false, false);
    }

    TickCommandState();
    WriteCommandsToFrameState(ioFrameState, !autoLapEnabled);
}

void CarSystem::ApplySimulationFrameState(const GameplayFrameState& frameState)
{
    SetWorldPosition(frameState.carWorldPosition);
    SetYawDegrees(frameState.carYawDeg);
    SetRuntimeFrameState(frameState);
}

void CarSystem::SyncRenderState(const Vector3D& renderPosition, int32_t gameplayYawDeg)
{
    SetWorldPosition(renderPosition);
    SetYawDegrees(gameplayYawDeg);
}

CarSystem::DrivetrainDebugSnapshot CarSystem::BuildDrivetrainDebugSnapshot(
    const GameplayFrameState& frameState) const
{
    DrivetrainDebugSnapshot snapshot{};
    const int gearDebugValue = static_cast<int>(frameState.debugGear);
    snapshot.gearChar =
        (gearDebugValue < 0)
            ? 'R'
            : static_cast<char>('0' + std::clamp<int>(gearDebugValue, 0, 9));
    snapshot.throttle = frameState.throttle;
    snapshot.braking = frameState.braking;
    snapshot.speedProxy = frameState.speedProxy;
    snapshot.speedKmh = frameState.debugSpeedKmh;
    snapshot.engineRpm = frameState.debugEngineRpm;
    snapshot.steeringCommand = commandState_.steering;
    snapshot.yawRateDeg = frameState.debugYawRateDeg;
    snapshot.yawStepDeg = frameState.debugYawStepDeg;
    snapshot.planarDx = frameState.debugPlanarDx;
    snapshot.netDz = frameState.debugNetDz;
    return snapshot;
}

void CarSystem::WriteCommandsToFrameState(GameplayFrameState& ioFrameState, bool enabled) const
{
    if (!enabled)
    {
        ioFrameState.throttle = 0;
        ioFrameState.steering = 0;
        ioFrameState.braking = false;
        ioFrameState.shiftUpRequested = false;
        ioFrameState.shiftDownRequested = false;
        ioFrameState.wheelsSpinning = false;
        ioFrameState.brakeHoldFrames = 0;
        return;
    }

    ioFrameState.throttle = commandState_.throttle;
    ioFrameState.steering = commandState_.steering;
    ioFrameState.braking = commandState_.braking;
    ioFrameState.wheelsSpinning = commandState_.wheelsSpinning;
    ioFrameState.brakeHoldFrames = commandState_.latches.brakeHoldFrames;
}

void CarSystem::SubmitRender(RenderPipeline& pipeline, bool logStats)
{
    if (!renderer_) return;
    wheelRig_.Update(wheelInput_);
    wheelRig_.Apply(*renderer_);

    const int32_t renderYawDeg = CurrentRenderYawDeg();
    SRL::Math::Types::Angle yaw =
        SRL::Math::Types::Angle::FromDegrees(SRL::Math::Types::Fxp::BuildRaw(renderYawDeg << 16));
    pipeline.Enqueue(*renderer_, worldPosition_, yaw, logStats);
}
} // namespace Game
