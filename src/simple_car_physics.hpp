#pragma once

#include <algorithm>
#include <cstdint>

#include "interfaces.hpp"

namespace Game
{
// Lightweight Saturn-friendly vehicle model:
// - body-frame longitudinal/lateral speeds
// - slip-inspired yaw/lateral coupling
// - surface-normal aligned attitude (pitch/roll)
class SimpleCarPhysics final : public ICarPhysics
{
public:
    void Step(GameplayFrameState& ioFrameState,
              const ITrackCollisionQuery* trackQuery,
              Vector3D& ioCarWorldPosition,
              int32_t& ioCarYawDeg) override
    {
        const Fxp throttleNorm = NormalizePercent(ioFrameState.throttle);
        const Fxp steerNorm = NormalizePercent(ioFrameState.steering);

        TrackSurfaceSample surfaceSample{};
        bool hasTrackSample = TryGetCachedSurface(ioFrameState,
                                                  ioCarWorldPosition,
                                                  kAsphaltFamilyId,
                                                  surfaceSample);
        if (!hasTrackSample)
        {
            hasTrackSample = QuerySurface(trackQuery, ioCarWorldPosition, surfaceSample);
            if (hasTrackSample)
            {
                StoreCachedSurface(ioFrameState, kAsphaltFamilyId, surfaceSample);
            }
        }
        const Vector3D sampledNormal = hasTrackSample ? surfaceSample.surfaceNormal : DefaultSurfaceNormal();
        smoothedSurfaceNormal_ = BlendAndNormalizeSurfaceNormal(smoothedSurfaceNormal_,
                                                                sampledNormal,
                                                                kSurfaceNormalBlendAlpha);
        if (!surfaceNormalInitialized_)
        {
            smoothedSurfaceNormal_ = sampledNormal;
            surfaceNormalInitialized_ = true;
        }

        ioCarYawDeg = NormalizeYaw(ioCarYawDeg);
        const Angle yawAngle = Angle::FromDegrees(Fxp::BuildRaw(ioCarYawDeg << 16));
        const Vector3D forward(SRL::Math::Trigonometry::Sin(yawAngle),
                               Fxp::BuildRaw(0),
                               SRL::Math::Trigonometry::Cos(yawAngle));
        const Vector3D right(forward.Z, Fxp::BuildRaw(0), -forward.X);

        Fxp longitudinalAccel = throttleNorm * kEngineAccelPerFrame;
        if (ioFrameState.braking)
        {
            longitudinalAccel -= kBrakePerFrame;
        }
        longitudinalAccel -= forwardSpeed_ * kDragPerFrame;
        if (ioFrameState.throttle == 0 && !ioFrameState.braking)
        {
            longitudinalAccel -= forwardSpeed_ * kCoastDampingPerFrame;
        }
        longitudinalAccel -= ComputeSlopeAlongForward(smoothedSurfaceNormal_, forward) * kSlopeGravityPerFrame;
        forwardSpeed_ += longitudinalAccel;
        forwardSpeed_ = Clamp(forwardSpeed_, kMaxReverseSpeed, kMaxForwardSpeed);

        const Fxp lateralTarget = steerNorm * forwardSpeed_ * kSteerLateralGain;
        lateralSpeed_ += (lateralTarget - lateralSpeed_) * kLateralResponsePerFrame;
        const Fxp lateralSpeedLimit = kLatBaseLimit + (forwardSpeed_.Abs() * kLatSpeedGain);
        lateralSpeed_ = Clamp(lateralSpeed_, -lateralSpeedLimit, lateralSpeedLimit);

        const Fxp yawTarget = steerNorm * forwardSpeed_.Abs() * kYawRateGain;
        yawRateDegPerFrame_ += (yawTarget - yawRateDegPerFrame_) * kYawResponsePerFrame;
        yawRateDegPerFrame_ -= (lateralSpeed_ * kYawSlipGain);
        yawRateDegPerFrame_ -= yawRateDegPerFrame_ * kYawDampingPerFrame;
        yawRateDegPerFrame_ = Clamp(yawRateDegPerFrame_,
                                    Fxp::BuildRaw(-(static_cast<int32_t>(kYawRateLimitDeg) << 16)),
                                    Fxp::BuildRaw(static_cast<int32_t>(kYawRateLimitDeg) << 16));

        ioCarYawDeg += yawRateDegPerFrame_.As<int16_t>();
        ioCarYawDeg = NormalizeYaw(ioCarYawDeg);

        const Vector3D worldVelocity((forward.X * forwardSpeed_) + (right.X * lateralSpeed_),
                                     Fxp::BuildRaw(0),
                                     (forward.Z * forwardSpeed_) + (right.Z * lateralSpeed_));
        ioCarWorldPosition.X += worldVelocity.X;
        ioCarWorldPosition.Z += worldVelocity.Z;

        if (hasTrackSample && surfaceSample.hasSurfaceY)
        {
            const Fxp desiredY = surfaceSample.surfaceY + kRideHeightY;
            ioCarWorldPosition.Y += (desiredY - ioCarWorldPosition.Y) * kRideFollowAlpha;
        }

        const int16_t terrainPitchDeg = ComputeTerrainPitchDeg(smoothedSurfaceNormal_, forward, right);
        const int16_t terrainRollDeg = ComputeTerrainRollDeg(smoothedSurfaceNormal_, forward, right);
        const int16_t dynPitchDeg =
            ClampToRange(static_cast<int16_t>(-(longitudinalAccel * kPitchAccelGain).As<int16_t>()), -8, 8);
        const int16_t dynRollDeg = ClampToRange(
            static_cast<int16_t>((lateralSpeed_ * kRollSlipGain).As<int16_t>() +
                                 (steerNorm * forwardSpeed_.Abs() * kRollSteerGain).As<int16_t>()),
            -10,
            10);

        const int16_t desiredPitchDeg = ClampToRange(static_cast<int16_t>(terrainPitchDeg + dynPitchDeg), -18, 18);
        const int16_t desiredRollDeg = ClampToRange(static_cast<int16_t>(terrainRollDeg + dynRollDeg), -22, 22);
        pitchDeg_ += (Fxp::BuildRaw(desiredPitchDeg << 16) - pitchDeg_) * kAttitudeResponsePerFrame;
        rollDeg_ += (Fxp::BuildRaw(desiredRollDeg << 16) - rollDeg_) * kAttitudeResponsePerFrame;

        ioFrameState.carWorldPosition = ioCarWorldPosition;
        ioFrameState.carYawDeg = ioCarYawDeg;
        ioFrameState.activeSegmentId = hasTrackSample ? surfaceSample.segmentId : -1;
        ioFrameState.surfaceNormalWorld = smoothedSurfaceNormal_;
        ioFrameState.carPitchDeg = pitchDeg_.As<int16_t>();
        ioFrameState.carRollDeg = rollDeg_.As<int16_t>();
        ioFrameState.yawRateDeg = yawRateDegPerFrame_.As<int16_t>();
        ioFrameState.speedProxy = ((forwardSpeed_.Abs() / kMaxForwardSpeed) * kTopSpeedKmhFxp).As<int16_t>();
    }

private:
    using Fxp = SRL::Math::Types::Fxp;
    using Angle = SRL::Math::Types::Angle;

