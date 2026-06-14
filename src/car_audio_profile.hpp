#pragma once

#include <cstdint>

#ifndef AUDIO_PROFILE
#define AUDIO_PROFILE 0
#endif

namespace Game
{

enum class CarAudioProfile : uint8_t
{
    Hq = 0,
    Lq = 1,
    LqRaw = 2
};

constexpr CarAudioProfile GetActiveCarAudioProfile()
{
#if AUDIO_PROFILE == 1
    return CarAudioProfile::Lq;
#elif AUDIO_PROFILE == 2
    return CarAudioProfile::LqRaw;
#else
    return CarAudioProfile::Hq;
#endif
}

constexpr bool IsLowQualityCarAudioProfile(CarAudioProfile profile)
{
    return profile == CarAudioProfile::Lq || profile == CarAudioProfile::LqRaw;
}

constexpr const char* CarAudioProfileName(CarAudioProfile profile)
{
    switch (profile)
    {
    case CarAudioProfile::Lq:
        return "LQ";
    case CarAudioProfile::LqRaw:
        return "LQ_RAW";
    default:
        return "HQ";
    }
}

} // namespace Game
