#pragma once

#include <srl.hpp>
#include "sky_background_dome.hpp"
#include "sky_panorama.hpp"

// Gerencia camadas de ceu em VDP2: panorama em NBG0 + domo opcional em RBG0.
struct SkyEnvironment
{
    bool useDome = false; // dome off
    bool usePanorama = true; // NBG0 panorama on

    SkyPanorama panorama;      // NBG0
    SkyBackgroundDome dome;    // RBG0 (desligado)

    // Configura fatores padr?es
    void Configure()
    {
        panorama.scrollY = 0;
        panorama.scaleX = SRL::Math::Types::Fxp(1.0f);
        panorama.scaleY = SRL::Math::Types::Fxp(1.0f);
        panorama.ResetYawAnchor();
    }

    bool Load(const char* const* paths, size_t count)
    {
        bool anyLoaded = false;
        if (usePanorama)
            anyLoaded = panorama.Load(paths, count) || anyLoaded;
        if (useDome)
            anyLoaded = dome.Load(paths, count) || anyLoaded;
        return anyLoaded;
    }

    void Update(int32_t yawDeg, int32_t viewYawDeg, int32_t viewPitchDeg)
    {
        if (usePanorama)
            panorama.Update(yawDeg, viewYawDeg, viewPitchDeg);
        if (useDome)
            dome.Update(yawDeg, viewYawDeg, viewPitchDeg);
    }
};
