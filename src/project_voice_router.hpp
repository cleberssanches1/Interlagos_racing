#pragma once

#include <cstdint>

#include "interfaces.hpp"

namespace Game
{

class ProjectVoiceRouter final : public IAudioVoiceRouter
{
public:
    struct VoiceLayout
    {
        static constexpr uint8_t kUiPrimary = 0;
        static constexpr uint8_t kUiSecondary = 1;
        static constexpr uint8_t kSystemPrimary = 2;
        static constexpr uint8_t kSystemSecondary = 3;
        static constexpr uint8_t kAmbiencePrimary = 4;
        static constexpr uint8_t kAmbienceSecondary = 5;
        static constexpr uint8_t kMusicPrimary = 6;
        static constexpr uint8_t kMusicSecondary = 7;
        static constexpr uint8_t kCarEngine = 8;
        static constexpr uint8_t kCarShiftUp = 12;
        static constexpr uint8_t kCarShiftDown = 13;
        static constexpr uint8_t kCarTireSkid = 14;
        static constexpr uint8_t kCarReserve0 = 15;
        static constexpr uint8_t kCarReserve1 = 16;
        static constexpr uint8_t kTrackReserve0 = 17;
        static constexpr uint8_t kTrackReserve1 = 18;
        static constexpr uint8_t kTrackReserve2 = 19;
        static constexpr uint8_t kTrackReserve3 = 20;
        static constexpr uint8_t kWorldReserve0 = 21;
        static constexpr uint8_t kWorldReserve1 = 22;
        static constexpr uint8_t kWorldReserve2 = 23;
        static constexpr uint8_t kWorldReserve3 = 24;
        static constexpr uint8_t kFreeReserve0 = 25;
        static constexpr uint8_t kFreeReserve1 = 26;
        static constexpr uint8_t kFreeReserve2 = 27;
        static constexpr uint8_t kFreeReserve3 = 28;
        static constexpr uint8_t kFreeReserve4 = 29;
        static constexpr uint8_t kFreeReserve5 = 30;
        static constexpr uint8_t kFreeReserve6 = 31;
    };

    uint8_t ResolveVoice(const AudioVoiceGroup group) const override
    {
        switch (group)
        {
        case AudioVoiceGroup::CarEngine:
            return VoiceLayout::kCarEngine;
        case AudioVoiceGroup::CarShiftUp:
            return VoiceLayout::kCarShiftUp;
        case AudioVoiceGroup::CarShiftDown:
            return VoiceLayout::kCarShiftDown;
        case AudioVoiceGroup::CarTireSkid:
            return VoiceLayout::kCarTireSkid;
        case AudioVoiceGroup::UiPrimary:
            return VoiceLayout::kUiPrimary;
        case AudioVoiceGroup::UiSecondary:
            return VoiceLayout::kUiSecondary;
        case AudioVoiceGroup::AmbiencePrimary:
            return VoiceLayout::kAmbiencePrimary;
        case AudioVoiceGroup::AmbienceSecondary:
            return VoiceLayout::kAmbienceSecondary;
        case AudioVoiceGroup::MusicPrimary:
            return VoiceLayout::kMusicPrimary;
        case AudioVoiceGroup::MusicSecondary:
            return VoiceLayout::kMusicSecondary;
        case AudioVoiceGroup::SystemPrimary:
            return VoiceLayout::kSystemPrimary;
        case AudioVoiceGroup::SystemSecondary:
            return VoiceLayout::kSystemSecondary;
        default:
            return VoiceLayout::kSystemPrimary;
        }
    }
};

} // namespace Game
