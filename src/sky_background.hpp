#pragma once

#include <srl.hpp>
#include "srl_tga.hpp"
#include "srl_tilemap_interfaces.hpp"

struct SkyBackground
{
    SRL::Tilemap::Interfaces::Bmp2Tile* tile = nullptr;
    SRL::Math::Types::Vector2D scroll = SRL::Math::Types::Vector2D(SRL::Math::Types::Fxp::Convert(0), SRL::Math::Types::Fxp::Convert(0));
    SRL::Math::Types::Fxp mapWidth = SRL::Math::Types::Fxp::Convert(512);
    SRL::Math::Types::Fxp mapHeight = SRL::Math::Types::Fxp::Convert(256);
    SRL::Math::Types::Fxp yawFactor = SRL::Math::Types::Fxp(0.10f);
    SRL::Math::Types::Fxp pitchFactor = SRL::Math::Types::Fxp(0.08f);
    SRL::Math::Types::Fxp drift = SRL::Math::Types::Fxp::Convert(0);
    SRL::Math::Types::Fxp driftStep = SRL::Math::Types::Fxp(0.02f);
    SRL::Math::Types::Fxp scaleX = SRL::Math::Types::Fxp(1.0f);
    SRL::Math::Types::Fxp scaleY = SRL::Math::Types::Fxp(1.0f);
    SRL::Math::Types::Fxp invScaleX = SRL::Math::Types::Fxp(1.0f);
    SRL::Math::Types::Fxp invScaleY = SRL::Math::Types::Fxp(1.0f);
    bool loaded = false;
    char lastPath[64] = {0};
    char lastState[16] = "idle";
    uint16_t lastWidth = 0;
    uint16_t lastHeight = 0;
    uint16_t lastColorMode = 0;

    void UpdateDiag(const char* state,
                    const char* path,
                    uint16_t width = 0,
                    uint16_t height = 0,
                    uint16_t colorMode = 0)
    {
        lastWidth = width;
        lastHeight = height;
        lastColorMode = colorMode;

        size_t i = 0;
        if (state)
        {
            while (i + 1 < sizeof(lastState) && state[i] != '\0')
            {
                lastState[i] = state[i];
                ++i;
            }
        }
        lastState[i] = '\0';

        i = 0;
        if (path)
        {
            while (i + 1 < sizeof(lastPath) && path[i] != '\0')
            {
                lastPath[i] = path[i];
                ++i;
            }
        }
        lastPath[i] = '\0';
    }

    ~SkyBackground()
    {
        delete tile;
    }

