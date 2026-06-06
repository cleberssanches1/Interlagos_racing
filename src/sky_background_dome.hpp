#pragma once

#include <srl.hpp>
#include "srl_tga.hpp"
#include "srl_tilemap_interfaces.hpp"

// C??u em RBG0 com rota????o simples (tilemap), seguindo o yaw da c??mera
struct SkyBackgroundDome
{
    static constexpr SRL::Math::Types::Fxp kFxpZero = SRL::Math::Types::Fxp::BuildRaw(0);
    static constexpr SRL::Math::Types::Fxp kFxpHalf = SRL::Math::Types::Fxp::BuildRaw(1 << 15);
    static constexpr SRL::Math::Types::Fxp kFxpOne = SRL::Math::Types::Fxp::BuildRaw(1 << 16);

    SRL::Tilemap::Interfaces::Bmp2Tile* tile = nullptr;
    SRL::Math::Types::Fxp yawFactor = kFxpOne;
    SRL::Math::Types::Fxp pitchFactor = kFxpOne;
    SRL::Math::Types::Fxp pitchOffsetFactor = kFxpHalf;
    SRL::Math::Types::Fxp viewYawFactor = kFxpOne;
    SRL::Math::Types::Fxp lateralOffsetFactor = kFxpZero;
    SRL::Math::Types::Fxp baseOffsetX = kFxpZero;
    SRL::Math::Types::Fxp baseOffsetY = SRL::Math::Types::Fxp::Convert(static_cast<int16_t>(-128));
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
            // SRL::Debug::Print(1, 10, "RBG sky load: %s", paths[i]);
            SRL::Bitmap::TGA skyBmp(&skyFile);
            auto skyInfo = skyBmp.GetInfo();
            // SRL::Debug::Print(1, 11, "RBG sky info: %u x %u mode %d", skyInfo.Width, skyInfo.Height, (int)skyInfo.ColorMode);

            tile = new SRL::Tilemap::Interfaces::Bmp2Tile(skyBmp);
            auto tileInfo = tile->GetInfo();
            // SRL::Debug::Print(1, 12, "RBG sky tilemap: %ux%u char:%u map:%u cellBytes:%d",
            //                   tileInfo.MapWidth, tileInfo.MapHeight,
            //                   tileInfo.CharSize, tileInfo.MapMode, tileInfo.CellByteSize);

            SRL::VDP2::RBG0::LoadTilemap(*tile);
            SRL::VDP2::RBG0::SetPriority(SRL::VDP2::Priority::Layer6);
            SRL::VDP2::RBG0::ScrollEnable();
            loaded = true;
            return true;
        }
        // SRL::Debug::Print(1, 10, "RBG sky missing");
        return false;
    }

    void Update(int32_t yawDeg, int32_t viewYawDeg, int32_t viewPitchDeg)
    {
        if (!loaded) return;

        const int16_t yawDeg16 = static_cast<int16_t>(yawDeg);
        const int16_t viewYawDeg16 = static_cast<int16_t>(viewYawDeg);
        const int16_t viewPitchDeg16 = static_cast<int16_t>(viewPitchDeg);
        const SRL::Math::Types::Fxp yawFxp = SRL::Math::Types::Fxp::Convert(yawDeg16);
        const SRL::Math::Types::Fxp viewYawFxp = SRL::Math::Types::Fxp::Convert(viewYawDeg16);
        const SRL::Math::Types::Fxp viewPitchFxp = SRL::Math::Types::Fxp::Convert(viewPitchDeg16);

        slPushMatrix();
        slUnitMatrix(nullptr);
        SRL::Math::Types::Fxp vOffset = pitchOffsetFactor * viewPitchFxp;
        SRL::Math::Types::Fxp hOffset = lateralOffsetFactor * viewYawFxp;
        slTranslate((baseOffsetX + hOffset).RawValue(), (baseOffsetY + vOffset).RawValue(), 0);
        auto rotY = SRL::Math::Types::Angle::FromDegrees((yawFxp * yawFactor) + (viewYawFxp * viewYawFactor));
        auto rotX = SRL::Math::Types::Angle::FromDegrees(viewPitchFxp * pitchFactor);
        slRotY(rotY.RawValue());
        slRotX(rotX.RawValue());
        SRL::VDP2::RBG0::SetCurrentTransform();
        slPopMatrix();
    }
};
