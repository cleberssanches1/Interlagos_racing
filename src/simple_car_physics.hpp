#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "interfaces.hpp"

namespace Game
{
// Lightweight Saturn-friendly car physics:
// - fixed-point only
// - no dynamic allocation
// - short state vector
class SimpleCarPhysics final : public ICarPhysics
{
public:
    void Step(GameplayFrameState& ioFrameState,
              const ITrackCollisionQuery* trackQuery,
              Vector3D& ioCarWorldPosition,
              int32_t& ioCarYawDeg) override
    {
        if (ioFrameState.resetRequested)
        {
            ResetDynamics(ioFrameState);
            return;
        }

        const SRL::Math::Types::Fxp throttleNorm = NormalizePercent(ioFrameState.throttle);
        const SRL::Math::Types::Fxp steerNorm = NormalizePercent(ioFrameState.steering);

        // F = m*a simplified into per-frame accelerations in fixed-point.
        forwardSpeed_ += throttleNorm * kEngineAccelPerFrame;
        if (ioFrameState.braking)
        {
            forwardSpeed_ -= kBrakeDecelPerFrame;
        }

        const SRL::Math::Types::Fxp speedAbs = forwardSpeed_.Abs();
        // Drag model uses linear + quadratic terms to avoid runaway speed.
        const SRL::Math::Types::Fxp aeroDrag = speedAbs * forwardSpeed_ * kAeroDragCoeff;
        forwardSpeed_ -= aeroDrag;
        forwardSpeed_ -= forwardSpeed_ * kRollingDragCoeff;

        if (ioFrameState.throttle == 0 && !ioFrameState.braking)
        {
            ApplyCoastDamping();
        }

        forwardSpeed_ = Clamp(forwardSpeed_, SRL::Math::Types::Fxp::BuildRaw(0), kMaxForwardSpeed);

        // Steering model: first-order steer response + speed-sensitive yaw.
        // World/camera handedness is inverted for steering input on this track setup.
        // Negate steering command so Left/Right map correctly for the player.
        const SRL::Math::Types::Fxp targetSteerDeg =
            SRL::Math::Types::Fxp::BuildRaw(-steerNorm.RawValue()) * kMaxSteerDeg;
        steerDeg_ += (targetSteerDeg - steerDeg_) * kSteerResponse;

        const SRL::Math::Types::Fxp speedRatio =
            Clamp(speedAbs / kMaxForwardSpeed,
                  SRL::Math::Types::Fxp::BuildRaw(0),
                  SRL::Math::Types::Fxp::BuildRaw(1 << 16));
        const SRL::Math::Types::Fxp steerGrip =
            SRL::Math::Types::Fxp::BuildRaw(1 << 16) - (speedRatio * kHighSpeedSteerLoss);

        yawRateDegPerFrame_ = steerDeg_ * (forwardSpeed_ * kYawFromSpeedCoeff) * steerGrip;
        yawRateDegPerFrame_ -= yawRateDegPerFrame_ * kYawDamping;
        ioCarYawDeg = NormalizeYaw(ioCarYawDeg + yawRateDegPerFrame_.As<int16_t>());

        const auto yawAngle =
            SRL::Math::Types::Angle::FromDegrees(
                SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(ioCarYawDeg) << 16));
        const SRL::Math::Types::Fxp sinYaw = SRL::Math::Trigonometry::Sin(yawAngle);
        const SRL::Math::Types::Fxp cosYaw = SRL::Math::Trigonometry::Cos(yawAngle);

        lateralSpeed_ += steerDeg_ * forwardSpeed_ * kLateralCouplingCoeff;
        lateralSpeed_ -= lateralSpeed_ * kLateralDampingCoeff;

