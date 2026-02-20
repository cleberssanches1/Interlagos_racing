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
    void PresentPeriodicFrameStats(uint32_t frameCounter,
                                   bool logTrack,
                                   bool logCar,
                                   uint32_t faceCount,
                                   uint32_t vertexCount,
                                   uint32_t submittedTrackFaces,
                                   uint32_t submittedCarFaces)
    {
        const int32_t hwrFree = SRL::Memory::CartRam::GetFreeSpace();
        const int32_t hwrTotal = 4 * 1024 * 1024;
        const int32_t hwrUsed  = hwrTotal - hwrFree;
        const size_t vdp1HeapUsed = SRL::VDP1::GetUsedMemory();
        const size_t vdp1HeapFree = SRL::VDP1::GetAvailableMemory();
        const size_t vdp1HeapTotal = vdp1HeapUsed + vdp1HeapFree;
        const uint32_t vdp1HeapPct = (vdp1HeapTotal > 0) ? static_cast<uint32_t>((vdp1HeapUsed * 100u) / vdp1HeapTotal) : 0;
        if (!heapPctFilterInit_)
        {
            heapPctFiltered_ = vdp1HeapPct;
            heapPctFilterInit_ = true;
        }
        else
        {
            // Smooth heap usage display so HP% remains stable during camera/clip changes.
            heapPctFiltered_ = static_cast<uint32_t>((heapPctFiltered_ * 7u + vdp1HeapPct) / 8u);
        }
        constexpr uint32_t kVdp1FaceCostBytes = 64;
        constexpr uint32_t kVdp1FrameBudgetBytes = 512u * 1024u;
        const uint32_t submittedFacesNow = submittedTrackFaces + submittedCarFaces;
        const uint32_t vdp1UsedNow = submittedFacesNow * kVdp1FaceCostBytes;
        const uint32_t vdp1ClampedNow = (vdp1UsedNow > kVdp1FrameBudgetBytes) ? kVdp1FrameBudgetBytes : vdp1UsedNow;
        const uint32_t vdp1PctNow = (kVdp1FrameBudgetBytes > 0) ? static_cast<uint32_t>((vdp1ClampedNow * 100u) / kVdp1FrameBudgetBytes) : 0;
        accumSubmittedFaces_ += submittedFacesNow;
        accumSamples_ += 1;
        if (vdp1ClampedNow > peakVdp1Used_) peakVdp1Used_ = vdp1ClampedNow;

        // Always show a compact VDP1 usage line every frame on multiple debug layers.
        SRL::Debug::Print(0, 23, "VDP1 FR%%:%u HP%%:%u F:%u", (unsigned)vdp1PctNow, (unsigned)heapPctFiltered_, (unsigned)submittedFacesNow);
        SRL::Debug::Print(1, 1,  "VDP1 FR%%:%u HP%%:%u F:%u", (unsigned)vdp1PctNow, (unsigned)heapPctFiltered_, (unsigned)submittedFacesNow);

        if ((frameCounter & 63) != 0) return;

        const uint32_t avgFaces = (accumSamples_ > 0) ? static_cast<uint32_t>(accumSubmittedFaces_ / accumSamples_) : 0;
        const uint32_t avgUsed = avgFaces * kVdp1FaceCostBytes;
        const uint32_t vdp1Used = (avgUsed > kVdp1FrameBudgetBytes) ? kVdp1FrameBudgetBytes : avgUsed;
        const uint32_t vdp1Free = (kVdp1FrameBudgetBytes > vdp1Used) ? (kVdp1FrameBudgetBytes - vdp1Used) : 0;
        const uint32_t vdp1Pct = (kVdp1FrameBudgetBytes > 0) ? static_cast<uint32_t>((vdp1Used * 100u) / kVdp1FrameBudgetBytes) : 0;
        const uint32_t peakPct = (kVdp1FrameBudgetBytes > 0) ? static_cast<uint32_t>((peakVdp1Used_ * 100u) / kVdp1FrameBudgetBytes) : 0;
        const int32_t hwrPct10 = (hwrTotal > 0) ? (hwrUsed * 1000 / hwrTotal) : 0;

        if (logCar)
        {
            SRL::Debug::Print(0, 24, "Car faces:%u verts:%u", (unsigned)faceCount, (unsigned)vertexCount);
        }
        if (logTrack) SRL::Debug::Print(0, 25, "HWR used:%d free:%d", hwrUsed, hwrFree);
        if (logTrack) SRL::Debug::Print(0, 26, "HWR pct:%d.%d%%", hwrPct10 / 10, hwrPct10 % 10);
        if (logTrack)
        {
            SRL::Debug::Print(0, 27, "VDP1 faces N:%u A:%u", (unsigned)submittedFacesNow, (unsigned)avgFaces);
            SRL::Debug::Print(0, 28, "VDP1 use A:%u P:%u", (unsigned)vdp1Used, (unsigned)peakVdp1Used_);
            SRL::Debug::Print(0, 29, "VDP1 %% A:%u P:%u", (unsigned)vdp1Pct, (unsigned)peakPct);
        }
        SRL::Debug::Print(0, 30, "VDP1 free:%u", (unsigned)vdp1Free);

        accumSubmittedFaces_ = 0;
        accumSamples_ = 0;
        peakVdp1Used_ = 0;
    }

private:
    HudStats stats_{};
    uint64_t accumSubmittedFaces_ = 0;
    uint32_t accumSamples_ = 0;
    uint32_t peakVdp1Used_ = 0;
    uint32_t heapPctFiltered_ = 0;
    bool heapPctFilterInit_ = false;
};
