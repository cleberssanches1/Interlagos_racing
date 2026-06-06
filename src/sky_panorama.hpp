#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <srl.hpp>
#include "srl_tga.hpp"
#include "srl_tilemap_interfaces.hpp"

struct SkyPanorama
{
    static constexpr SRL::Math::Types::Fxp kFxpZero = SRL::Math::Types::Fxp::BuildRaw(0);
    static constexpr SRL::Math::Types::Fxp kFxpOne = SRL::Math::Types::Fxp::BuildRaw(1 << 16);
    static constexpr int kExpectedWidthPx = 512;
    static constexpr int kExpectedHeightPx = 64;
    static constexpr int kTargetSkyHeightPx = 120;
    static constexpr int kScreenWidthPx = 320;

    SRL::Tilemap::Interfaces::Bmp2Tile* tile = nullptr;
    bool loaded = false;
    bool yawAnchorSet = false;
    int32_t yawAnchorDeg = 0;
    int32_t mapWidthPx = kExpectedWidthPx;
    int32_t scrollWrapWidthPx = kExpectedWidthPx;
    int32_t startOffsetPx = (kExpectedWidthPx - kScreenWidthPx) / 2;
    int16_t scrollY = 0;
    SRL::Math::Types::Fxp scaleX = kFxpOne;
    SRL::Math::Types::Fxp scaleY = kFxpOne;
    SRL::Math::Types::Vector2D scroll = SRL::Math::Types::Vector2D(
        kFxpZero,
        kFxpZero);

    ~SkyPanorama()
    {
        if (tile)
        {
            delete tile;
            tile = nullptr;
        }
    }

    void ResetYawAnchor()
    {
        yawAnchorSet = false;
        yawAnchorDeg = 0;
    }

    bool Load(const char* const* paths, size_t count)
    {
        loaded = false;
        ResetYawAnchor();

        if (tile)
        {
            delete tile;
            tile = nullptr;
        }

        SRL::Bitmap::TGA* tga = nullptr;
        const char* loadedPath = nullptr;
        if (paths && count > 0)
        {
            for (size_t i = 0; i < count && !tga; ++i)
            {
                const char* candidate = paths[i];
                if (!candidate || candidate[0] == '\0') continue;
                SRL::Cd::ChangeDir((const char*)0);
                SRL::Cd::File file(candidate);
                if (!file.Exists() || file.Size.Bytes <= 0) continue;
                SRL::Bitmap::TGA* probe = lwnew SRL::Bitmap::TGA(&file);
                if (!probe) continue;
                const auto info = probe->GetInfo();
                if (info.Width == kExpectedWidthPx && info.Height == kExpectedHeightPx)
                {
                    tga = probe;
                    loadedPath = candidate;
                    break;
                }
                delete probe;
            }
        }
        if (!tga)
        {
            static constexpr std::array<const char*, 19> kFallback = {
                "cd/data/ceup.tga",
                "cd/data/CEUP.TGA",
                "/CD/DATA/CEUP.TGA",
                "/CD/DATA/CEUP.TGA;1",
                "/DATA/CEUP.TGA",
                "/DATA/CEUP.TGA;1",
                "data/CEUP.TGA",
                "DATA/CEUP.TGA",
                "DATA/CEUP.TGA;1",
                "CEUP.TGA",
                "ceup.tga",
                "ceup.tga;1",
                "CEUP.TGA;1",
                "cd/data/SKY.tga",
                "cd/data/SKY1.tga",
                "cd/data/skybox_1.tga",
                "SKY.tga",
                "SKY1.tga",
                "skybox_1.tga"
            };
            for (size_t i = 0; i < kFallback.size() && !tga; ++i)
            {
                SRL::Cd::ChangeDir((const char*)0);
                SRL::Cd::File file(kFallback[i]);
                if (!file.Exists() || file.Size.Bytes <= 0) continue;
                SRL::Bitmap::TGA* probe = lwnew SRL::Bitmap::TGA(&file);
                if (!probe) continue;
                const auto info = probe->GetInfo();
                if (info.Width == kExpectedWidthPx && info.Height == kExpectedHeightPx)
                {
                    tga = probe;
                    loadedPath = kFallback[i];
                    break;
                }
                delete probe;
            }
        }
        if (!tga)
        {
            SRL::Debug::Print(0, 3, "SPN dim 0x0");
            return false;
        }

        // Scale the 64px panorama vertically to fill the visible sky area.
        scaleY = SRL::Math::Types::Fxp::BuildRaw(
            static_cast<int32_t>((kTargetSkyHeightPx << 16) / kExpectedHeightPx));

        tile = new SRL::Tilemap::Interfaces::Bmp2Tile(*tga, 1, SRL::Memory::Zone::LWRam);
        delete tga;
        if (!tile || !tile->GetCellData() || !tile->GetMapData())
        {
            SRL::Debug::Print(0, 3, "SPN fail tile");
            return false;
        }

        const auto tileInfo = tile->GetInfo();
        const int32_t charPixels = static_cast<int32_t>(tileInfo.CharSize ? 16 : 8);
        mapWidthPx = static_cast<int32_t>(tileInfo.MapWidth * charPixels);
        if (mapWidthPx <= 0) mapWidthPx = kExpectedWidthPx;
        scrollWrapWidthPx = mapWidthPx;
        startOffsetPx = (mapWidthPx - kScreenWidthPx) / 2;
        if (startOffsetPx < 0) startOffsetPx = 0;

        if (tile->GetMapData())
        {
            uint16_t* mapData = static_cast<uint16_t*>(tile->GetMapData());
            const int32_t mapW = static_cast<int32_t>(tileInfo.MapWidth);
            const int32_t mapH = static_cast<int32_t>(tileInfo.MapHeight);
            if (mapW > 0 && mapH > 0)
            {
                int32_t minX = mapW;
                int32_t minY = mapH;
                int32_t maxX = -1;
                int32_t maxY = -1;

                for (int32_t y = 0; y < mapH; ++y)
                {
                    const uint16_t* row = mapData + (y * mapW);
                    for (int32_t x = 0; x < mapW; ++x)
                    {
                        if (row[x] == 0) continue;
                        if (x < minX) minX = x;
                        if (x > maxX) maxX = x;
                        if (y < minY) minY = y;
                        if (y > maxY) maxY = y;
                    }
                }

                if (maxX >= minX && maxY >= minY)
                {
                    const int32_t srcW = (maxX - minX) + 1;
                    const int32_t srcH = (maxY - minY) + 1;
                    std::vector<uint16_t> src;
                    src.resize(static_cast<size_t>(srcW * srcH));

                    for (int32_t y = 0; y < srcH; ++y)
                    {
                        const uint16_t* srcRow = mapData + ((minY + y) * mapW) + minX;
                        uint16_t* dstRow = src.data() + (y * srcW);
                        for (int32_t x = 0; x < srcW; ++x)
                        {
                            dstRow[x] = srcRow[x];
                        }
                    }

                    for (int32_t y = 0; y < mapH; ++y)
                    {
                        uint16_t* dstRow = mapData + (y * mapW);
                        const int32_t srcY = (y < srcH) ? y : (srcH - 1);
                        const uint16_t* srcRow = src.data() + (srcY * srcW);
                        for (int32_t x = 0; x < mapW; ++x)
                        {
                            dstRow[x] = srcRow[x % srcW];
                        }
                    }

                    scrollY = 0;
                    scrollWrapWidthPx = srcW * charPixels;
                    if (scrollWrapWidthPx <= 0) scrollWrapWidthPx = mapWidthPx;
                    startOffsetPx = (scrollWrapWidthPx - kScreenWidthPx) / 2;
                    if (startOffsetPx < 0) startOffsetPx = 0;
                    SRL::Debug::Print(0, 3, "SPN src %dx%d", srcW, srcH);
                }
                else
                {
                    SRL::Debug::Print(0, 3, "SPN map empty");
                }
            }
        }

        SRL::VDP2::NBG0::LoadTilemap(*tile);
        SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer6);
        SRL::Math::Types::Vector2D skyScale = SRL::Math::Types::Vector2D(scaleX, scaleY);
        SRL::VDP2::NBG0::SetScale(skyScale);
        SRL::VDP2::NBG0::ScrollEnable();
        SRL::VDP2::NBG1::ScrollDisable();

