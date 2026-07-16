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
    state->latches.SetThrottleHeld(true);
    state->SetBraking(false);
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
    state->latches.SetBrakeHeld(true);
    state->SetBraking(true);
    if (state->latches.brakeHoldFrames < 255u) ++state->latches.brakeHoldFrames;
    state->latches.accelHoldFrames = 0u;
    int16_t next = static_cast<int16_t>(state->throttle - (CarSystem::kThrottleStep * 2));
    state->throttle = std::max<int16_t>(next, 0);
}

void CarSystem::CarCommandAdapter::SteerLeft()
{
    if (!state) return;
    state->latches.SetSteerHeld(true);
    state->steerDirection = -1;
}

void CarSystem::CarCommandAdapter::SteerRight()
{
    if (!state) return;
    state->latches.SetSteerHeld(true);
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
        commandState_.SetWheelsSpinning(true);
    }
    if (stop)
    {
        commandState_.SetWheelsSpinning(false);
    }
    if (commandState_.WheelsSpinning())
    {
        ++commandState_.wheelSpinTicks;
    }
}

void CarSystem::TickCommandState()
{
    // Steering model:
    // - while held: move toward full lock in requested direction
    // - released: return smoothly to center
    if (commandState_.latches.SteerHeld() && commandState_.steerDirection != 0)
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
            !commandState_.Braking() &&
            commandState_.latches.ThrottleHeld() &&
            ((wheelInput_.speedKmh <= CarSystem::kLaunchSteerSnapSpeedKmh) ||
             (std::abs(commandState_.steering) <= CarSystem::kSteeringStep) ||
             steeringCrossing);
        // In brake/reverse mode steering follows the current arrow directly.
        // No cross-center smoothing or one-shot limits.
        if (commandState_.Braking() || launchSteerSnap)
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
    if (!commandState_.latches.ThrottleHeld() &&
        !commandState_.Braking() &&
        commandState_.throttle > 0)
    {
        commandState_.throttle = static_cast<int16_t>(std::max<int16_t>(0, commandState_.throttle - CarSystem::kThrottleDecay));
    }
    if (!commandState_.latches.ThrottleHeld())
    {
        commandState_.latches.accelHoldFrames = 0u;
    }

    // Brake decays when button is released, allowing natural transition to reverse.
    if (commandState_.Braking())
    {
        if (!commandState_.latches.BrakeHeld())
        {
            commandState_.throttle = static_cast<int16_t>(
                std::max<int16_t>(0, commandState_.throttle - CarSystem::kBrakeReleaseDecay));
            if (commandState_.throttle == 0)
            {
                commandState_.SetBraking(false);
            }
            commandState_.latches.brakeHoldFrames = 0u;
        }
    }
    else
    {
        commandState_.latches.brakeHoldFrames = 0u;
    }

    // Reset per-frame latches.
    commandState_.latches.SetThrottleHeld(false);
    commandState_.latches.SetBrakeHeld(false);
    commandState_.latches.SetSteerHeld(false);
    commandState_.steerDirection = 0;
}

void CarSystem::SetRuntimeFrameState(const GameplayFrameState& frameState)
{
    runtimeDebug_.speedProxy = frameState.speedProxy;
    runtimeDebug_.speedKmh = frameState.carSpeedKmh;
    runtimeDebug_.engineRpm = frameState.carEngineRpm;
    runtimeDebug_.shiftRpmBefore = frameState.carShiftRpmBefore;
    runtimeDebug_.shiftRpmAfter = frameState.carShiftRpmAfter;
    runtimeDebug_.shiftFrames = frameState.carShiftFrames;
    runtimeDebug_.groundRearY = frameState.debugGroundYRear;
    runtimeDebug_.groundFrontY = frameState.debugGroundYFront;
    runtimeDebug_.groundTargetY = frameState.debugGroundYTarget;
    runtimeDebug_.groundFaceIndex = frameState.groundFaceIndex;
    runtimeDebug_.wallPushX = frameState.debugWallPushX;
    runtimeDebug_.wallPushZ = frameState.debugWallPushZ;
    runtimeDebug_.yawRateDeg = frameState.debugYawRateDeg;
    runtimeDebug_.yawStepDeg = frameState.debugYawStepDeg;
    runtimeDebug_.planarDx = frameState.debugPlanarDx;
    runtimeDebug_.netDz = frameState.debugNetDz;
    runtimeDebug_.gear = static_cast<int8_t>(frameState.carGear);
    runtimeDebug_.groundMask = frameState.debugGroundMask;
    runtimeDebug_.groundSurfaceType = frameState.groundSurfaceType;
    runtimeDebug_.groundFamilyId = frameState.groundFamilyId;
    runtimeDebug_.SetBraking(frameState.braking);
    runtimeDebug_.SetWallHit(frameState.debugWallHit != 0u);

    wheelInput_.speedKmh = frameState.speedProxy;
    wheelInput_.steering = frameState.steering;
    wheelInput_.yawStepDeg = frameState.debugYawStepDeg;
    wheelInput_.groundRearY = frameState.debugGroundYRear;
    wheelInput_.groundFrontY = frameState.debugGroundYFront;
    wheelInput_.groundRearYRaw = frameState.debugGroundYRearRaw;
    wheelInput_.groundFrontYRaw = frameState.debugGroundYFrontRaw;
    wheelInput_.groundMask = frameState.debugGroundMask;
    wheelInput_.braking = frameState.braking ? 1u : 0u;
}

