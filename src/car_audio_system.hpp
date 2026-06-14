#pragma once

#include <cstdint>

#include "car_audio_profile.hpp"
#include "interfaces.hpp"

namespace Game
{

class CarAudioSystem final : public IAudioEvents
{
public:
    static constexpr uint8_t kVoiceEngine    = 8;
    static constexpr uint8_t kVoiceShiftUp   = 12;
    static constexpr uint8_t kVoiceShiftDown = 13;
    static constexpr uint8_t kVoiceTire      = 14;

    static constexpr uint16_t kEngineIdleRpm      = 4200u;
    static constexpr uint16_t kEngineMaxRpm       = 12500u;
    static constexpr uint16_t kEngineAcousticMaxRpm = 15000u;
    static constexpr uint16_t kRpmRisePerFrame    = 220u;
    static constexpr uint16_t kRpmDropPerFrame    = 280u;
    static constexpr int16_t  kStoppedSpeedKmh    = 2;
    static constexpr int16_t  kSkidSpeedThreshold = 30;
    static constexpr uint8_t  kEngineVolume       = 110u;
    static constexpr uint8_t  kEngineShiftDuckVolume = 110u;
    static constexpr uint8_t  kEngineShiftDuckFrames = 2u;
    static constexpr uint8_t  kShiftUpVolume      = 118u;
    static constexpr uint8_t  kShiftDownVolume    = 124u;
    static constexpr int8_t   kShiftUpPan         = 4;
    static constexpr int8_t   kShiftDownPan       = -2;
    static constexpr uint8_t  kShiftVoiceHoldFrames = 20u;
    static constexpr uint8_t  kShiftRetriggerCooldownFrames = 6u;
    static constexpr uint8_t  kSkidVolume         = 90u;
    static constexpr uint32_t kEngineBaseHz       = 22050u;
    static constexpr uint32_t kEngineMaxHz        = kEngineBaseHz * 3u;

    struct Snapshot
    {
        uint16_t currentRpm      = 0;
        uint16_t enginePitchWord = 0;
        uint8_t  activeSampleIdx = 0;
        uint8_t  idleVolume      = 0;
        uint8_t  revVolume       = 0;
        bool     enginePlaying   = false;
        bool     skidActive      = false;
    };

    void Initialize();
    void OnFrame(const GameplayFrameState& frameState) override;
    void SetVoiceRouter(const IAudioVoiceRouter& router) { voiceRouter_ = &router; }
    static const IAudioVoiceRouter& DefaultVoiceRouter();

    const Snapshot& LastSnapshot() const { return snapshot_; }

private:
    class FixedVoiceRouter final : public IAudioVoiceRouter
    {
    public:
        uint8_t ResolveVoice(const AudioVoiceGroup group) const override
        {
            switch (group)
            {
            case AudioVoiceGroup::CarEngine:
                return kVoiceEngine;
            case AudioVoiceGroup::CarShiftUp:
                return kVoiceShiftUp;
            case AudioVoiceGroup::CarShiftDown:
                return kVoiceShiftDown;
            case AudioVoiceGroup::CarTireSkid:
                return kVoiceTire;
            default:
                return kVoiceEngine;
            }
        }
    };

    enum class AudioCue : uint8_t
    {
        Engine = 0,
        ShiftUp,
        ShiftDown,
        TireSkid
    };

    SRL::Sound::Pcm::WaveSound* engineSample_ = nullptr;
    SRL::Sound::Pcm::WaveSound* shiftUpSample_ = nullptr;
    SRL::Sound::Pcm::WaveSound* shiftDownSample_ = nullptr;
    SRL::Sound::Pcm::WaveSound* tireSample_ = nullptr;

