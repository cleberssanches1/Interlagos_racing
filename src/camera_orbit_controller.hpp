#pragma once

#include <srl.hpp>

// Orbit controller handles only orbit input state and offset generation.
class CameraOrbitController
{
public:
    using Fxp = SRL::Math::Types::Fxp;
    using Angle = SRL::Math::Types::Angle;
    using Vector3D = SRL::Math::Types::Vector3D;

    struct Config
    {
        int32_t yawStepDeg = 4;
    };

    // Update controller state from pad and current camera offset.
    void Update(SRL::Input::Digital& pad,
                const Config& cfg,
                const Vector3D& currentCameraOffset,
                bool allowOrbitInput = true)
    {
        if (!allowOrbitInput)
        {
            // Force-disable orbit when another system owns X+arrows (camera 2 calibration).
            active_ = false;
            orbitDeltaYawDeg_ = 0;
            xHeldPrev_ = false;
            return;
        }

        const bool xHeld = pad.IsHeld(SRL::Input::Digital::Button::X);
        const bool leftHeld = pad.IsHeld(SRL::Input::Digital::Button::Left);
        const bool rightHeld = pad.IsHeld(SRL::Input::Digital::Button::Right);

        if (xHeld && !xHeldPrev_)
        {
            orbitBaseOffset_ = currentCameraOffset;
            orbitDeltaYawDeg_ = 0;
            active_ = true;
        }

        if (xHeld && active_)
        {
            if (leftHeld) orbitDeltaYawDeg_ -= cfg.yawStepDeg;
            if (rightHeld) orbitDeltaYawDeg_ += cfg.yawStepDeg;
            orbitDeltaYawDeg_ = NormalizeDeg(orbitDeltaYawDeg_);
        }

        if (!xHeld && xHeldPrev_)
        {
            active_ = false;
            orbitDeltaYawDeg_ = 0;
        }

        xHeldPrev_ = xHeld;
    }

    bool Active() const { return active_; }

    // Return camera offset relative to target position.
    Vector3D ResolveOffset(const Vector3D& defaultOffset) const
    {
        if (!active_) return defaultOffset;
        return RotateY(orbitBaseOffset_, orbitDeltaYawDeg_);
    }

private:
    static int32_t NormalizeDeg(int32_t deg)
    {
        return ((deg % 360) + 360) % 360;
    }

    static Vector3D RotateY(const Vector3D& v, int32_t yawDeg)
    {
        const int32_t n = NormalizeDeg(yawDeg);
        const Angle a = Angle::FromDegrees(Fxp::BuildRaw(n << 16));
        const Fxp s = SRL::Math::Trigonometry::Sin(a);
        const Fxp c = SRL::Math::Trigonometry::Cos(a);
        return Vector3D(v.X * c + v.Z * s, v.Y, (-v.X) * s + v.Z * c);
    }

    bool xHeldPrev_ = false;
    bool active_ = false;
    int32_t orbitDeltaYawDeg_ = 0;
    Vector3D orbitBaseOffset_{};
};
