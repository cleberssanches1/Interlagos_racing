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
    SRL::Math::Types::Fxp drift = SRL::Math::Types::Fxp::Convert(0);
    SRL::Math::Types::Fxp driftStep = SRL::Math::Types::Fxp(0.02f);
    bool loaded = false;

    ~SkyBackground()
    {
        delete tile;
    }

    bool Load(const char* const* paths, size_t count)
    {
        delete tile;
        tile = nullptr;
        loaded = false;

        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::File skyFile(paths[i]);
            if (!skyFile.Exists())
            {
                continue;
            }

            SRL::Debug::Print(1, 10, "Sky load: %s", paths[i]);
            SRL::Bitmap::TGA skyBmp(&skyFile);
            auto skyInfo = skyBmp.GetInfo();
            SRL::Debug::Print(1, 11, "Sky info: %u x %u mode %d pal %p",
                              skyInfo.Width, skyInfo.Height,
                              (int)skyInfo.ColorMode, skyInfo.Palette);

            tile = new SRL::Tilemap::Interfaces::Bmp2Tile(skyBmp);
            auto tileInfo = tile->GetInfo();
            SRL::Debug::Print(1, 12, "Sky tilemap: %ux%u char:%u map:%u cellBytes:%d",
                              tileInfo.MapWidth, tileInfo.MapHeight,
                              tileInfo.CharSize, tileInfo.MapMode, tileInfo.CellByteSize);

            mapWidth = SRL::Math::Types::Fxp::Convert(tileInfo.MapWidth * (tileInfo.CharSize ? 16 : 8));
            mapHeight = SRL::Math::Types::Fxp::Convert(tileInfo.MapHeight * (tileInfo.CharSize ? 16 : 8));

            SRL::VDP2::NBG0::LoadTilemap(*tile);
            SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer6); // acima do backcolor
            SRL::Math::Types::Vector2D skyScale = SRL::Math::Types::Vector2D(SRL::Math::Types::Fxp(1.0f), SRL::Math::Types::Fxp(1.0f));
            SRL::VDP2::NBG0::SetScale(skyScale);
            SRL::VDP2::NBG0::ScrollEnable();

            loaded = true;
            return true;
        }

        SRL::Debug::Print(1, 10, "Sky missing: skybox_1.tga");
        return false;
    }

    void Update(int32_t yawDeg)
    {
        if (!loaded || mapWidth.RawValue() == 0)
        {
            return;
        }

        drift += driftStep;
        if (drift.RawValue() > mapWidth.RawValue())
        {
            drift -= mapWidth;
        }

        SRL::Math::Types::Fxp yawOffset = yawFactor * SRL::Math::Types::Fxp::Convert(yawDeg);
        SRL::Math::Types::Fxp scrollX = drift + yawOffset;
        while (scrollX.RawValue() >= mapWidth.RawValue()) scrollX -= mapWidth;
        while (scrollX.RawValue() < 0) scrollX += mapWidth;

        scroll.X = scrollX;
        scroll.Y = SRL::Math::Types::Fxp::Convert(0);
        SRL::VDP2::NBG0::SetPosition(scroll);
    }
};
