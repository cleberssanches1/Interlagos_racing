#pragma once

#include <srl.hpp>

#include <cstddef>
#include <cstdint>

#ifndef TRACK_BAKED_LIGHTING
#define TRACK_BAKED_LIGHTING 1
#endif

// Fixed VDP1 Gouraud-table arena owned by the track.  Two 2048-entry banks
// are alternated once per submitted frame.  The CPU writes only the bank that
// is not referenced by the VDP1 command list currently being displayed; this
// keeps streaming/reordering of track segments from changing Gouraud values
// halfway through a visible frame.  The car starts at entry 4096, so the two
// track banks remain disjoint from every other renderer.
namespace TrackGouraud
{
class FrameArena
{
public:
    static constexpr uint16_t kBaseEntry = 0u;
    static constexpr uint16_t kCapacityEntries = 2048u;
    static constexpr uint16_t kBankCount = 2u;
    static constexpr uint16_t kTrackEntries =
        static_cast<uint16_t>(kCapacityEntries * kBankCount);
    static constexpr uint16_t kAddressBase = 0xe000u;

    static void BeginFrame()
    {
        Cursor() = 0u;
        uint32_t& frame = FrameCounter();
        ++frame;
        if (frame == 0u) ++frame;
    }

    static uint32_t FrameId()
    {
        return FrameCounter();
    }

    static bool Reserve(size_t count, uint16_t& outBaseEntry)
    {
#if TRACK_BAKED_LIGHTING
        if (count == 0u || count > kCapacityEntries) return false;
        const size_t cursor = static_cast<size_t>(Cursor());
        if (cursor + count > kCapacityEntries) return false;
        outBaseEntry = static_cast<uint16_t>(ActiveBankBase() + Cursor());
        Cursor() = static_cast<uint16_t>(cursor + count);
        return true;
#else
        (void)count;
        (void)outBaseEntry;
        return false;
#endif
    }

    static uint16_t AttributeAddress(uint16_t entry)
    {
        return static_cast<uint16_t>(kAddressBase + entry);
    }

    static void WritePackedEntry(uint16_t entry, uint32_t packedLevels)
    {
#if TRACK_BAKED_LIGHTING
        if (entry >= static_cast<uint16_t>(kBaseEntry + kTrackEntries)) return;
        SRL::Types::HighColor* table = SRL::VDP1::GetGouraudTable();
        if (!table) return;
        const size_t first = static_cast<size_t>(entry) << 2;
        for (size_t corner = 0; corner < 4u; ++corner)
        {
            const uint8_t level = static_cast<uint8_t>((packedLevels >> (corner * 8u)) & 0x1fu);
            table[first + corner] = SRL::Types::HighColor::FromRGB555(level, level, level);
        }
#else
        (void)entry;
        (void)packedLevels;
#endif
    }

private:
    static uint16_t ActiveBankBase()
    {
        // Frame 1 starts in bank 1.  The previous submitted frame, if any,
        // therefore remains in the other bank while this one is assembled.
        const uint16_t bank = static_cast<uint16_t>(FrameCounter() & 1u);
        return static_cast<uint16_t>(kBaseEntry + bank * kCapacityEntries);
    }

    static uint16_t& Cursor()
    {
        static uint16_t cursor = 0u;
        return cursor;
    }

    static uint32_t& FrameCounter()
    {
        static uint32_t frame = 0u;
        return frame;
    }
};
} // namespace TrackGouraud