    // 0 => use TrackSystem drivable-ground family set (asphalt whitelist).
    static constexpr uint16_t kAsphaltFamilyId = 0u;
    static constexpr int16_t kYawRateLimitDeg = 14;
    static constexpr int16_t kTopSpeedKmh = 260;
    static Vector3D DefaultSurfaceNormal()
    {
        return Vector3D(0.0, -1.0, 0.0);
    }

    static Fxp NormalizePercent(int16_t value)
    {
        const int16_t clamped = std::clamp<int16_t>(value, static_cast<int16_t>(-100), static_cast<int16_t>(100));
        return Fxp::BuildRaw((static_cast<int32_t>(clamped) << 16) / 100);
    }

    static int32_t NormalizeYaw(int32_t yawDeg)
    {
        int32_t y = yawDeg % 360;
        if (y < 0) y += 360;
        return y;
    }

    static Fxp Clamp(const Fxp& value, const Fxp& minValue, const Fxp& maxValue)
    {
        return Fxp::Max(minValue, Fxp::Min(value, maxValue));
    }

    static int16_t ClampToRange(int16_t value, int16_t minValue, int16_t maxValue)
    {
        return std::clamp<int16_t>(value, minValue, maxValue);
    }

    static Vector3D NormalizeApprox(const Vector3D& n)
    {
        const int64_t nx = static_cast<int64_t>(n.X.RawValue());
        const int64_t ny = static_cast<int64_t>(n.Y.RawValue());
        const int64_t nz = static_cast<int64_t>(n.Z.RawValue());
        auto abs64 = [](int64_t v) -> int64_t { return (v < 0) ? -v : v; };
        const int64_t maxComp =
            std::max<int64_t>(1, std::max<int64_t>(abs64(nx), std::max<int64_t>(abs64(ny), abs64(nz))));
        return Vector3D(
            Fxp::BuildRaw(static_cast<int32_t>((nx << 16) / maxComp)),
            Fxp::BuildRaw(static_cast<int32_t>((ny << 16) / maxComp)),
            Fxp::BuildRaw(static_cast<int32_t>((nz << 16) / maxComp)));
    }

