#pragma once

#include <cstddef>
#include <cstdint>
#include <string.h>
#include <srl.hpp>

#include "minimap_marker_layout.hpp"

// LOD0 contact segment -> pre-baked MPINT pixels. The player arrow and NPC
// diamonds share a small VDP2 compositor without erasing the circuit line.
class MinimapSystem
{
public:
    bool Initialize()
    {
        if (ready_) return true;
        SRL::Cd::ChangeDir((const char*)0);
        SRL::Cd::File mapFile("MPILUT.BIN");
        if (!mapFile.Exists()) { status_ = 1; return false; }
        if (!LoadLookup(mapFile)) return false;

        SRL::Cd::File imageFile("MPHUD.TGA");
        if (!imageFile.Exists()) { status_ = 2; return false; }
        if (!LoadImage(imageFile)) return false;

        ready_ = true;
        status_ = 6;
        return true;
    }

    void UpdateSegment(int32_t segmentId, int32_t carYawDeg)
    {
        UpdateMarker(0u, segmentId, carYawDeg);
    }

    void BeginFrame()
    {
        for (uint8_t i = 1u; i < kMarkerCount; ++i)
            markers_[i].requested = false;
    }

    void EndFrame()
    {
        if (!ready_) return;
        for (uint8_t i = 1u; i < kMarkerCount; ++i)
        {
            if (!markers_[i].requested && markers_[i].pose.visible)
            {
                markers_[i].pose.visible = false;
                markersDirty_ = true;
            }
        }
        if (markersDirty_) ComposeMarkers();
    }

    // Extra arrows for Auto Lap NPC cars (slot 0/1 → marker 1/2).
    void UpdateNpcSegment(uint8_t npcSlot, int32_t segmentId, int32_t carYawDeg)
    {
        if (npcSlot >= 2u) return;
        UpdateMarker(static_cast<uint8_t>(1u + npcSlot), segmentId, carYawDeg);
    }

    bool Ready() const { return ready_; }
    uint8_t StatusCode() const { return status_; }

private:
    // VDP2 uses 64 pattern names per row for 8x8 cells (one 512px page).
    static constexpr uint16_t kMapWidth = 64u;
    static constexpr uint16_t kImageCells = 16u;
    static constexpr uint16_t kMapX = 0u;
    static constexpr uint16_t kMapY = 10u; // 128px map with 16px bottom margin
    static constexpr size_t kLookupHeaderBytes = 12u;
    static constexpr uint16_t kMaximumSegments = 512u;
    static constexpr uint32_t kTgaWidth = 256u;
    static constexpr uint32_t kTgaHeight = 240u;

    struct MapTilemap final : SRL::Tilemap::ITilemap
    {
        uint8_t* cells;
        uint16_t* names;
        SRL::Types::HighColor* colors;
        int32_t cellBytes;

        MapTilemap(uint8_t* cellData, uint16_t* mapData,
                   SRL::Types::HighColor* palette, int32_t bytes)
            : cells(cellData), names(mapData), colors(palette), cellBytes(bytes) {}

        void* GetCellData() override { return cells; }
        void* GetMapData() override { return names; }
        void* GetPalData() override { return colors; }
        SRL::Tilemap::TilemapInfo GetInfo() override
        {
            return SRL::Tilemap::TilemapInfo(
                SRL::CRAM::TextureColorMode::Paletted16,
                PNB_1WORD | CN_12BIT, CHAR_SIZE_1x1, PL_SIZE_1x1,
                kMapWidth, kMapWidth, cellBytes);
        }
    };

