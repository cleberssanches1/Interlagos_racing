#pragma once

#include <array>

#include "sky_environment.hpp"
#include "camera_controller.hpp"

// Respons?vel por carregar e atualizar o background (VDP2)
struct BackgroundManager
{
    static constexpr size_t kMaxPaths = 24;
    SkyEnvironment env;
    bool loaded = false;
    std::array<std::array<char, 96>, kMaxPaths> cachedPathStorage{};
    std::array<const char*, kMaxPaths> cachedPaths{};
    size_t cachedPathCount = 0;
    uint32_t retryTicks = 0;

    void Configure()
    {
        env.useDome = false;
        env.useHorizon = true;
        env.Configure();
    }

    bool Init(const char* const* paths, size_t count)
    {
        Configure();
        cachedPathCount = (count < kMaxPaths) ? count : kMaxPaths;
        for (size_t i = 0; i < cachedPathCount; ++i)
        {
            cachedPathStorage[i].fill('\0');
            if (paths[i])
            {
                const char* src = paths[i];
                char* dst = cachedPathStorage[i].data();
                const size_t maxLen = cachedPathStorage[i].size() - 1;
                size_t j = 0;
                while (j < maxLen && src[j] != '\0')
                {
                    dst[j] = src[j];
                    ++j;
                }
                dst[j] = '\0';
            }
            cachedPaths[i] = cachedPathStorage[i].data();
        }
        loaded = env.Load(paths, count);
        // log removido
        retryTicks = 0;
        return loaded;
    }

    void Update(const Camera::State& camera)
    {
        if (!loaded && cachedPathCount > 0)
        {
            ++retryTicks;
            const auto hwr = SRL::Memory::HighWorkRam::GetReport();
            constexpr size_t kMinRetryHwrBytes = 64u * 1024u;
            if (hwr.FreeSize >= kMinRetryHwrBytes && (retryTicks % 30u) == 0u)
            {
                loaded = env.Load(cachedPaths.data(), cachedPathCount);
                // log removido
            }
        }
        if (!loaded) return;
        env.Update(camera.yawDeg, camera.viewYawDeg, camera.viewPitchDeg);
    }
};