    static Vector3D BlendAndNormalizeSurfaceNormal(const Vector3D& current,
                                                   const Vector3D& target,
                                                   const Fxp& alpha)
    {
        const Fxp one = Fxp::BuildRaw(1 << 16);
        const Fxp inv = one - alpha;
        const Vector3D blended((current.X * inv) + (target.X * alpha),
                               (current.Y * inv) + (target.Y * alpha),
                               (current.Z * inv) + (target.Z * alpha));
        return NormalizeApprox(blended);
    }

    static Fxp ComputeSlopeAlongForward(const Vector3D& normal, const Vector3D& forward)
    {
        const Fxp nyAbs = normal.Y.Abs();
        const Fxp denom = (nyAbs > Fxp::BuildRaw(1 << 10)) ? nyAbs : Fxp::BuildRaw(1 << 10);
        const Fxp planar = (forward.X * normal.X) + (forward.Z * normal.Z);
        return planar / denom;
    }

    static int16_t ComputeTerrainPitchDeg(const Vector3D& normal,
                                          const Vector3D& forward,
                                          const Vector3D& right)
    {
        (void)right;
        const Fxp upAbs = (normal.Y < Fxp::BuildRaw(0)) ? -normal.Y : normal.Y;
        const Fxp up = Fxp::Max(upAbs, Fxp::BuildRaw(1 << 10));
        const Fxp forwardComp = (normal.X * forward.X) + (normal.Z * forward.Z);
        const auto angle = SRL::Math::Trigonometry::Atan2(forwardComp, up);
        const int32_t pitchDeg = (angle.ToDegrees().RawValue() + (1 << 15)) >> 16;
        return ClampToRange(static_cast<int16_t>(pitchDeg), static_cast<int16_t>(-18), static_cast<int16_t>(18));
    }

    static int16_t ComputeTerrainRollDeg(const Vector3D& normal,
                                         const Vector3D& forward,
                                         const Vector3D& right)
    {
        (void)forward;
        const Fxp upAbs = (normal.Y < Fxp::BuildRaw(0)) ? -normal.Y : normal.Y;
        const Fxp up = Fxp::Max(upAbs, Fxp::BuildRaw(1 << 10));
        const Fxp rightComp = (normal.X * right.X) + (normal.Z * right.Z);
        const auto angle = SRL::Math::Trigonometry::Atan2(rightComp, up);
        const int32_t rollDeg = (angle.ToDegrees().RawValue() + (1 << 15)) >> 16;
        return ClampToRange(static_cast<int16_t>(rollDeg), static_cast<int16_t>(-22), static_cast<int16_t>(22));
    }

    static bool QuerySurface(const ITrackCollisionQuery* trackQuery,
                             const Vector3D& worldPosition,
                             TrackSurfaceSample& outSample)
    {
        if (!trackQuery) return false;
        return trackQuery->SampleSurface(worldPosition, kAsphaltFamilyId, outSample);
    }

    static bool SamePosition(const Vector3D& a, const Vector3D& b)
    {
        return a.X.RawValue() == b.X.RawValue() &&
               a.Y.RawValue() == b.Y.RawValue() &&
               a.Z.RawValue() == b.Z.RawValue();
    }

