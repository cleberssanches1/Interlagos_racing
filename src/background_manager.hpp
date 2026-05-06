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
    uint32_t statusTicks = 0;
    uint16_t retryAttempts = 0;

    void Configure()
    {
        env.useDome = false;
        env.usePanorama = true;
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
        loaded = env.Load(cachedPaths.data(), cachedPathCount);
        retryTicks = 0;
        statusTicks = 0;
        retryAttempts = 0;
        return loaded;
    }

    void Update(const Camera::State& camera, int32_t carYawDeg)
    {
        // Reafirma back screen em cada frame para detectar conflitos de estado VDP2.
        SRL::VDP2::SetBackColor(SRL::Types::HighColor::FromRGB555(0, 31, 31));
        ++statusTicks;
        if (!loaded && cachedPathCount > 0)
        {
            ++retryTicks;
            const auto hwr = SRL::Memory::HighWorkRam::GetReport();
            constexpr size_t kMinRetryHwrBytes = 64u * 1024u;
            constexpr uint32_t kRetryCadenceFrames = 180u;
            constexpr uint16_t kMaxRetryAttempts = 20u;
            if (retryAttempts < kMaxRetryAttempts &&
                hwr.FreeSize >= kMinRetryHwrBytes &&
                (retryTicks % kRetryCadenceFrames) == 0u)
            {
                loaded = env.Load(cachedPaths.data(), cachedPathCount);
                ++retryAttempts;
            }
        }

        if (!loaded) return;
        env.Update(carYawDeg, camera.viewYawDeg, camera.viewPitchDeg);
    }
};
