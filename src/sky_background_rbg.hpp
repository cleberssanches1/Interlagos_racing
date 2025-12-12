#pragma once

#include <srl.hpp>
#include "srl_tga.hpp"
#include "srl_tilemap_interfaces.hpp"

struct SkyBackgroundRbg
{
    SRL::Tilemap::Interfaces::Bmp2Tile* tile = nullptr;
    SRL::Math::Types::Vector2D scroll = SRL::Math::Types::Vector2D(SRL::Math::Types::Fxp::Convert(0), SRL::Math::Types::Fxp::Convert(0));
    SRL::Math::Types::Fxp yawFactor = SRL::Math::Types::Fxp(0.25f); // sensibilidade do roll em relacao ao yaw
    SRL::Math::Types::Fxp drift = SRL::Math::Types::Fxp::Convert(0);
    SRL::Math::Types::Fxp driftStep = SRL::Math::Types::Fxp(0.02f);
    SRL::Math::Types::Fxp baseOffsetX = SRL::Math::Types::Fxp::Convert(0);
    SRL::Math::Types::Fxp baseOffsetY = SRL::Math::Types::Fxp::Convert(-128);
    bool loaded = false;

    ~SkyBackgroundRbg()
    {
        delete tile;
    }

    void Configure(const SRL::Math::Types::Fxp& newYawFactor,
                     const SRL::Math::Types::Fxp& newDriftStep,
                     const SRL::Math::Types::Fxp& offsetX,
                     const SRL::Math::Types::Fxp& offsetY)
    {
        yawFactor = newYawFactor;
        driftStep = newDriftStep;
        baseOffsetX = offsetX;
        baseOffsetY = offsetY;
    }

    void SetOffsets(const SRL::Math::Types::Fxp& offsetX, const SRL::Math::Types::Fxp& offsetY)
    {
        baseOffsetX = offsetX;
        baseOffsetY = offsetY;
    }

    bool Load(const char* const* paths, size_t count)
    {
        delete tile;
        tile = nullptr;
        loaded = false;

        // RBG0 deve ser configurado antes das demais camadas
        SRL::VDP2::RBG0::SetRotationMode(SRL::VDP2::RotationMode::OneAxis); // sem VRAM extra

        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::File skyFile(paths[i]);
            if (!skyFile.Exists())
            {
                continue;
            }

            SRL::Debug::Print(1, 10, "RBG sky load: %s", paths[i]);
            SRL::Bitmap::TGA skyBmp(&skyFile);
            auto skyInfo = skyBmp.GetInfo();
            SRL::Debug::Print(1, 11, "RBG sky info: %u x %u mode %d pal %p",
                              skyInfo.Width, skyInfo.Height,
                              (int)skyInfo.ColorMode, skyInfo.Palette);

            tile = new SRL::Tilemap::Interfaces::Bmp2Tile(skyBmp);
            auto tileInfo = tile->GetInfo();
            SRL::Debug::Print(1, 12, "RBG sky tilemap: %ux%u char:%u map:%u cellBytes:%d",
                              tileInfo.MapWidth, tileInfo.MapHeight,
                              tileInfo.CharSize, tileInfo.MapMode, tileInfo.CellByteSize);

            SRL::VDP2::RBG0::LoadTilemap(*tile);
            SRL::VDP2::RBG0::SetPriority(SRL::VDP2::Priority::Layer6); // abaixo de sprites, acima do backcolor
            SRL::VDP2::RBG0::ScrollEnable();
            loaded = true;
            return true;
        }

        SRL::Debug::Print(1, 10, "RBG sky missing");
        return false;
    }

    void Update(int32_t yawDeg)
    {
        if (!loaded)
        {
            return;
        }

        // drift base
        drift += driftStep;
        if (drift.RawValue() > SRL::Math::Types::Fxp::Convert(1024).RawValue()) // valor arbitrario
        {
            drift = SRL::Math::Types::Fxp::Convert(0);
        }

        // Apply simple yaw rotation and slight drift
        slPushMatrix();
        slUnitMatrix(nullptr);

        // small parallax scroll before rotation
        scroll.X = baseOffsetX + drift;
        scroll.Y = baseOffsetY;
        slTranslate(scroll.X.RawValue(), scroll.Y.RawValue(), 0);

        auto rotAngle = SRL::Math::Types::Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(yawDeg) * yawFactor);
        slRotY(rotAngle.RawValue());

        SRL::VDP2::RBG0::SetCurrentTransform();
        slPopMatrix();
    }
};
