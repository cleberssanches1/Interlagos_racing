#pragma once

#include <cstdint>

#include <srl.hpp>

#include "hud_stats.hpp"

class HudSystem
{
public:
    // Initialize HUD baseline model statistics.
    void Initialize(uint32_t faceCount,
                    uint32_t vertexCount,
                    uint32_t meshCount,
                    bool isSmooth,
                    const SRL::Math::Types::Vector3D& modelCenter,
                    const SRL::Math::Types::Vector3D& minV,
                    const SRL::Math::Types::Vector3D& maxV)
    {
        stats_.Init(faceCount, vertexCount, meshCount, isSmooth, modelCenter, minV, maxV);
    }

    // Update camera-facing HUD state each frame.
    void Update(const Camera::State& cameraState,
                const SRL::Math::Types::Vector3D& modelOffset,
                const SRL::Math::Types::Vector3D& cameraLocation,
                const SRL::Math::Types::Vector3D& carWorldPosition)
    {
        stats_.Update(cameraState, modelOffset, cameraLocation, carWorldPosition);
    }

    // Print periodic system memory and VDP1 usage logs.
    void PresentPeriodicFrameStats(uint32_t frameCounter, bool logTrack, bool logCar, uint32_t faceCount, uint32_t vertexCount) const
    {
        if ((frameCounter & 63) != 0) return;

        const int32_t hwrFree = SRL::Memory::CartRam::GetFreeSpace();
        const int32_t hwrTotal = 4 * 1024 * 1024;
        const int32_t hwrUsed  = hwrTotal - hwrFree;
        const int32_t vdp1TexCount = SRL::VDP1::GetTextureCount();
        const size_t vdp1Free  = SRL::VDP1::GetAvailableMemory();
        const size_t vdp1Used  = SRL::VDP1::GetUsedMemory();
        const size_t vdp1Total = vdp1Used + vdp1Free;
        const uint32_t vdp1Pct = (vdp1Total > 0) ? static_cast<uint32_t>((vdp1Used * 100) / vdp1Total) : 0;
        const int32_t hwrPct10 = (hwrTotal > 0) ? (hwrUsed * 1000 / hwrTotal) : 0;

        if (logCar)
        {
            SRL::Debug::Print(0, 24, "Car faces:%u verts:%u", (unsigned)faceCount, (unsigned)vertexCount);
        }
        if (logTrack) SRL::Debug::Print(0, 25, "HWR used:%d free:%d", hwrUsed, hwrFree);
        if (logTrack) SRL::Debug::Print(0, 26, "HWR pct:%d.%d%%", hwrPct10 / 10, hwrPct10 % 10);
        if (logTrack)
        {
            SRL::Debug::Print(0, 27, "VDP1 textures:%d", vdp1TexCount);
            SRL::Debug::Print(0, 28, "VDP1 mem used:%u free:%u pct:%u%%",
                              (unsigned)vdp1Used, (unsigned)vdp1Free, vdp1Pct);
        }
        SRL::Debug::Print(2, 31, "VDP1 mem used:%u free:%u pct:%u%%",
                          (unsigned)vdp1Used, (unsigned)vdp1Free, vdp1Pct);
    }

private:
    HudStats stats_{};
};
