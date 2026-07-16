#pragma once

#include <cstdint>

#include <srl.hpp>

namespace Game
{

/**
 * Engine loop via SCSP hardware loop (SlaveDriver-style).
 *
 * - Sample lives in Sound RAM once (not slPCMOn).
 * - Key-on once with loop bit set.
 * - Per frame: only pitch + volume registers (no stop/restart).
 *
 * Placement: Sound RAM [0x40000, 0x78000) — after driver/map, before SGL PCM_Work.
 * Slot 30: avoids shift (12/13) and tire (14).
 */
class ScspEngineLoop
{
public:
    static constexpr uint32_t kSoundRamBase   = 0x25A00000u;
    static constexpr uint32_t kScspRegBase    = 0x25B00000u;
    static constexpr uint32_t kSampleOffset   = 0x00040000u;
    static constexpr uint32_t kSampleMaxBytes = 0x00038000u; // up to PCM_Work
    static constexpr uint8_t  kSlot           = 30u;

    bool LoadWave(const char* filename)
    {
        loaded_ = false;
        playing_ = false;

        SRL::Cd::File file(filename);
        if (!file.Exists() || file.Size.Bytes < 44u)
        {
            return false;
        }

        uint8_t* staging = new (SRL::Memory::Zone::LWRam) uint8_t[file.Size.Bytes];
        if (staging == nullptr)
        {
            return false;
        }

        const int32_t got = file.LoadBytes(0, file.Size.Bytes, staging);
        if (got != static_cast<int32_t>(file.Size.Bytes))
        {
            delete[] staging;
            return false;
        }

        if (!ParseAndUpload(staging, file.Size.Bytes))
        {
            delete[] staging;
            return false;
        }

        delete[] staging;
        loaded_ = true;
        lastPitch_ = 0u;
        lastTl_ = 0xFFu;
        return true;
    }

    bool Start(const uint16_t pitchWord, const uint8_t volume127)
    {
        if (!loaded_)
        {
            return false;
        }

        Stop();

        volatile uint16_t* const slot = SlotRegs();
        const uint32_t sa = sampleOffset_;

        slot[1] = static_cast<uint16_t>(sa & 0xFFFFu);
        slot[2] = static_cast<uint16_t>(loopStartSample_);
        slot[3] = static_cast<uint16_t>(loopEndSample_);
        slot[4] = 0x001Fu;
        slot[5] = 0x000Fu;
        slot[6] = VolumeToTl(volume127);
        slot[7] = 0x0000u;
        slot[8] = pitchWord;
        slot[9] = 0x8000u;
        slot[10] = 0x0000u;
        slot[11] = static_cast<uint16_t>(7u << 13);

        uint16_t ctrl = static_cast<uint16_t>(0x1800u | ((sa >> 16) & 0xFu));
        if (is8Bit_)
        {
            ctrl = static_cast<uint16_t>(ctrl | 0x0010u);
        }
        ctrl = static_cast<uint16_t>(ctrl | (1u << 5)); // normal loop
        slot[0] = ctrl;

        playing_ = true;
        lastPitch_ = pitchWord;
        lastTl_ = VolumeToTl(volume127);
        return true;
    }

    void Update(const uint16_t pitchWord, const uint8_t volume127)
    {
        if (!playing_)
        {
            return;
        }

        volatile uint16_t* const slot = SlotRegs();
        const uint16_t tl = VolumeToTl(volume127);

        if (pitchWord != lastPitch_)
        {
            slot[8] = pitchWord;
            lastPitch_ = pitchWord;
        }
        if (tl != lastTl_)
        {
            slot[6] = tl;
            lastTl_ = tl;
        }
    }

    void Stop()
    {
        if (!playing_)
        {
            return;
        }

        volatile uint16_t* const slot = SlotRegs();
        slot[0] = static_cast<uint16_t>(slot[0] & 0x07FFu);
        playing_ = false;
    }

    bool IsLoaded() const { return loaded_; }
    bool IsPlaying() const { return playing_; }

private:
    bool loaded_ = false;
    bool playing_ = false;
    bool is8Bit_ = true;
    uint32_t sampleOffset_ = kSampleOffset;
    uint32_t loopStartSample_ = 0u;
    uint32_t loopEndSample_ = 0u;
    uint16_t lastPitch_ = 0u;
    uint16_t lastTl_ = 0xFFu;

    static volatile uint16_t* SlotRegs()
    {
        return reinterpret_cast<volatile uint16_t*>(kScspRegBase + (0x20u * kSlot));
    }

    static uint16_t VolumeToTl(const uint8_t volume127)
    {
        const uint8_t v = (volume127 > 127u) ? 127u : volume127;
        const uint16_t atten = static_cast<uint16_t>((127u - v) * 2u);
        return (atten > 0x00FEu) ? 0x00FEu : atten;
    }

