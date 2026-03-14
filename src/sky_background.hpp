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
                // log removido
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
                // log removido
                return true;
            }
            return false;
        };

        // Primeiro tenta os caminhos informados pelo chamador (sempre a partir da raiz).
        SRL::Cd::ChangeDir((const char*)0);
        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::ChangeDir((const char*)0);
            SRL::Cd::File skyFile(paths[i]);
            const bool exists = skyFile.Exists();
            const int32_t size = skyFile.Size.Bytes;
            // log removido
            if (!exists || size <= 0)
            {
                continue;
            }

            SRL::Bitmap::TGA skyBmp(&skyFile);
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
            // log removido
            return true;
        }

        // Fallback robusto por diretorio + nome, independente de path textual completo.
        struct DirChain { const char* a; const char* b; };
        const DirChain dirChains[] = {
            { "DATA", nullptr },
            { "data", nullptr },
            { nullptr, nullptr },
            { "DATA", "ARQ_TGA" },
            { "data", "arq_tga" },
            { "ARQ_TGA", nullptr },
            { "arq_tga", nullptr },
            { nullptr, nullptr }
        };
        const char* names[] = { "SKYBOX_1.TGA", "SKYBOX_1.TGA;1", "skybox_1.tga", "skybox_1.tga;1" };
        for (const auto& chain : dirChains)
        {
            SRL::Cd::ChangeDir((const char*)0);
            if (chain.a) SRL::Cd::ChangeDir(chain.a);
            if (chain.b) SRL::Cd::ChangeDir(chain.b);
            for (size_t i = 0; i < (sizeof(names) / sizeof(names[0])); ++i)
            {
                SRL::Cd::File skyFile(names[i]);
                const bool exists = skyFile.Exists();
                const int32_t size = skyFile.Size.Bytes;
                // log removido
                if (!exists || size <= 0) continue;

                SRL::Bitmap::TGA skyBmp(&skyFile);
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
                // log removido
                SRL::Cd::ChangeDir((const char*)0);
                return true;
            }
        }
        SRL::Cd::ChangeDir((const char*)0);

        SRL::Cd::ChangeDir((const char*)0);
        const char* rootNames[] = {"SKYBOX_1.TGA", "SKYBOX_1.TGA;1", "skybox_1.tga", "skybox_1.tga;1"};
        if (tryLoadNames("ROOT", rootNames, sizeof(rootNames) / sizeof(rootNames[0])))
        {
            return true;
        }

        SRL::Cd::ChangeDir("DATA");
        const char* dataNames[] = {"SKYBOX_1.TGA", "SKYBOX_1.TGA;1", "skybox_1.tga", "skybox_1.tga;1"};
        if (tryLoadNames("DATA", dataNames, sizeof(dataNames) / sizeof(dataNames[0])))
        {
            SRL::Cd::ChangeDir((const char*)0);
            return true;
        }
        SRL::Cd::ChangeDir((const char*)0);

        // log removido
        return false;
    }

    void Update(int32_t yawDeg, int32_t pitchDeg)
    {
        if (!loaded || !tile)
        {
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
        // Reassert NBG0 state every frame for robustness when other systems touch VDP2.
        SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer6);
        SRL::Math::Types::Vector2D skyScale = SRL::Math::Types::Vector2D(SRL::Math::Types::Fxp(1.0f), SRL::Math::Types::Fxp(1.0f));
        SRL::VDP2::NBG0::SetScale(skyScale);
        SRL::VDP2::NBG0::ScrollEnable();
        SRL::VDP2::NBG1::ScrollDisable();
        SRL::VDP2::NBG0::SetPosition(scroll);
    }
};
