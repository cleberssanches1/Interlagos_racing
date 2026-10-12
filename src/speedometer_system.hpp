#pragma once

#include <cstdint>

#include <srl.hpp>

#include "speedometer_policy.hpp"

// One indexed atlas in VDP2 NBG2. Only changed pattern-name words are written
// while racing; the atlas, palette and tile graphics remain resident in VRAM.
class SpeedometerSystem
{
public:
    bool Initialize()
    {
        if (ready_) return true;
        SRL::Cd::ChangeDir((const char*)0);
        SRL::Cd::File file("SPDHUD.TGA");
        if (!file.Exists()) { status_ = 1; return false; }
        return Load(file);
    }

    void Update(int16_t speedKmh, int16_t engineRpm, char gear)
    {
        if (!ready_) return;
        const SpeedometerPolicy::Display next =
            SpeedometerPolicy::Resolve(speedKmh, engineRpm, gear);

        if (!displayValid_ || next.gear != displayed_.gear) DrawGear(next.gear);
        if (!displayValid_ || next.speedKmh != displayed_.speedKmh) DrawSpeed(next.speedKmh);
        if (!displayValid_ || next.rpmSegments != displayed_.rpmSegments)
            DrawRpm(next.rpmSegments);
        displayed_ = next;
        displayValid_ = true;
    }

    bool Ready() const { return ready_; }
    // 0=disabled/not attempted, 1=CD name, 2=Cart RAM, 3=format,
    // 4=tile conversion, 5=VDP2 VRAM, 6=resident and enabled, 7=CD read.
    uint8_t StatusCode() const { return status_; }

private:
    static constexpr uint8_t kMapWidth = 32;
    static constexpr uint8_t kAtlasWidth = 16;
    static constexpr uint8_t kPanelX = 15; // 80x80 at (240,160)
    static constexpr uint8_t kPanelY = 10;
    static constexpr uint8_t kRpmCells[10][2] = {
        {0,4}, {0,3}, {0,2}, {1,3}, {1,2},
        {2,2}, {2,1}, {3,1}, {3,2}, {4,2}
    };

    struct CartIndexedBitmap final : SRL::Bitmap::IBitmap
    {
        uint8_t* pixels;
        SRL::Bitmap::Palette palette;

        CartIndexedBitmap(uint8_t* sourcePixels,
                          SRL::Types::HighColor* colors,
                          size_t count)
            : pixels(sourcePixels), palette(colors, count) {}

        uint8_t* GetData() override { return pixels; }
        SRL::Bitmap::BitmapInfo GetInfo() override
        {
            return SRL::Bitmap::BitmapInfo(256, 240, &palette);
        }
    };

    static uint16_t ReadLe16(const uint8_t* p)
    {
        return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
    }

