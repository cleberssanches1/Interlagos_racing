#pragma once

#include <srl.hpp>
#include "sky_background.hpp"
#include "sky_background_dome.hpp"

// Gerencia m?ltiplas camadas VDP2 para liberar VDP1: horizonte (NBG0) + domo (RBG0)
struct SkyEnvironment
{
    bool useDome = false; // dome off
    bool useHorizon = true; // NBG0 on

    SkyBackground horizon;     // NBG0
    SkyBackgroundDome dome;    // RBG0 (desligado)

    // Configura fatores padr?es
    void Configure()
    {
        // NBG0 scroll control
        horizon.yawFactor = SRL::Math::Types::Fxp(0.5f);
        horizon.driftStep = SRL::Math::Types::Fxp(0.0015f); // +50% de velocidade, ainda suave
    }

    void Load(const char* const* paths, size_t count)
    {
        if (useHorizon)
            horizon.Load(paths, count);
        if (useDome)
            dome.Load(paths, count);
    }

    void Update(int32_t yawDeg, int32_t viewYawDeg, int32_t viewPitchDeg)
    {
        if (useHorizon)
            horizon.Update(yawDeg + viewYawDeg, viewPitchDeg); // aplica yaw+pitch ao scroll (mais rápido no look)
        if (useDome)
            dome.Update(yawDeg, viewYawDeg, viewPitchDeg);
    }
};