    static uint16_t ReadLe16(const uint8_t* p)
    {
        return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8u));
    }

    static uint32_t ReadLe32(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8u) |
               (static_cast<uint32_t>(p[2]) << 16u) | (static_cast<uint32_t>(p[3]) << 24u);
    }

    static constexpr uint8_t kMarkerCount = 3u; // player + 2 NPC
    static constexpr uint8_t kBlendBanksPerMarker = 2u;
    static constexpr uint8_t kBlendTiles = 9u;
    static constexpr uint8_t kBlendCellCount =
        static_cast<uint8_t>(kMarkerCount * kBlendBanksPerMarker * kBlendTiles);

    struct MarkerState
    {
        MinimapMarkerLayout::Pose pose{};
        uint8_t blendBank = 0u;
        bool requested = false;
    };

    void UpdateMarker(uint8_t markerIndex, int32_t segmentId, int32_t carYawDeg)
    {
        if (!ready_ || !map_ || markerIndex >= kMarkerCount) return;
        MarkerState& marker = markers_[markerIndex];
        marker.requested = true;
        if (segmentId <= 0 || segmentId > segmentCount_)
        {
            if (marker.pose.visible)
            {
                marker.pose.visible = false;
                markersDirty_ = true;
            }
            return;
        }

        const size_t coordinateOffset =
            kLookupHeaderBytes + (static_cast<uint16_t>(segmentId) - 1u) * 2u;
        const uint8_t u = lookupFile_[coordinateOffset];
        const uint8_t v = lookupFile_[coordinateOffset + 1u];
        // A 16x16 arrow is centered on the LUT pixel, not rounded down to an
        // 8x8 VDP2 cell. Its shifted footprint occupies at most 3x3 cells.
        const uint8_t pixelX = (u < 8u) ? 0u : ((u > 120u) ? 112u : static_cast<uint8_t>(u - 8u));
        const uint8_t pixelY = (v < 8u) ? 0u : ((v > 120u) ? 112u : static_cast<uint8_t>(v - 8u));
        int32_t mapHeading = (carYawDeg * yawSense_ * 100 + yawOffsetCentideg_) % 36000;
        if (mapHeading < 0) mapHeading += 36000;
        const uint8_t direction = static_cast<uint8_t>(((mapHeading + 2250) / 4500) & 7);

        if (marker.pose.visible && pixelX == marker.pose.pixelX &&
            pixelY == marker.pose.pixelY &&
            (markerIndex != 0u || direction == marker.pose.direction)) return;
        marker.pose.pixelX = pixelX;
        marker.pose.pixelY = pixelY;
        marker.pose.direction = direction;
        marker.pose.visible = true;
        markersDirty_ = true;
    }

    void ComposeMarkers()
    {
        // Start every changed cell from the immutable map art. Restoring the
        // pattern left by one marker would also erase another marker sharing
        // that cell. Each owner has two private 3x3 banks, so VDP2 never sees
        // a pattern being edited in place.
        MinimapMarkerLayout::Pose poses[kMarkerCount]{};
        uint8_t nextBank[kMarkerCount]{};
        for (uint8_t i = 0u; i < kMarkerCount; ++i)
        {
            poses[i] = markers_[i].pose;
            nextBank[i] = static_cast<uint8_t>(markers_[i].blendBank ^ 1u);
        }
        for (uint8_t tileY = 0u; tileY < kImageCells; ++tileY)
            for (uint8_t tileX = 0u; tileX < kImageCells; ++tileX)
            {
                const uint16_t artIndex = static_cast<uint16_t>(tileY * kImageCells + tileX);
                const uint16_t mapIndex = static_cast<uint16_t>(
                    (kMapY + tileY) * kMapWidth + kMapX + tileX);
                const uint8_t owner = MinimapMarkerLayout::OwnerForTile(
                    poses, kMarkerCount, tileX, tileY);
                if (owner == 0xffu)
                {
                    if (map_[mapIndex] != baseMap_[artIndex])
                        map_[mapIndex] = baseMap_[artIndex];
                    continue;
                }
                const uint8_t localTile = static_cast<uint8_t>(
                    (tileY - MinimapMarkerLayout::CellY(poses[owner])) * 3u +
                    tileX - MinimapMarkerLayout::CellX(poses[owner]));
                const uint16_t blendName = blendCells_[owner][nextBank[owner]][localTile];
                const uint16_t blendIndex = static_cast<uint16_t>(
                    (blendName & 0x0fffu) - cellBaseOffset_);
                volatile uint8_t* blend = cells_ + static_cast<uint32_t>(blendIndex) * 32u;
                const uint16_t basePattern = static_cast<uint16_t>(baseMap_[artIndex] & 0x0fffu);
                if (basePattern == 0u)
                {
                    for (uint8_t byte = 0u; byte < 32u; ++byte) blend[byte] = 0u;
                }
                else
                {
                    const uint16_t baseIndex = static_cast<uint16_t>(basePattern - cellBaseOffset_);
                    const volatile uint8_t* ground = cells_ + static_cast<uint32_t>(baseIndex) * 32u;
                    for (uint8_t byte = 0u; byte < 32u; ++byte) blend[byte] = ground[byte];
                }
                for (uint8_t markerIndex = 0u; markerIndex < kMarkerCount; ++markerIndex)
                {
                    if (!MinimapMarkerLayout::CoversTile(poses[markerIndex], tileX, tileY)) continue;
                    OverlayMarkerTile(blend, tileX, tileY, markerIndex, poses[markerIndex]);
                }
                map_[mapIndex] = blendName;
            }
        for (uint8_t i = 0u; i < kMarkerCount; ++i)
            if (poses[i].visible) markers_[i].blendBank = nextBank[i];
        markersDirty_ = false;
    }

    void OverlayMarkerTile(volatile uint8_t* blend, uint8_t tileX, uint8_t tileY,
                           uint8_t markerIndex, const MinimapMarkerLayout::Pose& pose)
    {
        for (uint8_t y = 0u; y < 8u; ++y)
        {
            const int16_t spriteY = static_cast<int16_t>(tileY * 8u + y) - pose.pixelY;
            if (spriteY < 0 || spriteY >= 16) continue;
            for (uint8_t x = 0u; x < 8u; ++x)
            {
                const int16_t spriteX = static_cast<int16_t>(tileX * 8u + x) - pose.pixelX;
                if (spriteX < 0 || spriteX >= 16) continue;
                uint8_t color = 0u;
                if (markerIndex == 0u)
                {
                    const uint8_t arrowTile = static_cast<uint8_t>(
                        (spriteY >> 3u) * 2u + (spriteX >> 3u));
                    const uint16_t arrowName = markerCells_[pose.direction][arrowTile];
                    if ((arrowName & 0x0fffu) == 0u) continue;
                    const uint16_t arrowIndex = static_cast<uint16_t>(
                        (arrowName & 0x0fffu) - cellBaseOffset_);
                    const uint8_t packed = cells_[static_cast<uint32_t>(arrowIndex) * 32u +
                        (spriteY & 7u) * 4u + ((spriteX & 7u) >> 1u)];
                    color = (spriteX & 1) ? (packed & 0x0fu) : (packed >> 4u);
                }
                else
                {
                    // Small diamond for an NPC, centered on exactly the same
                    // map coordinate as the player's directional arrow.
                    const int16_t dx = spriteX - 7;
                    const int16_t dy = spriteY - 7;
                    const int16_t distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                    if (distance <= 3)
                        color = distance <= 1 ? 14u : (markerIndex == 1u ? 3u : 1u);
                }
                if (color == 0u) continue;
                const uint8_t byteIndex = static_cast<uint8_t>(y * 4u + (x >> 1u));
                const uint8_t previous = blend[byteIndex];
                blend[byteIndex] = (x & 1u)
                    ? static_cast<uint8_t>((previous & 0xf0u) | color)
                    : static_cast<uint8_t>((previous & 0x0fu) | (color << 4u));
            }
        }
    }

    bool LoadLookup(SRL::Cd::File& file)
    {
        const size_t bytes = static_cast<size_t>(file.Size.Bytes);
        if (bytes < kLookupHeaderBytes || bytes > sizeof(lookupFile_)) { status_ = 3; return false; }
        if (!file.Open()) { status_ = 7; return false; }
        const int32_t read = file.Read(static_cast<int32_t>(bytes), lookupFile_);
        file.Close();
        if (read != static_cast<int32_t>(bytes)) { status_ = 7; return false; }
        if (lookupFile_[0] != 'M' || lookupFile_[1] != 'P' ||
            lookupFile_[2] != 'L' || lookupFile_[3] != '3' ||
            ReadLe16(lookupFile_ + 4u) != 3u)
        { status_ = 3; return false; }
        segmentCount_ = ReadLe16(lookupFile_ + 6u);
        if (segmentCount_ == 0u || segmentCount_ > kMaximumSegments ||
            bytes != kLookupHeaderBytes + static_cast<size_t>(segmentCount_) * 2u)
        { status_ = 3; return false; }
        yawOffsetCentideg_ = static_cast<int16_t>(ReadLe16(lookupFile_ + 8u));
        yawSense_ = static_cast<int8_t>(lookupFile_[10u]);
        if ((yawSense_ != 1 && yawSense_ != -1) || lookupFile_[11u] != 0u)
        { status_ = 3; return false; }
        for (uint16_t i = 0; i < segmentCount_; ++i)
        {
            if (lookupFile_[kLookupHeaderBytes + i * 2u] > 127u ||
                lookupFile_[kLookupHeaderBytes + i * 2u + 1u] > 127u)
            { status_ = 3; return false; }
        }
        return true;
    }

    bool LoadImage(SRL::Cd::File& file)
    {
        const size_t bytes = static_cast<size_t>(file.Size.Bytes);
        if (bytes < 18u + 17u * 4u + kTgaWidth * kTgaHeight || bytes > 80u * 1024u)
        { status_ = 4; return false; }
        if (SRL::Memory::CartRam::GetReport().FreeSize < bytes + 40u * 1024u)
        { status_ = 5; return false; }

        uint8_t* tga = static_cast<uint8_t*>(SRL::Memory::CartRam::Malloc(bytes));
        if (!tga) { status_ = 5; return false; }
        if (!file.Open())
        {
            SRL::Memory::CartRam::Free(tga);
            status_ = 7;
            return false;
        }
        const int32_t read = file.Read(static_cast<int32_t>(bytes), tga);
        file.Close();
        if (read != static_cast<int32_t>(bytes))
        {
            SRL::Memory::CartRam::Free(tga);
            status_ = 7;
            return false;
        }

        const uint16_t paletteCount = ReadLe16(tga + 5u);
        const uint32_t pixelOffset = 18u + tga[0] + static_cast<uint32_t>(paletteCount) * 4u;
        if (tga[1] != 1u || tga[2] != 1u || ReadLe16(tga + 3u) != 0u ||
            paletteCount < 2u || paletteCount > 17u || tga[7] != 32u ||
            ReadLe16(tga + 12u) != kTgaWidth || ReadLe16(tga + 14u) != kTgaHeight ||
            tga[16] != 8u || (tga[17] & 0x10u) != 0u || pixelOffset > bytes ||
            bytes - pixelOffset < kTgaWidth * kTgaHeight)
        {
            SRL::Memory::CartRam::Free(tga);
            status_ = 4;
            return false;
        }

        uint8_t remap[256]{};
        SRL::Types::HighColor colors[16]{};
        uint8_t nextColor = 1u;
        const uint8_t* palette = tga + 18u + tga[0];
        for (uint16_t i = 0; i < paletteCount; ++i)
        {
            const uint8_t* entry = palette + static_cast<uint32_t>(i) * 4u;
            const bool magentaMatte = entry[2] > 150u && entry[0] > 150u && entry[1] < 90u;
            if (entry[3] == 0u || magentaMatte) continue;
            if (nextColor >= 16u)
            {
                SRL::Memory::CartRam::Free(tga);
                status_ = 4;
                return false;
            }
            remap[i] = nextColor;
            colors[nextColor++] = SRL::Types::HighColor::FromRGB555(
                static_cast<uint8_t>(entry[2] >> 3u),
                static_cast<uint8_t>(entry[1] >> 3u),
                static_cast<uint8_t>(entry[0] >> 3u));
        }

        const size_t pixelBytes = kTgaWidth * kTgaHeight;
        uint8_t* packed = static_cast<uint8_t*>(SRL::Memory::CartRam::Malloc(pixelBytes / 2u));
        if (!packed)
        {
            SRL::Memory::CartRam::Free(tga);
            status_ = 5;
            return false;
        }
        const bool topOrigin = (tga[17] & 0x20u) != 0u;
        const uint8_t* source = tga + pixelOffset;
        for (uint16_t y = 0; y < kTgaHeight; ++y)
        {
            const uint8_t* row = source + static_cast<uint32_t>(topOrigin ? y : kTgaHeight - 1u - y) * kTgaWidth;
            uint8_t* dst = packed + static_cast<uint32_t>(y) * (kTgaWidth / 2u);
            for (uint16_t x = 0; x < kTgaWidth / 2u; ++x)
            {
                const uint8_t a = row[x * 2u];
                const uint8_t b = row[x * 2u + 1u];
                if (a >= paletteCount || b >= paletteCount)
                {
                    SRL::Memory::CartRam::Free(packed);
                    SRL::Memory::CartRam::Free(tga);
                    status_ = 4;
                    return false;
                }
                dst[x] = static_cast<uint8_t>((remap[a] << 4u) | remap[b]);
            }
        }

        // Build one true 8x8 VDP2 page. The authored map occupies 16x16 cells;
        // the eight arrow patterns and per-marker nine-cell blend banks stay offscreen.
        static constexpr uint16_t kMaxCells =
            1u + kImageCells * kImageCells + 8u * 4u + kBlendCellCount;
        uint8_t* cells = static_cast<uint8_t*>(SRL::Memory::CartRam::Malloc(kMaxCells * 32u));
        uint16_t* names = static_cast<uint16_t*>(SRL::Memory::CartRam::Malloc(kMapWidth * kMapWidth * 2u));
        if (!cells || !names)
        {
            if (cells) SRL::Memory::CartRam::Free(cells);
            if (names) SRL::Memory::CartRam::Free(names);
            SRL::Memory::CartRam::Free(packed);
            SRL::Memory::CartRam::Free(tga);
            status_ = 5;
            return false;
        }
        memset(cells, 0, 32u);
        memset(names, 0, kMapWidth * kMapWidth * 2u);
        uint16_t cellCount = 1u;
        for (uint16_t y = 0; y < kImageCells; ++y)
        {
            for (uint16_t x = 0; x < kMapWidth / 2u; ++x)
            {
                const bool mapArt = x < kImageCells;
                const bool arrowArt = x >= kImageCells && y < 2u;
                if (!mapArt && !arrowArt) continue;
                uint8_t* cell = cells + static_cast<size_t>(cellCount) * 32u;
                bool nonempty = false;
                for (uint16_t row = 0; row < 8u; ++row)
                {
                    const uint8_t* source = packed +
                        static_cast<size_t>(y * 8u + row) * (kTgaWidth / 2u) + x * 4u;
                    for (uint16_t byte = 0; byte < 4u; ++byte)
                    {
                        cell[row * 4u + byte] = source[byte];
                        nonempty |= source[byte] != 0u;
                    }
                }
                const uint16_t pattern = nonempty ? cellCount++ : 0u;
                if (mapArt)
                    names[(kMapY + y) * kMapWidth + kMapX + x] = pattern;
                else
                    names[(kMarkerAtlasY + y) * kMapWidth + x] = pattern;
            }
        }
        for (uint8_t tile = 0u; tile < kBlendCellCount; ++tile)
        {
            memset(cells + static_cast<size_t>(cellCount) * 32u, 0, 32u);
            names[kBlendAtlasY * kMapWidth + tile] = cellCount++;
        }
        SRL::Memory::CartRam::Free(packed);
        SRL::Memory::CartRam::Free(tga);
        MapTilemap tile(cells, names, colors, static_cast<int32_t>(cellCount * 32u));
        auto info = tile.GetInfo();

        const uint32_t mapFreeB0 = SRL::VDP2::VRAM::GetAvailable(SRL::VDP2::VramBank::B0);
        const uint32_t mapFreeB1 = SRL::VDP2::VRAM::GetAvailable(SRL::VDP2::VramBank::B1);
        uint32_t maxCellFree = 0u;
        for (uint8_t bank = 0; bank < 4u; ++bank)
        {
            const uint32_t freeBytes = SRL::VDP2::VRAM::GetAvailable(static_cast<SRL::VDP2::VramBank>(bank));
            if (freeBytes > maxCellFree) maxCellFree = freeBytes;
        }
        if ((mapFreeB0 < 8192u && mapFreeB1 < 8192u) ||
            maxCellFree < static_cast<uint32_t>(info.CellByteSize) + 4096u)
        {
            SRL::Memory::CartRam::Free(cells);
            SRL::Memory::CartRam::Free(names);
            status_ = 5;
            return false;
        }

        SRL::VDP2::NBG1::LoadTilemap(tile);
        SRL::Memory::CartRam::Free(cells);
        SRL::Memory::CartRam::Free(names);
        map_ = static_cast<volatile uint16_t*>(SRL::VDP2::NBG1::GetMapAddress());
        cells_ = static_cast<volatile uint8_t*>(SRL::VDP2::NBG1::GetCellAddress());
        if (reinterpret_cast<uint32_t>(map_) < VDP2_VRAM_A0 ||
            reinterpret_cast<uint32_t>(cells_) < VDP2_VRAM_A0)
        { map_ = nullptr; cells_ = nullptr; status_ = 5; return false; }
        cellBaseOffset_ = static_cast<uint16_t>(
            SRL::VDP2::NBG1::GetCellOffset(info, SRL::VDP2::NBG1::GetCellAddress()));
        if (static_cast<uint32_t>(cellBaseOffset_) + cellCount > 0x1000u)
        { map_ = nullptr; cells_ = nullptr; status_ = 5; return false; }

        for (uint8_t y = 0u; y < kImageCells; ++y)
            for (uint8_t x = 0u; x < kImageCells; ++x)
                baseMap_[y * kImageCells + x] =
                    map_[(kMapY + y) * kMapWidth + kMapX + x];

        for (uint8_t direction = 0; direction < 8u; ++direction)
        {
            const uint16_t x = static_cast<uint16_t>(kMarkerAtlasX + direction * 2u);
            const uint16_t base = static_cast<uint16_t>(kMarkerAtlasY * kMapWidth + x);
            markerCells_[direction][0] = map_[base];
            markerCells_[direction][1] = map_[base + 1u];
            markerCells_[direction][2] = map_[base + kMapWidth];
            markerCells_[direction][3] = map_[base + kMapWidth + 1u];
        }
        for (uint8_t marker = 0u; marker < kMarkerCount; ++marker)
            for (uint8_t bank = 0u; bank < kBlendBanksPerMarker; ++bank)
                for (uint8_t tile = 0u; tile < kBlendTiles; ++tile)
                {
                    const uint16_t atlasIndex = static_cast<uint16_t>(
                        (marker * kBlendBanksPerMarker + bank) * kBlendTiles + tile);
                    blendCells_[marker][bank][tile] =
                        map_[kBlendAtlasY * kMapWidth + atlasIndex];
                }
        SRL::VDP2::NBG1::SetPriority(SRL::VDP2::Priority::Layer7);
        SRL::VDP2::NBG1::TransparentEnable();
        SRL::VDP2::NBG1::ScrollEnable();
        return true;
    }

    uint8_t lookupFile_[kLookupHeaderBytes + kMaximumSegments * 2u]{};
    volatile uint16_t* map_ = nullptr;
    volatile uint8_t* cells_ = nullptr;
    uint16_t baseMap_[kImageCells * kImageCells]{};
    MarkerState markers_[kMarkerCount]{};
    uint16_t markerCells_[8u][4u]{};
    uint16_t blendCells_[kMarkerCount][kBlendBanksPerMarker][kBlendTiles]{};
    uint16_t cellBaseOffset_ = 0u;
    uint16_t segmentCount_ = 0u;
    int16_t yawOffsetCentideg_ = 0;
    int8_t yawSense_ = 1;
    bool ready_ = false;
    bool markersDirty_ = false;
    uint8_t status_ = 0u;

    static constexpr uint16_t kMarkerAtlasX = 16u;
    static constexpr uint16_t kMarkerAtlasY = 32u;
    static constexpr uint16_t kBlendAtlasY = 34u;
    static_assert((kMapY + kImageCells) * 8u <= 224u,
                  "the complete minimap must fit in the shortest NTSC viewport");
    static_assert(kMarkerAtlasY * 8u >= 224u,
                  "arrow pattern names must stay below the visible viewport");
    static_assert(kBlendAtlasY < kMapWidth,
                  "blend pattern names must remain inside the tilemap page");
};
