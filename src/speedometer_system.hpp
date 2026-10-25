#pragma once

#include <cstdint>

#include <srl.hpp>

#include "speedometer_policy.hpp"
#include "time_attack_session.hpp"

// One indexed atlas in VDP2 NBG2. Time Attack uses 8x8 pattern names so its
// timing art can be truly half-sized; the normal speedometer keeps 16x16.
class SpeedometerSystem
{
public:
    bool Initialize(bool timeAttack = false)
    {
        if (ready_) return true;
        SRL::Cd::ChangeDir((const char*)0);
        SRL::Cd::File file(timeAttack ? "TAHUD.TGA" : "SPDHUD.TGA");
        if (!file.Exists()) { status_ = 1; return false; }
        timeAttackArt_ = timeAttack;
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
    bool HasTimeAttackArt() const { return ready_ && timeAttackArt_; }

    // The timing overlay shares the resident NBG2 atlas and map with the
    // speedometer. No VDP1 commands, allocation or new VDP2 layer per frame.
    bool DrawTimeAttack(const TimeAttack::Session& session, uint32_t now)
    {
        if (!HasTimeAttackArt()) return false;
        if (!timeAttackLabelsDrawn_)
        {
            static const char kBlankRow[] = "                                        ";
            for (uint8_t row = 0u; row < 8u; ++row)
                SRL::Debug::Print(0, row, kBlankRow);
            DrawStrip(0u, 0u, 10u, 0u, 8u);   // TOTAL TIME, 64x8
            DrawStrip(0u, 3u, 10u, 2u, 8u);   // LAP TIME, 64x8
            DrawStrip(32u, 0u, 10u, 4u, 7u); // BEST LAP, 56x8
            DrawStrip(33u, 3u, 10u, 6u, 7u); // COURSE RECORD, 56x8
            timeAttackLabelsDrawn_ = true;
        }
        const uint8_t hz = session.TicksPerSecond();
        DrawClock(0u, 1u, session.TotalTicks(now), hz, false, true);
        DrawClock(31u, 1u, session.BestLapTicks(), hz, false,
                  session.BestLapTicks() != 0u);
        DrawClock(31u, 4u, session.RecordCourseTicks(), hz, false,
                  session.RecordCourseTicks() != 0u);

        const uint8_t firstVisible = session.LapsCompleted() > 2u
            ? static_cast<uint8_t>(session.LapsCompleted() - 2u) : 0u;
        for (uint8_t slot = 0u; slot < 3u; ++slot)
        {
            const uint8_t lap = static_cast<uint8_t>(firstVisible + slot);
            const uint8_t y = static_cast<uint8_t>(4u + slot);
            const bool current = lap == session.LapsCompleted() &&
                                 session.CurrentPhase() == TimeAttack::Phase::Running;
            const bool completed = lap < session.LapsCompleted();
            if (lap >= session.LapsTarget())
            {
                for (uint8_t x = 0u; x < 10u; ++x) Put(x, y, TimeSource(30u, 28u));
                continue;
            }
            Put(0u, y, TimeGlyph(static_cast<uint8_t>((lap + 1u) % 10u), current));
            DrawClock(1u, y,
                      current ? session.CurrentLapTicks(now) : session.LapTicks(lap),
                      hz, current, completed || current);
        }
        DrawRaceState(session, now);
        return true;
    }
    // 0=disabled, 1=CD name, 2=Cart RAM, 4=tile conversion,
    // 5=VDP2 VRAM, 6=ready, 7=CD read; 31..34 identify format stages.
    uint8_t StatusCode() const { return status_; }

private:
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

    // Bmp2Tile already stores every 16x16 character as four consecutive
    // 8x8 cells. Only the pattern-name map needs expanding for NBG2 8x8.
    struct HalfCellTilemap final : SRL::Tilemap::ITilemap
    {
        SRL::Tilemap::Interfaces::Bmp2Tile& source;
        uint16_t* map;
        SRL::Tilemap::TilemapInfo info;

        explicit HalfCellTilemap(SRL::Tilemap::Interfaces::Bmp2Tile& tile)
            : source(tile), map(static_cast<uint16_t*>(
                  SRL::Memory::CartRam::Malloc(64u * 64u * sizeof(uint16_t)))),
              info(tile.GetInfo())
        {
            if (!map) return;
            info.CharSize = CHAR_SIZE_1x1;
            info.MapWidth = 64u;
            info.MapHeight = 64u;
            info.MapByteSize = 64u * 64u * sizeof(uint16_t);
            const uint16_t* old = static_cast<const uint16_t*>(source.GetMapData());
            for (uint16_t y = 0u; y < 32u; ++y)
                for (uint16_t x = 0u; x < 32u; ++x)
                {
                    const uint16_t base = static_cast<uint16_t>(old[y * 32u + x] * 4u);
                    const uint16_t offset = static_cast<uint16_t>(y * 2u * 64u + x * 2u);
                    map[offset] = base;
                    map[offset + 1u] = static_cast<uint16_t>(base + 1u);
                    map[offset + 64u] = static_cast<uint16_t>(base + 2u);
                    map[offset + 65u] = static_cast<uint16_t>(base + 3u);
                }
        }
        ~HalfCellTilemap() { if (map) SRL::Memory::CartRam::Free(map); }
        void* GetCellData() override { return source.GetCellData(); }
        void* GetMapData() override { return map; }
        void* GetPalData() override { return source.GetPalData(); }
        SRL::Tilemap::TilemapInfo GetInfo() override { return info; }
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
        const size_t pixelBytes = 256u * 240u;
        if (fileBytes < 18u + 4u + pixelBytes ||
            fileBytes > 80u * 1024u)
        { status_ = 31; return false; }
        if (SRL::Memory::CartRam::GetReport().FreeSize < fileBytes + pixelBytes)
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
            pixelOffset > fileBytes || fileBytes - pixelOffset < pixelBytes)
        {
            SRL::Memory::CartRam::Free(tga);
            status_ = 32;
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
                    status_ = 33;
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
            packed = static_cast<uint8_t*>(SRL::Memory::CartRam::Malloc(pixelBytes / 2u));
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
                        status_ = 34;
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
        if (timeAttackArt_)
        {
            HalfCellTilemap half(tile);
            if (!half.map) { status_ = 2; return false; }
            if (!InstallTilemap(half)) return false;
        }
        else if (!InstallTilemap(tile)) return false;

        ready_ = true;
        status_ = 6;
        Update(0, 4200, 'N');
        return true;
    }

    bool InstallTilemap(SRL::Tilemap::ITilemap& tile)
    {
        const auto tileInfo = tile.GetInfo();
        mapWidth_ = timeAttackArt_ ? 64u : 32u;
        if (tileInfo.MapWidth != mapWidth_ || tileInfo.MapHeight != mapWidth_ ||
            tileInfo.CellByteSize > 64 * 1024)
        { status_ = 4; return false; }

        // NBG2 map is 2 KiB normally, 8 KiB in Time Attack's 8x8 mode.
        const uint32_t mapFreeB0 = SRL::VDP2::VRAM::GetAvailable(SRL::VDP2::VramBank::B0);
        const uint32_t mapFreeB1 = SRL::VDP2::VRAM::GetAvailable(SRL::VDP2::VramBank::B1);
        uint32_t maxCellFree = 0;
        for (uint8_t bank = 0; bank < 4; ++bank)
        {
            const uint32_t freeBytes = SRL::VDP2::VRAM::GetAvailable(
                static_cast<SRL::VDP2::VramBank>(bank));
            if (freeBytes > maxCellFree) maxCellFree = freeBytes;
        }
        const uint32_t mapNeed = static_cast<uint32_t>(mapWidth_) * mapWidth_ * 2u;
        if ((mapFreeB0 < mapNeed && mapFreeB1 < mapNeed) ||
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
        if (timeAttackArt_)
        {
            const uint32_t cellOffset =
                (reinterpret_cast<uint32_t>(SRL::VDP2::NBG2::GetCellAddress()) -
                 VDP2_VRAM_A0) & 0x1ffffu;
            if ((cellOffset / 32u) +
                    ((static_cast<uint32_t>(tileInfo.CellByteSize) + 31u) / 32u) > 4096u)
            { map_ = nullptr; status_ = 5; return false; }
        }
        for (uint8_t y = 0; y < 15u; ++y)
            for (uint8_t x = 0; x < kAtlasWidth; ++x)
                atlas_[y * kAtlasWidth + x] = map_[
                    timeAttackArt_ ? (static_cast<uint16_t>(y) * 2u * 64u + x * 2u)
                                   : (static_cast<uint16_t>(y) * 32u + x)];

        const uint16_t blank = Source(15u, 14u);
        for (uint16_t i = 0; i < mapNeed / 2u; ++i) map_[i] = blank;
        for (uint8_t y = 0; y < 5; ++y)
            for (uint8_t x = 0; x < 5; ++x)
                PutPanel(kPanelX + x, kPanelY + y, x, y);

        // The Cart RAM tilemap is released on return; VRAM owns its copy.
        SRL::VDP2::NBG2::SetPriority(SRL::VDP2::Priority::Layer7);
        SRL::VDP2::NBG2::TransparentEnable();
        SRL::VDP2::NBG2::ScrollEnable();
        if (timeAttackArt_) InstallTimeAttackPalette();
        return true;
    }

    void InstallTimeAttackPalette()
    {
        // VDP2 special color calculation mode 3 uses CRAM bit 15 per pixel.
        // A second 16-color bank keeps the speedometer fully opaque while
        // only the timing-strip background colors blend with the game.
        const int altId = SRL::CRAM::GetFreeBank(SRL::CRAM::TextureColorMode::Paletted16);
        if (altId < 0 || altId >= 16) return; // 1-word PND has four palette bits.
        SRL::CRAM::SetBankUsedState(static_cast<uint16_t>(altId),
                                    SRL::CRAM::TextureColorMode::Paletted16, true);
        SRL::CRAM::Palette alt(SRL::CRAM::TextureColorMode::Paletted16,
                               static_cast<uint16_t>(altId));
        volatile uint16_t* original = reinterpret_cast<volatile uint16_t*>(
            SRL::VDP2::NBG2::TilePalette.GetData());
        volatile uint16_t* tinted = reinterpret_cast<volatile uint16_t*>(alt.GetData());
        for (uint8_t i = 0u; i < 16u; ++i)
        {
            const uint16_t color = original[i];
            const uint8_t red = static_cast<uint8_t>(color & 31u);
            const uint8_t green = static_cast<uint8_t>((color >> 5u) & 31u);
            const uint8_t blue = static_cast<uint8_t>((color >> 10u) & 31u);
            const bool darkStrip = red < 7u && green < 8u && blue < 15u;
            const bool redStrip = red > 18u && green < 7u && blue < 9u;
            tinted[i] = static_cast<uint16_t>((color & 0x7fffu) |
                ((darkStrip || redStrip) ? 0x8000u : 0u));
            original[i] = static_cast<uint16_t>(color & 0x7fffu);
        }
        timeAttackPalette_ = static_cast<uint8_t>(altId);
        SRL::VDP2::NBG2::SetOpacity(SRL::Math::Fxp(0.58));
        volatile uint16_t* const specialColorMode =
            reinterpret_cast<volatile uint16_t*>(0x25f800eeu);
        *specialColorMode = static_cast<uint16_t>((*specialColorMode & ~0x0030u) | 0x0030u);
    }

    uint16_t Source(uint8_t x, uint8_t y) const
    {
        return atlas_[static_cast<uint16_t>(y) * kAtlasWidth + x];
    }

    uint16_t Source8(uint8_t x, uint8_t y) const
    {
        const uint16_t base = Source(static_cast<uint8_t>(x / 2u),
                                     static_cast<uint8_t>(y / 2u));
        return static_cast<uint16_t>((base & 0xf000u) |
            ((base + (x & 1u) + ((y & 1u) * 2u)) & 0x0fffu));
    }

    uint16_t TimeSource(uint8_t x, uint8_t y) const
    {
        const uint16_t tile = Source8(x, y);
        return timeAttackPalette_ < 16u
            ? static_cast<uint16_t>((tile & 0x0fffu) | (timeAttackPalette_ << 12u))
            : tile;
    }

    void Put(uint8_t x, uint8_t y, uint16_t tile)
    {
        const uint16_t offset = static_cast<uint16_t>(y) * mapWidth_ + x;
        if (map_[offset] != tile) map_[offset] = tile;
    }

    void PutPanel(uint8_t x, uint8_t y, uint8_t sourceX, uint8_t sourceY)
    {
        if (!timeAttackArt_)
        { Put(x, y, Source(sourceX, sourceY)); return; }
        for (uint8_t dy = 0u; dy < 2u; ++dy)
            for (uint8_t dx = 0u; dx < 2u; ++dx)
                Put(static_cast<uint8_t>((x * 2u) + dx),
                    static_cast<uint8_t>((y * 2u) + dy),
                    Source8(static_cast<uint8_t>(sourceX * 2u + dx),
                            static_cast<uint8_t>(sourceY * 2u + dy)));
    }

    void DrawStrip(uint8_t destX, uint8_t destY,
                   uint8_t sourceX, uint8_t sourceY, uint8_t count)
    {
        for (uint8_t i = 0u; i < count; ++i)
            Put(static_cast<uint8_t>(destX + i), destY,
                TimeSource(static_cast<uint8_t>(sourceX + i), sourceY));
    }

    uint16_t TimeGlyph(uint8_t glyph, bool activeLap) const
    {
        // Source tiles occupy only the unused cells of the original 256x240
        // speedometer atlas; cell 15,14 remains the transparent blank.
        if (activeLap)
        {
            if (glyph == 0u) return TimeSource(30u, 6u);
            if (glyph <= 11u)
                return TimeSource(static_cast<uint8_t>((glyph + 4u) * 2u), 8u);
            return TimeSource(24u, 10u);
        }
        if (glyph < 3u)
            return TimeSource(static_cast<uint8_t>((13u + glyph) * 2u), 0u);
        if (glyph < 6u)
            return TimeSource(static_cast<uint8_t>((10u + glyph) * 2u), 2u);
        if (glyph < 10u)
            return TimeSource(static_cast<uint8_t>((6u + glyph) * 2u), 4u);
        return TimeSource(static_cast<uint8_t>((glyph + 2u) * 2u), 6u);
    }

    void DrawClock(uint8_t x, uint8_t y, uint32_t ticks, uint8_t hz,
                   bool activeLap, bool valid)
    {
        if (hz == 0u) hz = 60u;
        uint32_t seconds = ticks / hz;
        uint32_t minutes = seconds / 60u;
        if (minutes > 99u) minutes = 99u;
        seconds %= 60u;
        const uint32_t millis = ((ticks % hz) * 1000u) / hz;
        const uint8_t glyphs[9] = {
            static_cast<uint8_t>(valid ? minutes / 10u : 12u),
            static_cast<uint8_t>(valid ? minutes % 10u : 12u), 10u,
            static_cast<uint8_t>(valid ? seconds / 10u : 12u),
            static_cast<uint8_t>(valid ? seconds % 10u : 12u), 11u,
            static_cast<uint8_t>(valid ? millis / 100u : 12u),
            static_cast<uint8_t>(valid ? millis / 10u % 10u : 12u),
            static_cast<uint8_t>(valid ? millis % 10u : 12u)
        };
        for (uint8_t i = 0u; i < 9u; ++i)
            Put(static_cast<uint8_t>(x + i), y, TimeGlyph(glyphs[i], activeLap));
    }

    uint16_t CountdownTile(uint8_t index) const
    {
        if (index < 3u)
            return TimeSource(static_cast<uint8_t>((13u + index) * 2u), 10u);
        if (index < 9u)
            return TimeSource(static_cast<uint8_t>((7u + index) * 2u), 12u);
        return TimeSource(static_cast<uint8_t>((index - 1u) * 2u), 28u);
    }

    void DrawRaceState(const TimeAttack::Session& session, uint32_t now)
    {
        uint8_t sprite = 255u;
        if (session.CurrentPhase() == TimeAttack::Phase::Countdown)
        {
            const uint8_t number = session.CountdownNumber(now);
            sprite = number >= 1u && number <= 3u
                ? static_cast<uint8_t>(3u - number) : 3u;
        }
        else if (session.CurrentPhase() == TimeAttack::Phase::OutLap)
            sprite = session.TotalTicks(now) < session.TicksPerSecond() ? 3u : 255u;

        for (uint8_t dy = 0u; dy < 2u; ++dy)
            for (uint8_t dx = 0u; dx < 2u; ++dx)
            {
                const uint16_t tile = sprite == 255u ? TimeSource(30u, 28u) :
                    CountdownTile(static_cast<uint8_t>(sprite * 4u + dy * 2u + dx));
                Put(static_cast<uint8_t>(19u + dx), static_cast<uint8_t>(10u + dy), tile);
            }
    }

    void DrawGear(char gear)
    {
        uint8_t index = (gear == 'N') ? 8u : ((gear == 'R') ? 9u : static_cast<uint8_t>(gear - '1'));
        PutPanel(kPanelX + 4u, kPanelY, index, 5u);
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
            PutPanel(kPanelX + 1u + column, kPanelY + 4u,
                     numbers[column], 6u);
        }
    }

    void DrawRpm(uint8_t lit)
    {
        for (uint8_t i = 0; i < 10; ++i)
        {
            const uint8_t x = kRpmCells[i][0];
            const uint8_t y = kRpmCells[i][1];
            const uint8_t variant = static_cast<uint8_t>((lit ? lit - 1u : 0u) * 10u + i);
            PutPanel(kPanelX + x, kPanelY + y,
                     lit ? static_cast<uint8_t>(variant % 16u) : x,
                     lit ? static_cast<uint8_t>(7u + variant / 16u) : y);
        }
    }

    volatile uint16_t* map_ = nullptr;
    uint16_t atlas_[16u * 15u]{};
    SpeedometerPolicy::Display displayed_{};
    bool ready_ = false;
    bool displayValid_ = false;
    bool timeAttackArt_ = false;
    bool timeAttackLabelsDrawn_ = false;
    uint8_t mapWidth_ = 32u;
    uint8_t timeAttackPalette_ = 255u;
    uint8_t status_ = 0;
};
