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
    bool loaded = false;

    ~SkyBackground()
    {
        delete tile;
    }

    bool Load(const char* const* paths, size_t count)
    {
        (void)paths;
        (void)count;
        delete tile;
        tile = nullptr;
        loaded = false;

        auto tryLoadNames = [&](const char* dirLabel, const char* const* names, size_t nameCount) -> bool
        {
            for (size_t i = 0; i < nameCount; ++i)
            {
                SRL::Cd::File skyFile(names[i]);
                const bool exists = skyFile.Exists();
                const int32_t size = skyFile.Size.Bytes;
                SRL::Debug::Print(1, 11, "Sky %s[%u] ex:%d sz:%ld", dirLabel, (unsigned)i, exists ? 1 : 0, (long)size);
                if (!exists || size <= 0)
                {
                    continue;
                }

                SRL::Bitmap::TGA skyBmp(&skyFile);
                auto skyInfo = skyBmp.GetInfo();
                (void)skyInfo;

                tile = new SRL::Tilemap::Interfaces::Bmp2Tile(skyBmp);
                auto tileInfo = tile->GetInfo();

                mapWidth = SRL::Math::Types::Fxp::Convert(tileInfo.MapWidth * (tileInfo.CharSize ? 16 : 8));
                mapHeight = SRL::Math::Types::Fxp::Convert(tileInfo.MapHeight * (tileInfo.CharSize ? 16 : 8));

                SRL::VDP2::NBG0::LoadTilemap(*tile);
                SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer6);
                SRL::Math::Types::Vector2D skyScale = SRL::Math::Types::Vector2D(SRL::Math::Types::Fxp(1.0f), SRL::Math::Types::Fxp(1.0f));
                SRL::VDP2::NBG0::SetScale(skyScale);
                SRL::VDP2::NBG0::ScrollEnable();
                SRL::VDP2::NBG1::ScrollDisable();

                loaded = true;
                SRL::Debug::Print(1, 11, "Sky loaded %s[%u]", dirLabel, (unsigned)i);
                return true;
            }
            return false;
        };

        SRL::Cd::ChangeDir((const char*)0);
        const char* rootNames[] = {"SKYBOX_1.TGA", "SKYBOX_1.TGA;1", "skybox_1.tga", "skybox_1.tga;1"};
        if (tryLoadNames("ROOT", rootNames, sizeof(rootNames) / sizeof(rootNames[0])))
        {
            return true;
        }

        SRL::Cd::ChangeDir("DATA");
        {
            SRL::Cd::File carProbe("CAR1.NYA");
            SRL::Cd::File segProbe("SEG_001.NYA");
            SRL::Debug::Print(1, 11, "Sky probe DATA CAR1 ex:%d sz:%ld", carProbe.Exists() ? 1 : 0, (long)carProbe.Size.Bytes);
            SRL::Debug::Print(1, 12, "Sky probe DATA SEG1 ex:%d sz:%ld", segProbe.Exists() ? 1 : 0, (long)segProbe.Size.Bytes);
        }
        const char* dataNames[] = {"SKYBOX_1.TGA", "SKYBOX_1.TGA;1", "skybox_1.tga", "skybox_1.tga;1"};
        if (tryLoadNames("DATA", dataNames, sizeof(dataNames) / sizeof(dataNames[0])))
        {
            SRL::Cd::ChangeDir((const char*)0);
            return true;
        }
        SRL::Cd::ChangeDir((const char*)0);

        SRL::Debug::Print(1, 11, "Sky missing on all candidate paths");
        return false;
    }

    void Update(int32_t yawDeg, int32_t pitchDeg)
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
        SRL::Math::Types::Fxp pitchOffset = pitchFactor * SRL::Math::Types::Fxp::Convert(pitchDeg);
        SRL::Math::Types::Fxp scrollY = pitchOffset;
        while (scrollY.RawValue() >= mapHeight.RawValue()) scrollY -= mapHeight;
        while (scrollY.RawValue() < 0) scrollY += mapHeight;
        scroll.Y = scrollY;
        SRL::VDP2::NBG0::SetPosition(scroll);
    }
};