    bool Load(const char* const* paths, size_t count)
    {
        SRL::Tilemap::Interfaces::Bmp2Tile* oldTile = tile;
        const bool hadLoaded = loaded;
        char oldPath[sizeof(lastPath)] = {0};
        const uint16_t oldWidth = lastWidth;
        const uint16_t oldHeight = lastHeight;
        const uint16_t oldColorMode = lastColorMode;
        for (size_t i = 0; i + 1 < sizeof(oldPath) && lastPath[i] != '\0'; ++i)
        {
            oldPath[i] = lastPath[i];
        }

        UpdateDiag("start", "");
        bool sawConcreteFailure = false;

        auto tryLoadFile = [&](const char* candidatePath, SRL::Cd::File& skyFile) -> bool
        {
            const bool exists = skyFile.Exists();
            const int32_t size = skyFile.Size.Bytes;
            if (!exists || size <= 0)
            {
                if (!sawConcreteFailure)
                {
                    UpdateDiag("miss", candidatePath ? candidatePath : "");
                }
                return false;
            }

            // Decode in Low Work RAM to avoid High Work RAM spikes with larger TGAs.
            SRL::Bitmap::TGA* skyBmp = lwnew SRL::Bitmap::TGA(&skyFile);
            if (skyBmp == nullptr)
            {
                sawConcreteFailure = true;
                UpdateDiag("oom_tga", candidatePath ? candidatePath : "");
                return false;
            }

            const auto skyInfo = skyBmp->GetInfo();
            if (skyInfo.Width == 0 || skyInfo.Height == 0 || skyBmp->GetData() == nullptr)
            {
                sawConcreteFailure = true;
                UpdateDiag("bad_tga",
                           candidatePath ? candidatePath : "",
                           static_cast<uint16_t>(skyInfo.Width),
                           static_cast<uint16_t>(skyInfo.Height));
                delete skyBmp;
                return false;
            }

            SRL::Tilemap::Interfaces::Bmp2Tile* newTile = new SRL::Tilemap::Interfaces::Bmp2Tile(*skyBmp);
            delete skyBmp;
            if (!newTile)
            {
                sawConcreteFailure = true;
                UpdateDiag("no_tile", candidatePath ? candidatePath : "");
                return false;
            }

            const auto tileInfo = newTile->GetInfo();
            if (tileInfo.MapWidth == 0 ||
                tileInfo.MapHeight == 0 ||
                tileInfo.CellByteSize == 0 ||
                newTile->GetCellData() == nullptr ||
                newTile->GetMapData() == nullptr)
            {
                delete newTile;
                sawConcreteFailure = true;
                UpdateDiag("bad_tile", candidatePath ? candidatePath : "");
                return false;
            }

            // Bmp2Tile builds a fixed map page (typically 32x32 chars), while the source
            // image may occupy only a subset of that page. Replicate the valid region across
            // the full map to avoid empty gaps during horizontal/vertical scrolling.
            {
                uint16_t* mapData = static_cast<uint16_t*>(newTile->GetMapData());
                const uint16_t mapW = tileInfo.MapWidth;
                const uint16_t mapH = tileInfo.MapHeight;
                const uint16_t charPixels = tileInfo.CharSize ? 16u : 8u;

                uint16_t srcW = static_cast<uint16_t>(skyInfo.Width / charPixels);
                uint16_t srcH = static_cast<uint16_t>(skyInfo.Height / charPixels);
                if (srcW == 0u) srcW = 1u;
                if (srcH == 0u) srcH = 1u;
                if (srcW > mapW) srcW = mapW;
                if (srcH > mapH) srcH = mapH;

                if (srcW < mapW || srcH < mapH)
                {
                    for (uint16_t y = 0; y < mapH; ++y)
                    {
                        const uint16_t srcY = static_cast<uint16_t>(y % srcH);
                        uint16_t* dstRow = mapData + (static_cast<uint32_t>(y) * mapW);
                        const uint16_t* srcRow = mapData + (static_cast<uint32_t>(srcY) * mapW);
                        for (uint16_t x = 0; x < mapW; ++x)
                        {
                            dstRow[x] = srcRow[x % srcW];
                        }
                    }
                }
            }

            if (oldTile && oldTile != newTile)
            {
                delete oldTile;
            }
            tile = newTile;
            mapWidth = SRL::Math::Types::Fxp::Convert(tileInfo.MapWidth * (tileInfo.CharSize ? 16 : 8));
            mapHeight = SRL::Math::Types::Fxp::Convert(tileInfo.MapHeight * (tileInfo.CharSize ? 16 : 8));
            scaleX = SRL::Math::Types::Fxp(1.0f);
            scaleY = SRL::Math::Types::Fxp(1.0f);
            if (skyInfo.Width == 256)
            {
                scaleX = SRL::Math::Types::Fxp(2.0f);
            }
            if (skyInfo.Height == 128)
            {
                scaleY = SRL::Math::Types::Fxp(2.0f);
            }
            invScaleX = SRL::Math::Types::Fxp(1.0f) / scaleX;
            invScaleY = SRL::Math::Types::Fxp(1.0f) / scaleY;

            SRL::VDP2::NBG0::LoadTilemap(*tile);
            SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer6);
            SRL::Math::Types::Vector2D skyScale = SRL::Math::Types::Vector2D(scaleX, scaleY);
            SRL::VDP2::NBG0::SetScale(skyScale);
            SRL::VDP2::NBG0::ScrollEnable();
            SRL::VDP2::NBG1::ScrollDisable();

            loaded = true;
            UpdateDiag("ok",
                       candidatePath ? candidatePath : "",
                       static_cast<uint16_t>(skyInfo.Width),
                       static_cast<uint16_t>(skyInfo.Height),
                       static_cast<uint16_t>(skyInfo.ColorMode));
            return true;
        };

