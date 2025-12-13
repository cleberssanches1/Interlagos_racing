#pragma once

#include "sky_environment.hpp"
#include "camera_controller.hpp"

// Respons?vel por carregar e atualizar o background (VDP2)
struct BackgroundManager
{
    SkyEnvironment env;
    bool loaded = false;

    void Configure()
    {
        env.useDome = false;
        env.useHorizon = true;
        env.Configure();
    }

    bool Init(const char* const* paths, size_t count)
    {
        Configure();
        env.Load(paths, count);
        loaded = true;
        return true;
    }

    void Update(const Camera::State& camera)
    {
        if (!loaded) return;
        env.Update(camera.yawDeg, camera.viewYawDeg, camera.viewPitchDeg);
    }
};
