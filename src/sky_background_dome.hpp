#pragma once

#include <srl.hpp>
#include "srl_tga.hpp"
#include "srl_tilemap_interfaces.hpp"

// C??u em RBG0 com rota????o simples (tilemap), seguindo o yaw da c??mera
struct SkyBackgroundDome
{
    SRL::Tilemap::Interfaces::Bmp2Tile* tile = nullptr;
    SRL::Math::Types::Fxp yawFactor = SRL::Math::Types::Fxp(1.0f);
    SRL::Math::Types::Fxp pitchFactor = SRL::Math::Types::Fxp(1.0f);
    SRL::Math::Types::Fxp pitchOffsetFactor = SRL::Math::Types::Fxp(0.5f);
    SRL::Math::Types::Fxp viewYawFactor = SRL::Math::Types::Fxp(1.0f);
    SRL::Math::Types::Fxp lateralOffsetFactor = SRL::Math::Types::Fxp(0.0f);
    SRL::Math::Types::Fxp baseOffsetX = SRL::Math::Types::Fxp::Convert(0);
    SRL::Math::Types::Fxp baseOffsetY = SRL::Math::Types::Fxp::Convert(-128);
    bool loaded = false;

    ~SkyBackgroundDome()
    {
        delete tile;
    }

    void Configure(const SRL::Math::Types::Fxp& newYawFactor,
                   const SRL::Math::Types::Fxp& offsetX,
                   const SRL::Math::Types::Fxp& offsetY,
                   const SRL::Math::Types::Fxp& newPitchFactor,
                   const SRL::Math::Types::Fxp& newPitchOffsetFactor,
                   const SRL::Math::Types::Fxp& newViewYawFactor,
                   const SRL::Math::Types::Fxp& newLateralOffsetFactor)
    {
        yawFactor = newYawFactor;
        baseOffsetX = offsetX;
        baseOffsetY = offsetY;
        pitchFactor = newPitchFactor;
        pitchOffsetFactor = newPitchOffsetFactor;
        viewYawFactor = newViewYawFactor;
        lateralOffsetFactor = newLateralOffsetFactor;
    }

    bool Load(const char* const* paths, size_t count)
    {
        delete tile;
        tile = nullptr;
        loaded = false;

        SRL::VDP2::RBG0::SetRotationMode(SRL::VDP2::RotationMode::OneAxis);

        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::File skyFile(paths[i]);
            if (!skyFile.Exists()) continue;
            SRL::Debug::Print(1, 10, "RBG sky load: %s", paths[i]);
            SRL::Bitmap::TGA skyBmp(&skyFile);
            auto skyInfo = skyBmp.GetInfo();
            SRL::Debug::Print(1, 11, "RBG sky info: %u x %u mode %d", skyInfo.Width, skyInfo.Height, (int)skyInfo.ColorMode);

            tile = new SRL::Tilemap::Interfaces::Bmp2Tile(skyBmp);
            auto tileInfo = tile->GetInfo();
            SRL::Debug::Print(1, 12, "RBG sky tilemap: %ux%u char:%u map:%u cellBytes:%d",
                              tileInfo.MapWidth, tileInfo.MapHeight,
                              tileInfo.CharSize, tileInfo.MapMode, tileInfo.CellByteSize);

            SRL::VDP2::RBG0::LoadTilemap(*tile);
            SRL::VDP2::RBG0::SetPriority(SRL::VDP2::Priority::Layer6);
            SRL::VDP2::RBG0::ScrollEnable();
            loaded = true;
            return true;
        }
        SRL::Debug::Print(1, 10, "RBG sky missing");
        return false;
    }

    void Update(int32_t yawDeg, int32_t viewYawDeg, int32_t viewPitchDeg)
    {
        if (!loaded) return;

        slPushMatrix();
        slUnitMatrix(nullptr);
        SRL::Math::Types::Fxp vOffset = pitchOffsetFactor * SRL::Math::Types::Fxp::Convert(viewPitchDeg);
        SRL::Math::Types::Fxp hOffset = lateralOffsetFactor * SRL::Math::Types::Fxp::Convert(viewYawDeg);
        slTranslate((baseOffsetX + hOffset).RawValue(), (baseOffsetY + vOffset).RawValue(), 0);
        auto rotY = SRL::Math::Types::Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(yawDeg) * yawFactor + SRL::Math::Types::Fxp::Convert(viewYawDeg) * viewYawFactor);
        auto rotX = SRL::Math::Types::Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(viewPitchDeg) * pitchFactor);
        slRotY(rotY.RawValue());
        slRotX(rotX.RawValue());
        SRL::VDP2::RBG0::SetCurrentTransform();
        slPopMatrix();
    }
};