        // Comportamento equivalente ao fluxo antigo: tenta exatamente os
        // caminhos fornecidos pelo chamador, em ordem.
        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::File skyFile(paths[i]);
            if (tryLoadFile(paths[i], skyFile))
            {
                return true;
            }
        }

        // Mantém o sky anterior se a nova tentativa falhar.
        if (hadLoaded && oldTile)
        {
            tile = oldTile;
            loaded = true;
            UpdateDiag("keep", oldPath, oldWidth, oldHeight, oldColorMode);
            return true;
        }

        if (oldTile)
        {
            delete oldTile;
        }
        tile = nullptr;
        loaded = false;
        if (!sawConcreteFailure)
        {
            UpdateDiag("fail",
                       oldPath[0] ? oldPath : "",
                       oldWidth,
                       oldHeight,
                       oldColorMode);
        }
        return false;
    }

    void Update(int32_t yawDeg, int32_t pitchDeg)
    {
        if (!loaded || !tile || tile->GetCellData() == nullptr || tile->GetMapData() == nullptr)
        {
            loaded = false;
            UpdateDiag("invalid", lastPath, lastWidth, lastHeight, lastColorMode);
            return;
        }
        if (mapWidth.RawValue() == 0 || mapHeight.RawValue() == 0)
        {
            auto tileInfo = tile->GetInfo();
            mapWidth = SRL::Math::Types::Fxp::Convert(tileInfo.MapWidth * (tileInfo.CharSize ? 16 : 8));
            mapHeight = SRL::Math::Types::Fxp::Convert(tileInfo.MapHeight * (tileInfo.CharSize ? 16 : 8));
            if (mapWidth.RawValue() == 0) mapWidth = SRL::Math::Types::Fxp::Convert(512);
            if (mapHeight.RawValue() == 0) mapHeight = SRL::Math::Types::Fxp::Convert(256);
            SRL::VDP2::NBG0::LoadTilemap(*tile);
        }

        drift += driftStep;
        if (drift.RawValue() > mapWidth.RawValue())
        {
            drift -= mapWidth;
        }

        SRL::Math::Types::Fxp yawOffset = yawFactor * SRL::Math::Types::Fxp::Convert(yawDeg) * invScaleX;
        SRL::Math::Types::Fxp scrollX = drift + yawOffset;
        while (scrollX.RawValue() >= mapWidth.RawValue()) scrollX -= mapWidth;
        while (scrollX.RawValue() < 0) scrollX += mapWidth;

        scroll.X = scrollX;
        SRL::Math::Types::Fxp pitchOffset = pitchFactor * SRL::Math::Types::Fxp::Convert(pitchDeg) * invScaleY;
        SRL::Math::Types::Fxp scrollY = pitchOffset;
        while (scrollY.RawValue() >= mapHeight.RawValue()) scrollY -= mapHeight;
        while (scrollY.RawValue() < 0) scrollY += mapHeight;
        scroll.Y = scrollY;
        // Reassert NBG0 state every frame for robustness when other systems touch VDP2.
        SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer6);
        SRL::Math::Types::Vector2D skyScale = SRL::Math::Types::Vector2D(scaleX, scaleY);
        SRL::VDP2::NBG0::SetScale(skyScale);
        SRL::VDP2::NBG0::ScrollEnable();
        SRL::VDP2::NBG1::ScrollDisable();
        SRL::VDP2::NBG0::SetPosition(scroll);
    }
};
