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
    Fxp moveStep{Fxp::BuildRaw(0x00008000)};    // 0.5
    Fxp strafeStep{Fxp::BuildRaw(0x00008000)};  // 0.5
    int32_t yawMinDeg{0};
    int32_t yawMaxDeg{360};
    int32_t pitchMinDeg{-80};
    int32_t pitchMaxDeg{80};
    int32_t viewYawMinDeg{-90};
    int32_t viewYawMaxDeg{90};
    int32_t viewPitchMinDeg{-80};
    int32_t viewPitchMaxDeg{80};
    Fxp targetDistance{Fxp::BuildRaw(0x00780000)}; // 120
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
    state.yaw = Angle::FromDegrees(Fxp::BuildRaw(state.yawDeg << 16));
    state.pitch = Angle::FromDegrees(Fxp::BuildRaw(state.pitchDeg << 16));
    state.viewYaw = Angle::FromDegrees(Fxp::BuildRaw(state.viewYawDeg << 16));
    state.viewPitch = Angle::FromDegrees(Fxp::BuildRaw(state.viewPitchDeg << 16));
}

inline void UpdateInput(State& state, const Tuning& tuning, Digital& pad)
{
    if (tuning.yawMinDeg > tuning.yawMaxDeg || tuning.pitchMinDeg > tuning.pitchMaxDeg ||
        tuning.viewYawMinDeg > tuning.viewYawMaxDeg || tuning.viewPitchMinDeg > tuning.viewPitchMaxDeg)
    {
        return;
    }

    const int32_t yawStep = tuning.yawStepDeg != 0 ? tuning.yawStepDeg : 1;
    const int32_t pitchStep = tuning.pitchStepDeg != 0 ? tuning.pitchStepDeg : 1;
    bool zHeld = pad.IsHeld(Digital::Button::Z);
    bool yHeld = pad.IsHeld(Digital::Button::Y);
    bool xHeld = pad.IsHeld(Digital::Button::X);

    // Salva posio inicial e restaura quando X no estiver pressionado
    static bool homeSet = false;
    static int32_t homeYawDeg = 0, homePitchDeg = 0, homeViewYawDeg = 0, homeViewPitchDeg = 0;
    static Vector3D homeStrafe;
    static bool wasXHeld = false;

    if (!homeSet)
    {
        homeYawDeg = state.yawDeg;
        homePitchDeg = state.pitchDeg;
        homeViewYawDeg = state.viewYawDeg;
        homeViewPitchDeg = state.viewPitchDeg;
        homeStrafe = state.strafe;
        homeSet = true;
    }

    if (xHeld)
    {
        // Orbita ao redor do modelo
        if (pad.IsHeld(Digital::Button::Left))  state.yawDeg -= yawStep;
        if (pad.IsHeld(Digital::Button::Right)) state.yawDeg += yawStep;
        if (state.yawDeg >= 360 || state.yawDeg < 0)
        {
            state.yawDeg %= 360;
            if (state.yawDeg < 0) state.yawDeg += 360;
        }
    }
    // Se X foi solto neste frame, restaurar posio inicial uma nica vez
    if (wasXHeld && !xHeld)
    {
        state.yawDeg = homeYawDeg;
        state.pitchDeg = homePitchDeg;
        state.viewYawDeg = homeViewYawDeg;
        state.viewPitchDeg = homeViewPitchDeg;
        state.strafe = homeStrafe;
    }

    wasXHeld = xHeld;

    if (yHeld)
    {
        Vector3D moveDelta = Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(0));
        // Frente/tras invertidos conforme pedido anterior
        if (pad.IsHeld(Digital::Button::Up))    moveDelta += Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0),  tuning.moveStep);
        if (pad.IsHeld(Digital::Button::Down))  moveDelta += Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), -tuning.moveStep);
        // Laterais ajustadas
        if (pad.IsHeld(Digital::Button::Left))  moveDelta += Vector3D( tuning.strafeStep, Fxp::BuildRaw(0), Fxp::BuildRaw(0));
        if (pad.IsHeld(Digital::Button::Right)) moveDelta += Vector3D(-tuning.strafeStep, Fxp::BuildRaw(0), Fxp::BuildRaw(0));
        // Y+R e Y+L movem a camara no eixo vertical
        const Fxp verticalLimit = Fxp::BuildRaw(19 << 16); // leave margin before hitting kStrafeLimit
        const Fxp currentY = state.strafe.Y;
        if (pad.IsHeld(Digital::Button::R) && currentY < verticalLimit)
            moveDelta += Vector3D(Fxp::BuildRaw(0),  tuning.strafeStep, Fxp::BuildRaw(0));
        if (pad.IsHeld(Digital::Button::L) && currentY > -verticalLimit)
            moveDelta += Vector3D(Fxp::BuildRaw(0), -tuning.strafeStep, Fxp::BuildRaw(0));
        state.strafe += moveDelta;
    }

    constexpr Fxp kStrafeLimit = Fxp::BuildRaw(20 << 16);
    auto clamp = [&](Fxp& value)
    {
        if (value > kStrafeLimit) value = kStrafeLimit;
        if (value < -kStrafeLimit) value = -kStrafeLimit;
    };
    clamp(state.strafe.X);
    clamp(state.strafe.Y);
    clamp(state.strafe.Z);

    Clamp(state.yawDeg, tuning.yawMinDeg, tuning.yawMaxDeg);
    Clamp(state.pitchDeg, tuning.pitchMinDeg, tuning.pitchMaxDeg);

    // Yaw de olhar: sempre livre 360, apenas normaliza
    if (state.viewYawDeg >= 360 || state.viewYawDeg < 0)
    {
        state.viewYawDeg %= 360;
        if (state.viewYawDeg < 0) state.viewYawDeg += 360;
    }
    // Pitch de olhar continua limitado para evitar virar de ponta-cabea
    Clamp(state.viewPitchDeg, tuning.viewPitchMinDeg, tuning.viewPitchMaxDeg);

    RefreshAngles(state);
    state.location = OrbitPosition(state.yaw, state.pitch, state.radius) + state.strafe;
}

inline Vector3D ComputeLookTarget(const State& state,
                                  const Tuning& tuning,
                                  Digital& pad,
                                  const Vector3D& modelTarget = Vector3D(Fxp::BuildRaw(0), Fxp::BuildRaw(0), Fxp::BuildRaw(0)))
{
    if (pad.IsHeld(Digital::Button::X))
    {
        return modelTarget; // orbita olhando para o centro do modelo
    }
    return state.strafe + OrbitPosition(state.viewYaw, state.viewPitch, tuning.targetDistance);
}
} // namespace Camera
