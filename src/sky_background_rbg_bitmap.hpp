#pragma once

#include <srl.hpp>
#include "srl_tga.hpp"

// Bitmap em RBG0 (512x256 8bpp) para eliminar tiling
struct SkyBackgroundRbgBitmap
{
    SRL::Bitmap::TGA* bmp = nullptr;
    SRL::CRAM::Palette palette;
    bool loaded = false;

    ~SkyBackgroundRbgBitmap()
    {
        delete bmp;
    }

    bool Load(const char* const* paths, size_t count)
    {
        delete bmp;
        bmp = nullptr;
        loaded = false;

        // Seleciona arquivo
        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::File skyFile(paths[i]);
            if (!skyFile.Exists()) continue;

            bmp = new SRL::Bitmap::TGA(&skyFile);
            auto info = bmp->GetInfo();
            if (info.Width != 512 || info.Height != 256 || info.ColorMode != SRL::CRAM::TextureColorMode::Paletted256)
            {
                SRL::Debug::Print(1, 10, "RBG bmp invalido %ux%u mode %d", info.Width, info.Height, (int)info.ColorMode);
                delete bmp; bmp = nullptr; continue;
            }

            // aloca paleta 256 cores
            int palId = SRL::CRAM::GetFreeBank(SRL::CRAM::TextureColorMode::Paletted256);
            if (palId < 0)
            {
                SRL::Debug::Print(1, 10, "Sem CRAM para sky bmp");
                delete bmp; bmp = nullptr; return false;
            }
            SRL::CRAM::SetBankUsedState(palId, SRL::CRAM::TextureColorMode::Paletted256, true);
            palette = SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted256, palId);
            palette.Load((SRL::Types::HighColor*)info.Palette, 256);

            // endere?o de VRAM (usar A0 base)
            uint8_t* vram = (uint8_t*)VDP2_VRAM_A0;
            // copia bitmap (512*256 bytes)
            slDMACopy((void*)bmp->GetData(), vram, info.Width * info.Height);

            // inicializa bitmap RBG0
            slInitBitMapRbg0(BM_512x256, vram);
            slBitMapRbg0(COL_TYPE_256, palId, vram);
            slPriorityRbg0(6);
            slScrPosRbg0(0,0);
            loaded = true;
            return true;
        }

        SRL::Debug::Print(1, 10, "RBG bmp missing");
        return false;
    }

    void Update(int32_t /*yawDeg*/, int32_t /*viewYawDeg*/, int32_t /*viewPitchDeg*/)
    {
        if (!loaded) return;
        // Mantém transformações zeradas (identidade) para evitar distorção
        SRL::VDP2::RBG0::SetCurrentTransform();
    }
};