    static void PokeSoundRamBytes(const uint32_t offset, const uint8_t* data, const uint32_t size)
    {
        volatile uint8_t* dst =
            reinterpret_cast<volatile uint8_t*>(kSoundRamBase + offset);
        for (uint32_t i = 0; i < size; ++i)
        {
            dst[i] = data[i];
        }
    }

    static void PokeSoundRamS16Be(const uint32_t offset, const int16_t* samples, const uint32_t count)
    {
        volatile uint16_t* dst =
            reinterpret_cast<volatile uint16_t*>(kSoundRamBase + offset);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint16_t s = static_cast<uint16_t>(samples[i]);
            dst[i] = static_cast<uint16_t>(((s & 0x00FFu) << 8) | ((s & 0xFF00u) >> 8));
        }
    }

    bool ParseAndUpload(uint8_t* fileData, const uint32_t fileSize)
    {
        if (fileSize < 44u)
        {
            return false;
        }
        if (fileData[0] != 'R' || fileData[1] != 'I' || fileData[2] != 'F' || fileData[3] != 'F')
        {
            return false;
        }
        if (fileData[8] != 'W' || fileData[9] != 'A' || fileData[10] != 'V' || fileData[11] != 'E')
        {
            return false;
        }

        uint32_t pos = 12u;
        uint16_t audioFormat = 0u;
        uint16_t channels = 0u;
        uint16_t bits = 0u;
        uint8_t* dataPtr = nullptr;
        uint32_t dataBytes = 0u;

        while ((pos + 8u) <= fileSize)
        {
            const char c0 = static_cast<char>(fileData[pos + 0u]);
            const char c1 = static_cast<char>(fileData[pos + 1u]);
            const char c2 = static_cast<char>(fileData[pos + 2u]);
            const char c3 = static_cast<char>(fileData[pos + 3u]);
            const uint32_t chunkSize =
                static_cast<uint32_t>(fileData[pos + 4u]) |
                (static_cast<uint32_t>(fileData[pos + 5u]) << 8) |
                (static_cast<uint32_t>(fileData[pos + 6u]) << 16) |
                (static_cast<uint32_t>(fileData[pos + 7u]) << 24);
            const uint32_t dataOff = pos + 8u;
            if ((dataOff + chunkSize) > fileSize)
            {
                break;
            }

            if (c0 == 'f' && c1 == 'm' && c2 == 't' && c3 == ' ')
            {
                if (chunkSize >= 16u)
                {
                    audioFormat =
                        static_cast<uint16_t>(fileData[dataOff + 0u] |
                                              (fileData[dataOff + 1u] << 8));
                    channels =
                        static_cast<uint16_t>(fileData[dataOff + 2u] |
                                              (fileData[dataOff + 3u] << 8));
                    bits =
                        static_cast<uint16_t>(fileData[dataOff + 14u] |
                                              (fileData[dataOff + 15u] << 8));
                }
            }
            else if (c0 == 'd' && c1 == 'a' && c2 == 't' && c3 == 'a')
            {
                dataPtr = fileData + dataOff;
                dataBytes = chunkSize;
                break;
            }

            pos = dataOff + chunkSize;
            if (chunkSize & 1u)
            {
                ++pos;
            }
        }

        if (dataPtr == nullptr || dataBytes == 0u || audioFormat != 1u || channels != 1u)
        {
            return false;
        }
        if (bits != 8u && bits != 16u)
        {
            return false;
        }
        if (dataBytes > kSampleMaxBytes)
        {
            return false;
        }

        is8Bit_ = (bits == 8u);
        sampleOffset_ = kSampleOffset;

        if (is8Bit_)
        {
            for (uint32_t i = 0; i < dataBytes; ++i)
            {
                dataPtr[i] = static_cast<uint8_t>(
                    static_cast<int16_t>(dataPtr[i]) - 128);
            }
            PokeSoundRamBytes(sampleOffset_, dataPtr, dataBytes);
            loopStartSample_ = 0u;
            loopEndSample_ = dataBytes;
        }
        else
        {
            const uint32_t sampleCount = dataBytes >> 1;
            int16_t* samples = reinterpret_cast<int16_t*>(dataPtr);
            for (uint32_t i = 0; i < sampleCount; ++i)
            {
                const uint8_t lo = dataPtr[(i << 1)];
                const uint8_t hi = dataPtr[(i << 1) + 1u];
                samples[i] = static_cast<int16_t>(lo | (hi << 8));
            }
            PokeSoundRamS16Be(sampleOffset_, samples, sampleCount);
            loopStartSample_ = 0u;
            loopEndSample_ = sampleCount;
        }

        return true;
    }
};

} // namespace Game