    struct State
    {
        uint16_t audioRpmCurrent = kEngineIdleRpm;
        uint16_t audioRpmTarget = kEngineIdleRpm;
        int16_t lastGear = 0;
        bool engineActive = false;
        bool skidActive = false;
        bool gearInitialized = false;
        uint32_t lastShiftSoundFrameId = 0u;
        uint8_t lastShiftFrames = 0u;
        uint8_t shiftSoundCooldownFrames = 0u;
        uint8_t shiftUpVoiceFrames = 0u;
        uint8_t shiftDownVoiceFrames = 0u;
        uint8_t engineShiftDuckFrames = 0u;
        uint8_t forcedShiftFrames = 0u;
        uint16_t forcedShiftRpm = 0u;
    };

    State state_{};
    Snapshot snapshot_{};
    const IAudioVoiceRouter* voiceRouter_ = nullptr;

    static uint16_t GetPhysicsRpm(const GameplayFrameState& fs);
    static uint16_t SmoothRpm(uint16_t current, uint16_t target);
    static uint16_t ComputeEnginePitchWord(uint16_t rpm);
    static const char* ResolveAssetName(AudioCue cue, CarAudioProfile profile);
    static SRL::Sound::Pcm::WaveSound* TryLoadWaveCue(AudioCue cue);
    static SRL::Sound::Pcm::WaveSound* TryLoadWave(const char* filename);
    static bool IsVoiceFree(uint8_t voice);
    static void StopVoice(uint8_t voice);
    static void SetVoiceVolumePan(uint8_t voice, uint8_t volume, int8_t pan);
    static void SetVoicePitch(uint8_t voice, uint16_t pitchWord);