        SRL::Debug::Print(0, 3, "SPN ok mw:%d sw:%d", mapWidthPx, scrollWrapWidthPx);
        (void)loadedPath;
        loaded = true;
        return true;
    }

    void Update(int32_t yawDeg, int32_t viewYawDeg, int32_t pitchDeg)
    {
        (void)pitchDeg;
        if (!loaded || !tile) return;
        if (!tile->GetCellData() || !tile->GetMapData()) return;

        const int32_t yawNorm = NormalizeDeg360(yawDeg + viewYawDeg);
        if (!yawAnchorSet)
        {
            yawAnchorSet = true;
            yawAnchorDeg = yawNorm;
        }

        const int32_t yawDelta = NormalizeDegSigned(yawNorm - yawAnchorDeg);
        const int32_t offsetPx = (yawDelta * scrollWrapWidthPx) / 360;

        int32_t x = startOffsetPx + offsetPx;
        if (scrollWrapWidthPx > 0)
        {
            x %= scrollWrapWidthPx;
            if (x < 0) x += scrollWrapWidthPx;
        }
        else
        {
            x = 0;
        }

        scroll.X = SRL::Math::Types::Fxp::Convert(static_cast<int16_t>(x));
        scroll.Y = SRL::Math::Types::Fxp::Convert(scrollY);
        SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer6);
        SRL::Math::Types::Vector2D skyScale = SRL::Math::Types::Vector2D(scaleX, scaleY);
        SRL::VDP2::NBG0::SetScale(skyScale);
        SRL::VDP2::NBG0::ScrollEnable();
        SRL::VDP2::NBG1::ScrollDisable();
        SRL::VDP2::NBG0::SetPosition(scroll);
    }

private:
    static int32_t NormalizeDeg360(int32_t deg)
    {
        deg %= 360;
        if (deg < 0) deg += 360;
        return deg;
    }

    static int32_t NormalizeDegSigned(int32_t deg)
    {
        deg = NormalizeDeg360(deg);
        if (deg > 180) deg -= 360;
        return deg;
    }

    // Path probing is handled in Load() so we can validate dimensions before accepting a file.
};
