#pragma once

#include <algorithm>
#include <cstdint>

#include "interfaces.hpp"

namespace Game
{
// Minimal drivable car physics for early gameplay integration.
class SimpleCarPhysics final : public ICarPhysics
{
public:
    void Step(GameplayFrameState& ioFrameState,
              const ITrackCollisionQuery* trackQuery,
              Vector3D& ioCarWorldPosition,
              int32_t& ioCarYawDeg) override
    {
        const SRL::Math::Types::Fxp throttleNorm = NormalizePercent(ioFrameState.throttle);
        speed_ += throttleNorm * kAccelPerFrame;

        if (ioFrameState.braking)
        {
            speed_ -= kBrakePerFrame;
        }

        // Apply generic drag every frame.
        speed_ -= speed_ * kDragPerFrame;
        speed_ = Clamp(speed_, kMaxReverseSpeed, kMaxForwardSpeed);

        // Apply extra idle damping when there is no explicit throttle.
        if (ioFrameState.throttle == 0 && !ioFrameState.braking)
        {
            ApplyIdleDamping();
        }

        const SRL::Math::Types::Fxp absSpeed = speed_.Abs();
        ioFrameState.speedProxy =
            ((absSpeed / kMaxForwardSpeed) * SRL::Math::Types::Fxp::BuildRaw(kTargetTopSpeedKmh << 16)).As<int16_t>();

        if (ioFrameState.steering != 0 && absSpeed > kMinSteerSpeed)
        {
            const SRL::Math::Types::Fxp steerNorm = NormalizePercent(ioFrameState.steering);
            const SRL::Math::Types::Fxp steerGain = kSteerDegreesPerFrame + (absSpeed * kSteerSpeedGain);
            const int16_t yawDeltaDeg = (steerNorm * steerGain).As<int16_t>();
            ioCarYawDeg += yawDeltaDeg;
            ioCarYawDeg = NormalizeYaw(ioCarYawDeg);
        }

        const auto yawAngle =
            SRL::Math::Types::Angle::FromDegrees(SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(ioCarYawDeg) << 16));
        const SRL::Math::Types::Fxp sinYaw = SRL::Math::Trigonometry::Sin(yawAngle);
        const SRL::Math::Types::Fxp cosYaw = SRL::Math::Trigonometry::Cos(yawAngle);
        ioCarWorldPosition.X += sinYaw * speed_;
        ioCarWorldPosition.Z += cosYaw * speed_;

        // Keep car slightly above track plane to avoid z-fighting and depth flicker.
        ioCarWorldPosition.Y = kRideHeightY;
        if (trackQuery)
        {
            Vector3D surfaceNormal{};
            int32_t segmentId = -1;
            (void)trackQuery->Sample(ioCarWorldPosition, surfaceNormal, segmentId);
            (void)surfaceNormal;
            (void)segmentId;
        }
    }

private:
    static constexpr int16_t kTargetTopSpeedKmh = 250;

    static SRL::Math::Types::Fxp NormalizePercent(int16_t value)
    {
        const int16_t clamped = std::clamp<int16_t>(value, static_cast<int16_t>(-100), static_cast<int16_t>(100));
        return SRL::Math::Types::Fxp::BuildRaw((static_cast<int32_t>(clamped) << 16) / 100);
    }

    static int32_t NormalizeYaw(int32_t yawDeg)
    {
        int32_t y = yawDeg % 360;
        if (y < 0) y += 360;
        return y;
    }

    static SRL::Math::Types::Fxp Clamp(const SRL::Math::Types::Fxp& value,
                                       const SRL::Math::Types::Fxp& minValue,
                                       const SRL::Math::Types::Fxp& maxValue)
    {
        return SRL::Math::Types::Fxp::Max(minValue, SRL::Math::Types::Fxp::Min(value, maxValue));
    }

    void ApplyIdleDamping()
    {
        if (speed_ > kIdleDampingPerFrame)
        {
            speed_ -= kIdleDampingPerFrame;
            return;
        }
        if (speed_ < -kIdleDampingPerFrame)
        {
            speed_ += kIdleDampingPerFrame;
            return;
        }
        speed_ = SRL::Math::Types::Fxp::BuildRaw(0);
    }

    SRL::Math::Types::Fxp speed_ = SRL::Math::Types::Fxp::BuildRaw(0);

    static constexpr SRL::Math::Types::Fxp kAccelPerFrame = SRL::Math::Types::Fxp::BuildRaw(0x00000CCC);      // ~0.050
    static constexpr SRL::Math::Types::Fxp kBrakePerFrame = SRL::Math::Types::Fxp::BuildRaw(0x0000147B);      // ~0.080
    static constexpr SRL::Math::Types::Fxp kDragPerFrame = SRL::Math::Types::Fxp::BuildRaw(0x000003D7);       // ~0.015
    static constexpr SRL::Math::Types::Fxp kIdleDampingPerFrame = SRL::Math::Types::Fxp::BuildRaw(0x0000020C);// ~0.008
    static constexpr SRL::Math::Types::Fxp kMaxForwardSpeed = SRL::Math::Types::Fxp::BuildRaw(0x00028000);    // 2.50 ~= 250 km/h proxy
    static constexpr SRL::Math::Types::Fxp kMaxReverseSpeed = SRL::Math::Types::Fxp::BuildRaw(-0x00006000);   // -0.375
    static constexpr SRL::Math::Types::Fxp kMinSteerSpeed = SRL::Math::Types::Fxp::BuildRaw(0x00000A3D);      // ~0.04
    static constexpr SRL::Math::Types::Fxp kSteerDegreesPerFrame = SRL::Math::Types::Fxp::BuildRaw(0x00011EB8);// ~1.12
    static constexpr SRL::Math::Types::Fxp kSteerSpeedGain = SRL::Math::Types::Fxp::BuildRaw(0x000070A4);     // ~0.44
    static constexpr SRL::Math::Types::Fxp kRideHeightY = SRL::Math::Types::Fxp::BuildRaw(-3 << 16);
};
} // namespace Game