    bool Load(SRL::Cd::File& file)
    {
        // Stage the indexed TGA and Bmp2Tile conversion in the expansion cart.
        // Only Cd::File's one-sector read buffer uses Work RAM temporarily.
        const size_t fileBytes = static_cast<size_t>(file.Size.Bytes);
        if (fileBytes < 18u + 4u + 256u * 240u ||
            fileBytes > 80u * 1024u)
        { status_ = 3; return false; }
        if (SRL::Memory::CartRam::GetReport().FreeSize < fileBytes + 80u * 1024u)
        { status_ = 2; return false; }

        uint8_t* tga = static_cast<uint8_t*>(SRL::Memory::CartRam::Malloc(fileBytes));
        if (!tga) { status_ = 2; return false; }
        if (!file.Open())
        {
            SRL::Memory::CartRam::Free(tga);
            status_ = 7;
            return false;
        }
        const int32_t read = file.Read(static_cast<int32_t>(fileBytes), tga);
        file.Close();
        if (read != static_cast<int32_t>(fileBytes))
        {
            SRL::Memory::CartRam::Free(tga);
            status_ = 7;
            return false;
        }

        const uint16_t paletteCount = ReadLe16(tga + 5);
        const uint32_t pixelOffset = 18u + tga[0] + (static_cast<uint32_t>(paletteCount) * 4u);
        if (tga[1] != 1u || tga[2] != 1u || ReadLe16(tga + 3) != 0u ||
            paletteCount < 2u || paletteCount > 255u || tga[7] != 32u ||
            ReadLe16(tga + 12) != 256u || ReadLe16(tga + 14) != 240u ||
            tga[16] != 8u || (tga[17] & 0x10u) != 0u ||
            pixelOffset > fileBytes || fileBytes - pixelOffset < 256u * 240u)
        {
            SRL::Memory::CartRam::Free(tga);
            status_ = 3;
            return false;
        }

        // The TGA is 8-bit indexed; Bmp2Tile expects packed nibbles for 4bpp.
        // Reindex opaque colors after transparent slot zero before packing.
        const bool fourBit = paletteCount <= 17u;
        const uint8_t* paletteBytes = tga + 18u + tga[0];
        uint8_t remap[256]{};
        SRL::Types::HighColor colors[256]{};
        if (fourBit)
        {
            uint8_t nextColor = 1u;
            for (uint16_t i = 0; i < paletteCount; ++i)
            {
                const uint8_t* entry = paletteBytes + static_cast<uint32_t>(i) * 4u;
                const bool matte = entry[2] > 150u && entry[0] > 150u && entry[1] < 90u;
                if (entry[3] == 0u || matte) continue;
                if (nextColor >= 16u)
                {
                    SRL::Memory::CartRam::Free(tga);
                    status_ = 3;
                    return false;
                }
                remap[i] = nextColor;
                colors[nextColor++] = SRL::Types::HighColor::FromRGB555(
                    static_cast<uint8_t>(entry[2] >> 3),
                    static_cast<uint8_t>(entry[1] >> 3),
                    static_cast<uint8_t>(entry[0] >> 3));
            }
        }
        else
        {
            for (uint16_t i = 1; i < paletteCount; ++i)
            {
                const uint8_t* entry = paletteBytes + static_cast<uint32_t>(i) * 4u;
                if (entry[3] == 0u) continue;
                colors[i] = SRL::Types::HighColor::FromRGB555(
                    static_cast<uint8_t>(entry[2] >> 3),
                    static_cast<uint8_t>(entry[1] >> 3),
                    static_cast<uint8_t>(entry[0] >> 3));
            }
        }
        const bool topOrigin = (tga[17] & 0x20u) != 0u;
        uint8_t* pixels = tga + pixelOffset;
        uint8_t* packed = nullptr;
        if (fourBit)
        {
            packed = static_cast<uint8_t*>(SRL::Memory::CartRam::Malloc(256u * 240u / 2u));
            if (!packed)
            {
                SRL::Memory::CartRam::Free(tga);
                status_ = 2;
                return false;
            }
            for (uint16_t y = 0; y < 240u; ++y)
            {
                const uint8_t* row = pixels + static_cast<uint32_t>(topOrigin ? y : 239u - y) * 256u;
                uint8_t* dst = packed + static_cast<uint32_t>(y) * 128u;
                for (uint16_t x = 0; x < 128u; ++x)
                {
                    const uint8_t a = row[x * 2u];
                    const uint8_t b = row[x * 2u + 1u];
                    if (a >= paletteCount || b >= paletteCount)
                    {
                        SRL::Memory::CartRam::Free(packed);
                        SRL::Memory::CartRam::Free(tga);
                        status_ = 3;
                        return false;
                    }
                    dst[x] = static_cast<uint8_t>((remap[a] << 4u) | remap[b]);
                }
            }
        }
        else
        {
            if (!topOrigin)
            {
                uint8_t row[256];
                for (uint16_t y = 0; y < 120u; ++y)
                {
                    uint8_t* upper = pixels + static_cast<uint32_t>(y) * 256u;
                    uint8_t* lower = pixels + static_cast<uint32_t>(239u - y) * 256u;
                    for (uint16_t x = 0; x < 256u; ++x) row[x] = upper[x];
                    for (uint16_t x = 0; x < 256u; ++x) upper[x] = lower[x];
                    for (uint16_t x = 0; x < 256u; ++x) lower[x] = row[x];
                }
            }
        }

        const uint16_t colorCount = fourBit ? 16u : paletteCount;
        CartIndexedBitmap bitmap(fourBit ? packed : pixels, colors, colorCount);
        SRL::Tilemap::Interfaces::Bmp2Tile tile(bitmap, 1, SRL::Memory::Zone::CartRam);
        if (packed) SRL::Memory::CartRam::Free(packed);
        SRL::Memory::CartRam::Free(tga);

        if (!tile.GetCellData() || !tile.GetMapData() || !tile.GetPalData())
        {
            status_ = 4;
            return false;
        }
        const auto tileInfo = tile.GetInfo();
        if (tileInfo.MapWidth != 32 || tileInfo.MapHeight != 32 ||
            tileInfo.CellByteSize > 64 * 1024)
        {
            status_ = 4;
            return false;
        }

        // NBG2 needs one 2 KiB map in B0/B1 and one contiguous cell bank.
        const uint32_t mapFreeB0 = SRL::VDP2::VRAM::GetAvailable(SRL::VDP2::VramBank::B0);
        const uint32_t mapFreeB1 = SRL::VDP2::VRAM::GetAvailable(SRL::VDP2::VramBank::B1);
        uint32_t maxCellFree = 0;
        for (uint8_t bank = 0; bank < 4; ++bank)
        {
            const uint32_t freeBytes = SRL::VDP2::VRAM::GetAvailable(
                static_cast<SRL::VDP2::VramBank>(bank));
            if (freeBytes > maxCellFree) maxCellFree = freeBytes;
        }
        if ((mapFreeB0 < 4096u && mapFreeB1 < 4096u) ||
            maxCellFree < tileInfo.CellByteSize + 4096u)
        {
            status_ = 5;
            return false;
        }

        SRL::VDP2::NBG2::LoadTilemap(tile);
        map_ = static_cast<volatile uint16_t*>(SRL::VDP2::NBG2::GetMapAddress());
        if (reinterpret_cast<uint32_t>(map_) < VDP2_VRAM_A0 ||
            reinterpret_cast<uint32_t>(SRL::VDP2::NBG2::GetCellAddress()) < VDP2_VRAM_A0)
        {
            map_ = nullptr;
            status_ = 5;
            return false;
        }
        for (uint8_t y = 0; y < 15; ++y)
            for (uint8_t x = 0; x < kAtlasWidth; ++x)
                atlas_[y * kAtlasWidth + x] = map_[y * kMapWidth + x];

        const uint16_t blank = Source(15, 14);
        for (uint16_t i = 0; i < 32u * 32u; ++i) map_[i] = blank;
        for (uint8_t y = 0; y < 5; ++y)
            for (uint8_t x = 0; x < 5; ++x)
                Put(kPanelX + x, kPanelY + y, Source(x, y));

        // The Cart RAM tilemap is released on return; VRAM owns its copy.
        SRL::VDP2::NBG2::SetPriority(SRL::VDP2::Priority::Layer7);
        SRL::VDP2::NBG2::TransparentEnable();
        SRL::VDP2::NBG2::ScrollEnable();
        ready_ = true;
        status_ = 6;
        Update(0, 4200, 'N');
        return true;
    }