    uint8_t ResolveVoice(AudioVoiceGroup group) const;
    void TickVoiceLifetime();
    void EnsureEngineVoiceStarted();
    void UpdateEngineSnapshot(uint16_t pitchWord);
    void TriggerShiftVoice(SRL::Sound::Pcm::WaveSound* sample,
                           uint8_t voice,
                           uint8_t volume,
                           int8_t pan,
                           uint8_t& ioVoiceFrames,
                           const GameplayFrameState& fs);
    void TickEngine(const GameplayFrameState& fs);
    void TickGearShift(const GameplayFrameState& fs);
    void TickTire(const GameplayFrameState& fs);
};

inline void CarAudioSystem::Initialize()
{
    if (!voiceRouter_)
    {
        voiceRouter_ = &DefaultVoiceRouter();
    }

    const bool hwrHasRoom =
        SRL::Memory::HighWorkRam::GetLargestFreeBlockSize() >= 192u * 1024u;
    const bool cartAvailable =
        SRL::Memory::CartRam::GetReport().TotalSize > 0u;
    SRL::Sound::Pcm::SetMemAllocationBehaviour(
        SRL::Sound::Pcm::PcmMalloc::LwRam,
        hwrHasRoom
            ? SRL::Sound::Pcm::PcmMalloc::HwRam
            : (cartAvailable
                ? SRL::Sound::Pcm::PcmMalloc::CartRam
                : SRL::Sound::Pcm::PcmMalloc::HwRam));

    engineSample_ = TryLoadWaveCue(AudioCue::Engine);
    shiftUpSample_ = TryLoadWaveCue(AudioCue::ShiftUp);
    shiftDownSample_ = TryLoadWaveCue(AudioCue::ShiftDown);
    tireSample_ = TryLoadWaveCue(AudioCue::TireSkid);

    state_ = {};
    state_.audioRpmCurrent = kEngineIdleRpm;
    state_.audioRpmTarget = kEngineIdleRpm;
    state_.lastGear = 0;
    snapshot_ = {};

    if (engineSample_)
    {
        state_.engineActive = engineSample_->PlayOnVoice(ResolveVoice(AudioVoiceGroup::CarEngine),
                                                         kEngineVolume,
                                                         0);
        if (state_.engineActive)
        {
            SetVoicePitch(ResolveVoice(AudioVoiceGroup::CarEngine),
                          ComputeEnginePitchWord(kEngineIdleRpm));
        }
    }
}

inline void CarAudioSystem::OnFrame(const GameplayFrameState& frameState)
{
    TickVoiceLifetime();

    // Order matters: shift state may momentarily override engine RPM,
    // then tire skid is evaluated from the final frame snapshot.
    TickGearShift(frameState);
    TickEngine(frameState);
    TickTire(frameState);
}

inline void CarAudioSystem::TickEngine(const GameplayFrameState& fs)
{
    // Engine loop synthesis from authoritative drivetrain RPM.
    state_.audioRpmTarget = GetPhysicsRpm(fs);
    if ((state_.forcedShiftFrames > 0u) &&
        (state_.forcedShiftRpm >= kEngineIdleRpm))
    {
        state_.audioRpmTarget = state_.forcedShiftRpm;
        state_.audioRpmCurrent = state_.forcedShiftRpm;
        --state_.forcedShiftFrames;
    }
    else
    {
        state_.audioRpmCurrent = SmoothRpm(state_.audioRpmCurrent, state_.audioRpmTarget);
    }

    if (IsVoiceFree(ResolveVoice(AudioVoiceGroup::CarEngine)))
    {
        state_.engineActive = false;
    }

    EnsureEngineVoiceStarted();

    if (state_.engineActive)
    {
        const uint8_t engineVolume =
            (state_.engineShiftDuckFrames > 0u) ? kEngineShiftDuckVolume : kEngineVolume;
        const uint16_t pitchWord = ComputeEnginePitchWord(state_.audioRpmCurrent);
        SetVoiceVolumePan(ResolveVoice(AudioVoiceGroup::CarEngine), engineVolume, 0);
        SetVoicePitch(ResolveVoice(AudioVoiceGroup::CarEngine), pitchWord);
        UpdateEngineSnapshot(pitchWord);
    }
    else
    {
        UpdateEngineSnapshot(ComputeEnginePitchWord(state_.audioRpmCurrent));
    }
    if (state_.engineShiftDuckFrames > 0u)
    {
        --state_.engineShiftDuckFrames;
    }
}

inline void CarAudioSystem::TickGearShift(const GameplayFrameState& fs)
{
    // Shift SFX are edge-triggered from drivetrain telemetry.
    const int16_t gear = fs.carGear;
    const bool shiftStartedThisFrame =
        (fs.carShiftFrames > 0u) &&
        (state_.lastShiftFrames == 0u);
    const bool stationaryShift =
        (fs.carSpeedKmh <= kStoppedSpeedKmh) &&
        (fs.throttle == 0) &&
        !fs.braking;

    if (!state_.gearInitialized)
    {
        state_.lastGear = gear;
        state_.gearInitialized = true;
        state_.lastShiftFrames = fs.carShiftFrames;
        return;
    }

    if (!shiftStartedThisFrame && (gear == state_.lastGear))
    {
        state_.lastShiftFrames = fs.carShiftFrames;
        return;
    }

    const bool isStartupWindow = fs.frameId < 30u;
    const bool gearChanged = (gear != state_.lastGear);
    const bool playShiftSound =
        !isStartupWindow &&
        (shiftStartedThisFrame || gearChanged) &&
        (fs.frameId != state_.lastShiftSoundFrameId) &&
        (state_.shiftSoundCooldownFrames == 0u);

    const bool upshiftDrop =
        (fs.carShiftFrames > 0u) &&
        (fs.carShiftRpmBefore > 0) &&
        (fs.carShiftRpmAfter >= kEngineIdleRpm) &&
        (fs.carShiftRpmAfter < fs.carShiftRpmBefore);

    if (upshiftDrop && shiftStartedThisFrame && !stationaryShift)
    {
        state_.forcedShiftRpm = static_cast<uint16_t>(fs.carShiftRpmAfter);
        state_.forcedShiftFrames = 1u;
        state_.engineShiftDuckFrames = kEngineShiftDuckFrames;
    }

    if (playShiftSound && (gear > state_.lastGear))
    {
        TriggerShiftVoice(shiftUpSample_,
                          ResolveVoice(AudioVoiceGroup::CarShiftUp),
                          kShiftUpVolume,
                          kShiftUpPan,
                          state_.shiftUpVoiceFrames,
                          fs);
    }
    else if (playShiftSound)
    {
        TriggerShiftVoice(shiftDownSample_,
                          ResolveVoice(AudioVoiceGroup::CarShiftDown),
                          kShiftDownVolume,
                          kShiftDownPan,
                          state_.shiftDownVoiceFrames,
                          fs);
    }

    state_.lastGear = gear;
    state_.lastShiftFrames = fs.carShiftFrames;
}

inline void CarAudioSystem::TickTire(const GameplayFrameState& fs)
{
    // Tire skid runs as an independent one-voice layer.
    const bool shouldSkid =
        (fs.carSpeedKmh > kSkidSpeedThreshold) &&
        (fs.wheelsSpinning || fs.braking);

    if (shouldSkid)
    {
        if (IsVoiceFree(ResolveVoice(AudioVoiceGroup::CarTireSkid)))
        {
            if (tireSample_)
            {
                tireSample_->PlayOnVoice(ResolveVoice(AudioVoiceGroup::CarTireSkid), kSkidVolume, 0);
            }
        }
        else
        {
            SetVoiceVolumePan(ResolveVoice(AudioVoiceGroup::CarTireSkid), kSkidVolume, 0);
        }
    }
    else if (!IsVoiceFree(ResolveVoice(AudioVoiceGroup::CarTireSkid)))
    {
        StopVoice(ResolveVoice(AudioVoiceGroup::CarTireSkid));
    }

    state_.skidActive = shouldSkid;
    snapshot_.skidActive = shouldSkid;
}

inline uint16_t CarAudioSystem::GetPhysicsRpm(const GameplayFrameState& fs)
{
    int32_t rpm = (fs.carEngineRpm > 0) ? fs.carEngineRpm : 0;
    if (rpm <= 0)
    {
        rpm = kEngineIdleRpm;
        if (fs.throttle > 0)
        {
            rpm += (static_cast<int32_t>(fs.throttle) * (kEngineMaxRpm - kEngineIdleRpm)) / 100;
        }
    }

    if (rpm < static_cast<int32_t>(kEngineIdleRpm)) rpm = kEngineIdleRpm;
    if (rpm > static_cast<int32_t>(kEngineMaxRpm)) rpm = kEngineMaxRpm;
    return static_cast<uint16_t>(rpm);
}

inline uint16_t CarAudioSystem::SmoothRpm(const uint16_t current, const uint16_t target)
{
    if (target > current)
    {
        const uint16_t delta = static_cast<uint16_t>(target - current);
        return static_cast<uint16_t>(current + ((delta > kRpmRisePerFrame) ? kRpmRisePerFrame : delta));
    }

    const uint16_t delta = static_cast<uint16_t>(current - target);
    return static_cast<uint16_t>(current - ((delta > kRpmDropPerFrame) ? kRpmDropPerFrame : delta));
}

inline uint16_t CarAudioSystem::ComputeEnginePitchWord(const uint16_t rpm)
{
    const uint32_t targetHz =
        (rpm <= kEngineIdleRpm)
            ? kEngineBaseHz
            : (kEngineBaseHz +
               ((kEngineMaxHz - kEngineBaseHz) *
                static_cast<uint32_t>(rpm - kEngineIdleRpm)) /
                   static_cast<uint32_t>(kEngineAcousticMaxRpm - kEngineIdleRpm));
    return SRL::Sound::Pcm::ComputePitchWord(targetHz);
}

inline const IAudioVoiceRouter& CarAudioSystem::DefaultVoiceRouter()
{
    static FixedVoiceRouter kRouter{};
    return kRouter;
}

inline bool CarAudioSystem::IsVoiceFree(const uint8_t voice)
{
    return SRL::Sound::Pcm::IsVoiceFree(voice);
}

inline void CarAudioSystem::StopVoice(const uint8_t voice)
{
    SRL::Sound::Pcm::StopVoice(voice);
}

inline void CarAudioSystem::SetVoiceVolumePan(const uint8_t voice,
                                              const uint8_t volume,
                                              const int8_t pan)
{
    SRL::Sound::Pcm::SetVoiceVolumePan(voice, volume, pan);
}

inline void CarAudioSystem::SetVoicePitch(const uint8_t voice, const uint16_t pitchWord)
{
    SRL::Sound::Pcm::SetVoicePitch(voice, pitchWord);
}

inline uint8_t CarAudioSystem::ResolveVoice(const AudioVoiceGroup group) const
{
    return (voiceRouter_ ? voiceRouter_ : &DefaultVoiceRouter())->ResolveVoice(group);
}

inline void CarAudioSystem::TickVoiceLifetime()
{
    if (state_.shiftSoundCooldownFrames > 0u)
    {
        --state_.shiftSoundCooldownFrames;
    }
    if ((state_.shiftUpVoiceFrames > 0u) && (--state_.shiftUpVoiceFrames == 0u))
    {
        StopVoice(ResolveVoice(AudioVoiceGroup::CarShiftUp));
    }
    if ((state_.shiftDownVoiceFrames > 0u) && (--state_.shiftDownVoiceFrames == 0u))
    {
        StopVoice(ResolveVoice(AudioVoiceGroup::CarShiftDown));
    }
}

inline void CarAudioSystem::EnsureEngineVoiceStarted()
{
    if (!state_.engineActive && engineSample_)
    {
        state_.engineActive = engineSample_->PlayOnVoice(ResolveVoice(AudioVoiceGroup::CarEngine),
                                                         kEngineVolume,
                                                         0);
    }
}

inline void CarAudioSystem::UpdateEngineSnapshot(const uint16_t pitchWord)
{
    snapshot_.currentRpm = state_.audioRpmCurrent;
    snapshot_.enginePitchWord = pitchWord;
    snapshot_.activeSampleIdx = 0u;
    snapshot_.idleVolume = kEngineVolume;
    snapshot_.revVolume = kEngineVolume;
    snapshot_.enginePlaying = state_.engineActive;
}

inline void CarAudioSystem::TriggerShiftVoice(SRL::Sound::Pcm::WaveSound* sample,
                                              const uint8_t voice,
                                              const uint8_t volume,
                                              const int8_t pan,
                                              uint8_t& ioVoiceFrames,
                                              const GameplayFrameState& fs)
{
    if (!sample)
    {
        return;
    }

    StopVoice(voice);
    if (sample->PlayOnVoice(voice, volume, pan))
    {
        state_.lastShiftSoundFrameId = fs.frameId;
        state_.shiftSoundCooldownFrames = kShiftRetriggerCooldownFrames;
        ioVoiceFrames = kShiftVoiceHoldFrames;
    }
}

inline const char* CarAudioSystem::ResolveAssetName(const AudioCue cue, const CarAudioProfile profile)
{
    const bool lowQuality = IsLowQualityCarAudioProfile(profile);
    switch (cue)
    {
    case AudioCue::Engine:
        return "ENGSD.WAV";
    case AudioCue::ShiftUp:
        return lowQuality ? "SUPLQ.WAV" : "SHIUP.WAV";
    case AudioCue::ShiftDown:
        return lowQuality ? "SDNLQ.WAV" : "SHIDN.WAV";
    case AudioCue::TireSkid:
        return lowQuality ? "TILDLQ.WAV" : "TIRSLD.WAV";
    default:
        return nullptr;
    }
}

inline SRL::Sound::Pcm::WaveSound* CarAudioSystem::TryLoadWaveCue(const AudioCue cue)
{
    const CarAudioProfile activeProfile = GetActiveCarAudioProfile();
    if (const char* preferredName = ResolveAssetName(cue, activeProfile))
    {
        if (SRL::Sound::Pcm::WaveSound* wave = TryLoadWave(preferredName))
        {
            return wave;
        }
    }
    return nullptr;
}

inline SRL::Sound::Pcm::WaveSound* CarAudioSystem::TryLoadWave(const char* filename)
{
    SRL::Cd::File f(filename);
    if (!f.Exists())
    {
        return nullptr;
    }
    return new SRL::Sound::Pcm::WaveSound(&f);
}

} // namespace Game
