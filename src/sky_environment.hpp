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
        // Mantem movimento horizontal perceptivel mesmo sem giro de camera.
        horizon.driftStep = SRL::Math::Types::Fxp(0.03f);
    }

    bool Load(const char* const* paths, size_t count)
    {
        bool anyLoaded = false;
        if (useHorizon)
            anyLoaded = horizon.Load(paths, count) || anyLoaded;
        if (useDome)
            anyLoaded = dome.Load(paths, count) || anyLoaded;
        return anyLoaded;
    }

    void Update(int32_t yawDeg, int32_t viewYawDeg, int32_t viewPitchDeg)
    {
        if (useHorizon)
            horizon.Update(yawDeg + viewYawDeg, viewPitchDeg); // aplica yaw+pitch ao scroll (mais rapido no look)
        if (useDome)
            dome.Update(yawDeg, viewYawDeg, viewPitchDeg);
    }
};
