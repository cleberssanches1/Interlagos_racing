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
    state->throttleInputHeld = true;
    state->braking = false;
    int16_t next = static_cast<int16_t>(state->throttle + CarSystem::kThrottleStep);
    state->throttle = std::min<int16_t>(next, CarSystem::kThrottleMax);
}

void CarSystem::CarCommandAdapter::Brake()
{
    if (!state) return;
    state->brakeInputHeld = true;
    state->braking = true;
    int16_t next = static_cast<int16_t>(state->throttle - (CarSystem::kThrottleStep * 2));
    state->throttle = std::max<int16_t>(next, 0);
}

void CarSystem::CarCommandAdapter::SteerLeft()
{
    if (!state) return;
    state->steerInputHeld = true;
    state->steerDirection = -1;
}

void CarSystem::CarCommandAdapter::SteerRight()
{
    if (!state) return;
    state->steerInputHeld = true;
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
    if (validOrderCount == 0u && meshCount > 0u)
    {
        validOrderCount = std::min(meshCount, rendererConfig.drawOrder.size());
        for (size_t i = 0; i < validOrderCount; ++i)
        {
            rendererConfig.drawOrder[i] = i;
        }
    }
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
    if (commandState_.steerInputHeld && commandState_.steerDirection != 0)
    {
        const int16_t target =
            (commandState_.steerDirection > 0)
                ? CarSystem::kSteeringMax
                : static_cast<int16_t>(-CarSystem::kSteeringMax);
        // If the player changed side (left->right or right->left), recenter fast first.
        if ((commandState_.steerDirection > 0 && commandState_.steering < 0) ||
            (commandState_.steerDirection < 0 && commandState_.steering > 0))
        {
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
            commandState_.steering = static_cast<int16_t>(
                std::min<int16_t>(target, static_cast<int16_t>(commandState_.steering + CarSystem::kSteeringStep)));
        }
        else if (commandState_.steering > target)
        {
            commandState_.steering = static_cast<int16_t>(
                std::max<int16_t>(target, static_cast<int16_t>(commandState_.steering - CarSystem::kSteeringStep)));
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
    if (!commandState_.throttleInputHeld &&
        !commandState_.braking &&
        commandState_.throttle > 0)
    {
        commandState_.throttle = static_cast<int16_t>(std::max<int16_t>(0, commandState_.throttle - CarSystem::kThrottleDecay));
    }

    // Brake decays when button is released, allowing natural transition to reverse.
    if (commandState_.braking)
    {
        if (!commandState_.brakeInputHeld)
        {
            commandState_.throttle = static_cast<int16_t>(
                std::max<int16_t>(0, commandState_.throttle - CarSystem::kBrakeReleaseDecay));
            if (commandState_.throttle == 0)
            {
                commandState_.braking = false;
            }
        }
    }

    // Reset per-frame latches.
    commandState_.throttleInputHeld = false;
    commandState_.brakeInputHeld = false;
    commandState_.steerInputHeld = false;
    commandState_.steerDirection = 0;
}

void CarSystem::Render(int32_t yawDeg)
{
    yawDeg_ = NormalizeYawDeg(yawDeg);
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
