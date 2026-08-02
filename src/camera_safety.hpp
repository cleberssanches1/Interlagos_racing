#pragma once

#include <srl.hpp>

// Camera safety utilities to avoid unstable look-at states.
namespace CameraSafety
{
using Fxp = SRL::Math::Types::Fxp;
using Vector3D = SRL::Math::Types::Vector3D;

struct Config
{
    Fxp minTargetDistance = Fxp::BuildRaw(20 << 16);
    Fxp fallbackLookDistance = Fxp::BuildRaw(128 << 16);
    // Zero disables world coordinate clamping.
    Fxp worldClamp = Fxp::BuildRaw(0);
    // Y-down: camera.Y must stay below car.Y - this amount (higher altitude).
    // Default raised for slope chase; CameraSystem may override per pitch.
    Fxp minCameraHeightAboveTarget = Fxp::BuildRaw(12 << 16);
};

inline Fxp ClampFxp(Fxp v, Fxp lo, Fxp hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

inline Vector3D ClampWorld(const Vector3D& v, const Config& cfg)
{
    if (cfg.worldClamp <= Fxp::BuildRaw(0)) return v;
    return Vector3D(
        ClampFxp(v.X, -cfg.worldClamp, cfg.worldClamp),
        ClampFxp(v.Y, -cfg.worldClamp, cfg.worldClamp),
        ClampFxp(v.Z, -cfg.worldClamp, cfg.worldClamp));
}

inline Vector3D BuildSafeLookTarget(const Vector3D& cameraPos,
                                    const Vector3D& desiredTarget,
                                    const Vector3D& fallbackForward,
                                    const Config& cfg)
{
    Vector3D target = desiredTarget;
    Vector3D delta = target - cameraPos;
    const Fxp d2 = delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z;
    const Fxp minD2 = cfg.minTargetDistance * cfg.minTargetDistance;

    if (d2 <= minD2)
    {
        Vector3D safeForward = fallbackForward;
        const Fxp f2 = safeForward.X * safeForward.X + safeForward.Y * safeForward.Y + safeForward.Z * safeForward.Z;
        if (f2 <= Fxp::BuildRaw(1 << 8))
        {
            safeForward = Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(1 << 16));
        }
        target = cameraPos + safeForward;
    }

    // Keep a minimal horizontal spread to avoid near-vertical look vectors.
    delta = target - cameraPos;
    if (delta.X.Abs() + delta.Z.Abs() <= Fxp::BuildRaw(1 << 15))
    {
        target.Z += Fxp::BuildRaw(1 << 16);
    }

    return ClampWorld(target, cfg);
}

inline Vector3D ResolveBoomGuard(const Vector3D& desiredCameraPos,
                                 const Vector3D& targetPos,
                                 const Config& cfg)
{
    Vector3D out = desiredCameraPos;
    // Keep camera from going under target reference.
    const Fxp maxY = targetPos.Y - cfg.minCameraHeightAboveTarget;
    if (out.Y > maxY) out.Y = maxY;
    return ClampWorld(out, cfg);
}
} // namespace CameraSafety