    uint16_t Source(uint8_t x, uint8_t y) const
    {
        return atlas_[static_cast<uint16_t>(y) * kAtlasWidth + x];
    }

    void Put(uint8_t x, uint8_t y, uint16_t tile)
    {
        map_[static_cast<uint16_t>(y) * kMapWidth + x] = tile;
    }

    void DrawGear(char gear)
    {
        uint8_t index = (gear == 'N') ? 8u : ((gear == 'R') ? 9u : static_cast<uint8_t>(gear - '1'));
        Put(kPanelX + 4u, kPanelY, Source(index, 5u));
    }

    void DrawSpeed(uint16_t speed)
    {
        const uint8_t numbers[3] = {
            static_cast<uint8_t>(speed / 100u),
            static_cast<uint8_t>((speed / 10u) % 10u),
            static_cast<uint8_t>(speed % 10u)
        };
        for (uint8_t column = 0; column < 3; ++column)
        {
            Put(kPanelX + 1u + column, kPanelY + 4u,
                Source(numbers[column], 6u));
        }
    }

    void DrawRpm(uint8_t lit)
    {
        for (uint8_t i = 0; i < 10; ++i)
        {
            const uint8_t x = kRpmCells[i][0];
            const uint8_t y = kRpmCells[i][1];
            const uint8_t variant = static_cast<uint8_t>((lit ? lit - 1u : 0u) * 10u + i);
            Put(kPanelX + x, kPanelY + y,
                lit ? Source(static_cast<uint8_t>(variant % 16u),
                             static_cast<uint8_t>(7u + variant / 16u))
                    : Source(x, y));
        }
    }

    volatile uint16_t* map_ = nullptr;
    uint16_t atlas_[16u * 15u]{};
    SpeedometerPolicy::Display displayed_{};
    bool ready_ = false;
    bool displayValid_ = false;
    uint8_t status_ = 0;
};
