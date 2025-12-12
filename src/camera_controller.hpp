#pragma once

#include <srl.hpp>

namespace Camera
{
using SRL::Input::Digital;
using SRL::Math::Types::Angle;
using SRL::Math::Types::Vector3D;
using SRL::Math::Types::Fxp;

struct State
{
    int32_t yawDeg;
    int32_t pitchDeg;
    int32_t viewYawDeg;
    int32_t viewPitchDeg;
    Fxp radius;
    Vector3D strafe;
    Vector3D location;
    Angle yaw;
    Angle pitch;
    Angle viewYaw;
    Angle viewPitch;
};

struct Tuning
{
    int32_t yawStepDeg{2};
    int32_t pitchStepDeg{2};
    Fxp moveStep{Fxp(0.5f)};
    Fxp strafeStep{Fxp(0.5f)};
    int32_t yawMinDeg{90};
    int32_t yawMaxDeg{270};
    int32_t pitchMinDeg{-80};
    int32_t pitchMaxDeg{80};
    int32_t viewYawMinDeg{-90};
    int32_t viewYawMaxDeg{90};
    int32_t viewPitchMinDeg{-80};
    int32_t viewPitchMaxDeg{80};
    Fxp targetDistance{Fxp(120.0f)};
};

inline void Clamp(int32_t& v, int32_t min, int32_t max)
{
    if (v < min) v = min;
    if (v > max) v = max;
}

inline Vector3D OrbitPosition(Angle yaw, Angle pitch, Fxp radius)
{
    Fxp sinYaw = SRL::Math::Trigonometry::Sin(yaw);
    Fxp cosYaw = SRL::Math::Trigonometry::Cos(yaw);
    Fxp sinPitch = SRL::Math::Trigonometry::Sin(pitch);
    Fxp cosPitch = SRL::Math::Trigonometry::Cos(pitch);
    return Vector3D(radius * sinYaw * cosPitch,
                    radius * sinPitch,
                    radius * cosYaw * cosPitch);
}

inline void RefreshAngles(State& state)
{
    state.yaw = Angle::FromDegrees(Fxp::Convert(state.yawDeg));
    state.pitch = Angle::FromDegrees(Fxp::Convert(state.pitchDeg));
    state.viewYaw = Angle::FromDegrees(Fxp::Convert(state.viewYawDeg));
    state.viewPitch = Angle::FromDegrees(Fxp::Convert(state.viewPitchDeg));
}

inline void UpdateInput(State& state, const Tuning& tuning, Digital& pad)
{
    bool zHeld = pad.IsHeld(Digital::Button::Z);
    bool yHeld = pad.IsHeld(Digital::Button::Y);
    bool xHeld = pad.IsHeld(Digital::Button::X);

    if (zHeld)
    {
        if (pad.IsHeld(Digital::Button::Up))    state.viewPitchDeg -= tuning.pitchStepDeg;
        if (pad.IsHeld(Digital::Button::Down))  state.viewPitchDeg += tuning.pitchStepDeg;
        if (pad.IsHeld(Digital::Button::Left))  state.viewYawDeg   -= tuning.yawStepDeg;
        if (pad.IsHeld(Digital::Button::Right)) state.viewYawDeg   += tuning.yawStepDeg;
    }

    if (xHeld)
    {
        // Orbita ao redor do modelo
        if (pad.IsHeld(Digital::Button::Left))  state.yawDeg -= tuning.yawStepDeg;
        if (pad.IsHeld(Digital::Button::Right)) state.yawDeg += tuning.yawStepDeg;
    }

    if (yHeld)
    {
        Vector3D moveDelta = Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0));
        // Frente/tras invertidos conforme pedido anterior
        if (pad.IsHeld(Digital::Button::Up))    moveDelta += Vector3D(Fxp::Convert(0), Fxp::Convert(0),  tuning.moveStep);
        if (pad.IsHeld(Digital::Button::Down))  moveDelta += Vector3D(Fxp::Convert(0), Fxp::Convert(0), -tuning.moveStep);
        // Laterais ajustadas
        if (pad.IsHeld(Digital::Button::Left))  moveDelta += Vector3D( tuning.strafeStep, Fxp::Convert(0), Fxp::Convert(0));
        if (pad.IsHeld(Digital::Button::Right)) moveDelta += Vector3D(-tuning.strafeStep, Fxp::Convert(0), Fxp::Convert(0));
        state.strafe += moveDelta;
    }

    Clamp(state.yawDeg, tuning.yawMinDeg, tuning.yawMaxDeg);
    Clamp(state.pitchDeg, tuning.pitchMinDeg, tuning.pitchMaxDeg);
    Clamp(state.viewYawDeg, tuning.viewYawMinDeg, tuning.viewYawMaxDeg);
    Clamp(state.viewPitchDeg, tuning.viewPitchMinDeg, tuning.viewPitchMaxDeg);

    RefreshAngles(state);
    state.location = OrbitPosition(state.yaw, state.pitch, state.radius) + state.strafe;
}

inline Vector3D ComputeLookTarget(const State& state,
                                  const Tuning& tuning,
                                  Digital& pad,
                                  const Vector3D& modelTarget = Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0)))
{
    if (pad.IsHeld(Digital::Button::X))
    {
        return modelTarget; // orbita olhando para o centro do modelo
    }
    return state.strafe + OrbitPosition(state.viewYaw, state.viewPitch, tuning.targetDistance);
}
} // namespace Camera