        const SRL::Math::Types::Fxp negCosYaw =
            SRL::Math::Types::Fxp::BuildRaw(-cosYaw.RawValue());
        ioCarWorldPosition.X += (sinYaw * forwardSpeed_) + (cosYaw * lateralSpeed_);
        ioCarWorldPosition.Z += (negCosYaw * forwardSpeed_) + (sinYaw * lateralSpeed_);

        int32_t sampledSegmentId = -1;
        Vector3D sampledNormal{};
        if (trackQuery)
        {
            (void)trackQuery->Sample(ioCarWorldPosition, sampledNormal, sampledSegmentId);
            const bool segmentChanged = (sampledSegmentId > 0) && (sampledSegmentId != lastSurfaceSegmentId_);
            if (surfaceProbeCooldown_ == 0u || segmentChanged || !surfaceYInitialized_)
            {
                ProbeSurfaceY(trackQuery, ioCarWorldPosition);
                surfaceProbeCooldown_ = kSurfaceProbeIntervalFrames;
            }
            else
            {
                --surfaceProbeCooldown_;
            }
        }

        if (surfaceYInitialized_)
        {
            SRL::Math::Types::Fxp deltaY = surfaceYTarget_ - ioCarWorldPosition.Y;
            const SRL::Math::Types::Fxp minYStep =
                SRL::Math::Types::Fxp::BuildRaw(-kMaxYStepPerFrame.RawValue());
            deltaY = Clamp(deltaY, minYStep, kMaxYStepPerFrame);
            ioCarWorldPosition.Y += deltaY;
        }

        ioFrameState.activeSegmentId = sampledSegmentId;
        ioFrameState.speedProxy = BuildSpeedProxy(forwardSpeed_);
    }

private:
    static constexpr int16_t kTargetTopSpeedKmh = 260;
    static constexpr std::array<uint16_t, 16> kDriveableFamilies = {
        337u, // F05564
        32u,  // F04764
        148u, // F01064
        2u,   // F01864
        74u,  // F02564
        321u, // F04364
        46u,  // F04664
        78u,  // F05464
        218u, // F00164
        77u,  // F00264
        169u, // F00364
        362u, // F00464
        367u, // F00564
        112u, // F06164
        283u, // F06264
        1u    // F06364
    };

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

    static int16_t BuildSpeedProxy(const SRL::Math::Types::Fxp& forwardSpeed)
    {
        const SRL::Math::Types::Fxp normalized =
            Clamp(forwardSpeed / kMaxForwardSpeed,
                  SRL::Math::Types::Fxp::BuildRaw(0),
                  SRL::Math::Types::Fxp::BuildRaw(1 << 16));
        const SRL::Math::Types::Fxp topKmh = SRL::Math::Types::Fxp::BuildRaw(kTargetTopSpeedKmh << 16);
        return (normalized * topKmh).As<int16_t>();
    }

    void ApplyCoastDamping()
    {
        if (forwardSpeed_ > kCoastDampingPerFrame)
        {
            forwardSpeed_ -= kCoastDampingPerFrame;
            return;
        }
        forwardSpeed_ = SRL::Math::Types::Fxp::BuildRaw(0);
    }

    void ProbeSurfaceY(const ITrackCollisionQuery* trackQuery,
                       const Vector3D& worldPosition)
    {
        if (!trackQuery) return;

        SRL::Math::Types::Fxp sampledY{};
        int32_t sampledSegmentId = -1;
        if (!trackQuery->SampleSurfaceYByFamilySet(worldPosition,
                                                   kDriveableFamilies.data(),
                                                   kDriveableFamilies.size(),
                                                   sampledY,
                                                   &sampledSegmentId))
        {
            return;
        }

        surfaceYTarget_ = sampledY + kRideHeightOffset;
        surfaceYInitialized_ = true;
        if (sampledSegmentId > 0)
        {
            lastSurfaceSegmentId_ = sampledSegmentId;
        }
    }