    static bool TryGetCachedSurface(const GameplayFrameState& frameState,
                                    const Vector3D& worldPosition,
                                    uint16_t surfaceFamilyId,
                                    TrackSurfaceSample& outSample)
    {
        if (!frameState.hasCachedSurfaceSample) return false;
        if (frameState.cachedSurfaceFamilyId != surfaceFamilyId) return false;
        if (!SamePosition(frameState.cachedSurfaceSample.worldPosition, worldPosition)) return false;
        outSample = frameState.cachedSurfaceSample;
        return true;
    }

    static void StoreCachedSurface(GameplayFrameState& frameState,
                                   uint16_t surfaceFamilyId,
                                   const TrackSurfaceSample& sample)
    {
        frameState.cachedSurfaceSample = sample;
        frameState.cachedSurfaceFamilyId = surfaceFamilyId;
        frameState.hasCachedSurfaceSample = true;
    }

    Fxp forwardSpeed_ = Fxp::BuildRaw(0);
    Fxp lateralSpeed_ = Fxp::BuildRaw(0);
    Fxp yawRateDegPerFrame_ = Fxp::BuildRaw(0);
    Fxp pitchDeg_ = Fxp::BuildRaw(0);
    Fxp rollDeg_ = Fxp::BuildRaw(0);
    Vector3D smoothedSurfaceNormal_{0.0, -1.0, 0.0};
    bool surfaceNormalInitialized_ = false;

    static constexpr Fxp kEngineAccelPerFrame = Fxp::BuildRaw(0x00000A3D);       // ~0.040
    static constexpr Fxp kBrakePerFrame = Fxp::BuildRaw(0x00001C29);             // ~0.110
    static constexpr Fxp kDragPerFrame = Fxp::BuildRaw(0x0000030D);              // ~0.012
    static constexpr Fxp kCoastDampingPerFrame = Fxp::BuildRaw(0x000001F5);      // ~0.0077
    static constexpr Fxp kSlopeGravityPerFrame = Fxp::BuildRaw(0x0000047B);      // ~0.017
    static constexpr Fxp kSteerLateralGain = Fxp::BuildRaw(0x00006666);          // ~0.40
    static constexpr Fxp kLateralResponsePerFrame = Fxp::BuildRaw(0x00004CCD);   // ~0.30
    static constexpr Fxp kLatBaseLimit = Fxp::BuildRaw(0x0000247B);              // ~0.142
    static constexpr Fxp kLatSpeedGain = Fxp::BuildRaw(0x00003333);              // ~0.20
    static constexpr Fxp kYawRateGain = Fxp::BuildRaw(0x000126E9);               // ~1.15
    static constexpr Fxp kYawResponsePerFrame = Fxp::BuildRaw(0x00004000);       // 0.25
    static constexpr Fxp kYawSlipGain = Fxp::BuildRaw(0x00008000);               // 0.50
    static constexpr Fxp kYawDampingPerFrame = Fxp::BuildRaw(0x00001EB8);        // ~0.12
    static constexpr Fxp kRideHeightY = Fxp::BuildRaw(-(3 << 16));
    static constexpr Fxp kRideFollowAlpha = Fxp::BuildRaw(0x000070A4);           // ~0.44
    static constexpr Fxp kSurfaceNormalBlendAlpha = Fxp::BuildRaw(0x00003333);   // 0.20
    static constexpr Fxp kAttitudeResponsePerFrame = Fxp::BuildRaw(0x000028F6);  // ~0.16
    static constexpr Fxp kPitchAccelGain = Fxp::BuildRaw(0x00006666);            // ~0.40
    static constexpr Fxp kRollSlipGain = Fxp::BuildRaw(0x00018000);              // 1.5
    static constexpr Fxp kRollSteerGain = Fxp::BuildRaw(0x00006666);             // ~0.40
    static constexpr Fxp kTopSpeedKmhFxp = Fxp::BuildRaw(kTopSpeedKmh << 16);
    static constexpr Fxp kMaxForwardSpeed = Fxp::BuildRaw(0x00032000);           // 3.125
    static constexpr Fxp kMaxReverseSpeed = Fxp::BuildRaw(-0x0000A3D7);          // -0.64
};
} // namespace Game