void CarSystem::ApplyGameplayInput(const GameplayInputSnapshot& input,
                                   uint32_t frameCounter,
                                   GameplayFrameState& ioFrameState)
{
    lastGameplayInput_ = input;

    const bool steerLeftHeld = input.SteerLeftHeld();
    const bool steerRightHeld = input.SteerRightHeld();
    const bool shiftDownHeld = input.ShiftDownHeld();
    const bool shiftUpHeld = input.ShiftUpHeld();
    const bool accelerateHeld = input.AccelerateHeld();
    const bool brakeHeld = input.BrakeHeld();
    const bool shiftLockHeld = input.ShiftLockHeld();

    const bool leftJustPressed = steerLeftHeld && !inputHistory_.leftHeldPrev;
    const bool rightJustPressed = steerRightHeld && !inputHistory_.rightHeldPrev;
    const bool lJustPressed = shiftDownHeld && !inputHistory_.shiftDownHeldPrev;
    const bool rJustPressed = shiftUpHeld && !inputHistory_.shiftUpHeldPrev;
    if (leftJustPressed) inputHistory_.lastLeftPressFrame = frameCounter;
    if (rightJustPressed) inputHistory_.lastRightPressFrame = frameCounter;

    if (accelerateHeld) command_.Accelerate();

    int8_t desiredSteerDir = 0;
    if (steerLeftHeld && !steerRightHeld)
    {
        desiredSteerDir = -1;
    }
    else if (steerRightHeld && !steerLeftHeld)
    {
        desiredSteerDir = 1;
    }
    else if (steerLeftHeld && steerRightHeld)
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
    if (brakeHeld)
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

    if (!suppressBrakeThisFrame && brakeHeld)
    {
        command_.Brake();
    }

    UpdateWheels(accelerateHeld, brakeHeld);
    if (!shiftLockHeld)
    {
        ioFrameState.shiftDownRequested = lJustPressed;
        ioFrameState.shiftUpRequested = rJustPressed;
    }
    else
    {
        ioFrameState.shiftDownRequested = false;
        ioFrameState.shiftUpRequested = false;
    }

    inputHistory_.leftHeldPrev = steerLeftHeld;
    inputHistory_.rightHeldPrev = steerRightHeld;
    inputHistory_.shiftDownHeldPrev = shiftDownHeld;
    inputHistory_.shiftUpHeldPrev = shiftUpHeld;
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
        lastGameplayInput_ = {};
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

CarSystem::DrivetrainDebugSnapshot CarSystem::BuildDrivetrainDebugSnapshot() const
{
    DrivetrainDebugSnapshot snapshot{};
    const int gearDebugValue = static_cast<int>(runtimeDebug_.gear);
    snapshot.gearChar =
        (gearDebugValue < 0)
            ? 'R'
            : ((gearDebugValue == 0)
                ? 'N'
                : static_cast<char>('0' + std::clamp<int>(gearDebugValue, 0, 9)));
    snapshot.throttle = commandState_.throttle;
    snapshot.SetBraking(runtimeDebug_.Braking());
    snapshot.speedProxy = runtimeDebug_.speedProxy;
    snapshot.speedKmh = runtimeDebug_.speedKmh;
    snapshot.engineRpm = runtimeDebug_.engineRpm;
    snapshot.shiftRpmBefore = runtimeDebug_.shiftRpmBefore;
    snapshot.shiftRpmAfter = runtimeDebug_.shiftRpmAfter;
    snapshot.shiftFrames = runtimeDebug_.shiftFrames;
    snapshot.steeringCommand = commandState_.steering;
    snapshot.yawRateDeg = runtimeDebug_.yawRateDeg;
    snapshot.yawStepDeg = runtimeDebug_.yawStepDeg;
    snapshot.planarDx = runtimeDebug_.planarDx;
    snapshot.netDz = runtimeDebug_.netDz;
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
    ioFrameState.braking = commandState_.Braking();
    ioFrameState.wheelsSpinning = commandState_.WheelsSpinning();
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