    void ResetDynamics(GameplayFrameState& ioFrameState)
    {
        forwardSpeed_ = SRL::Math::Types::Fxp::BuildRaw(0);
        lateralSpeed_ = SRL::Math::Types::Fxp::BuildRaw(0);
        yawRateDegPerFrame_ = SRL::Math::Types::Fxp::BuildRaw(0);
        steerDeg_ = SRL::Math::Types::Fxp::BuildRaw(0);
        surfaceProbeCooldown_ = 0u;
        ioFrameState.speedProxy = 0;
    }

    SRL::Math::Types::Fxp forwardSpeed_ = SRL::Math::Types::Fxp::BuildRaw(0);
    SRL::Math::Types::Fxp lateralSpeed_ = SRL::Math::Types::Fxp::BuildRaw(0);
    SRL::Math::Types::Fxp yawRateDegPerFrame_ = SRL::Math::Types::Fxp::BuildRaw(0);
    SRL::Math::Types::Fxp steerDeg_ = SRL::Math::Types::Fxp::BuildRaw(0);
    SRL::Math::Types::Fxp surfaceYTarget_ = SRL::Math::Types::Fxp::BuildRaw(0);
    int16_t lastSurfaceSegmentId_ = -1;
    uint8_t surfaceProbeCooldown_ = 0u;
    bool surfaceYInitialized_ = false;

    static constexpr uint8_t kSurfaceProbeIntervalFrames = 2u;
    static constexpr SRL::Math::Types::Fxp kEngineAccelPerFrame = SRL::Math::Types::Fxp::BuildRaw(0x00003852); // ~0.220
    static constexpr SRL::Math::Types::Fxp kBrakeDecelPerFrame = SRL::Math::Types::Fxp::BuildRaw(0x00003852);  // ~0.220
    static constexpr SRL::Math::Types::Fxp kAeroDragCoeff = SRL::Math::Types::Fxp::BuildRaw(0x00000083);       // ~0.0020
    static constexpr SRL::Math::Types::Fxp kRollingDragCoeff = SRL::Math::Types::Fxp::BuildRaw(0x000001AA);    // ~0.0065
    static constexpr SRL::Math::Types::Fxp kCoastDampingPerFrame = SRL::Math::Types::Fxp::BuildRaw(0x00000106);// ~0.0040
    static constexpr SRL::Math::Types::Fxp kMaxForwardSpeed = SRL::Math::Types::Fxp::BuildRaw(0x00070000);     // ~7.0
    static constexpr SRL::Math::Types::Fxp kMaxSteerDeg = SRL::Math::Types::Fxp::BuildRaw(6 << 16);           // 6 deg
    static constexpr SRL::Math::Types::Fxp kSteerResponse = SRL::Math::Types::Fxp::BuildRaw(0x00006000);      // ~0.375
    static constexpr SRL::Math::Types::Fxp kHighSpeedSteerLoss = SRL::Math::Types::Fxp::BuildRaw(0x00008000); // 0.5
    static constexpr SRL::Math::Types::Fxp kYawFromSpeedCoeff = SRL::Math::Types::Fxp::BuildRaw(0x00003000);  // ~0.1875
    static constexpr SRL::Math::Types::Fxp kYawDamping = SRL::Math::Types::Fxp::BuildRaw(0x00002000);         // 0.125
    static constexpr SRL::Math::Types::Fxp kLateralCouplingCoeff = SRL::Math::Types::Fxp::BuildRaw(0x00000800);// 0.03125
    static constexpr SRL::Math::Types::Fxp kLateralDampingCoeff = SRL::Math::Types::Fxp::BuildRaw(0x00004000);// 0.25
    static constexpr SRL::Math::Types::Fxp kRideHeightOffset = SRL::Math::Types::Fxp::BuildRaw(-(1 << 13));   // -0.125
    static constexpr SRL::Math::Types::Fxp kMaxYStepPerFrame = SRL::Math::Types::Fxp::BuildRaw(0x00004000);   // 0.25
};
} // namespace Game
