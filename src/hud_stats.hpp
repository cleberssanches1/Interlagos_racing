#pragma once

#include <srl.hpp>
#include "camera_controller.hpp"

struct HudStats
{
    void Init(uint32_t faceCount,
              uint32_t vertexCount,
              uint32_t meshCount,
              bool isSmooth,
              const SRL::Math::Types::Vector3D& modelCenter,
              const SRL::Math::Types::Vector3D& minV,
              const SRL::Math::Types::Vector3D& maxV)
    {
        SRL::Debug::Print(1, 1, "CAR1.NYA viewer");
        SRL::Debug::Print(1, 2, "Faces:%u Verts:%u Meshes:%u Smooth:%d", faceCount, vertexCount, meshCount, isSmooth ? 1 : 0);
        SRL::Debug::Print(1, 3, "Center: %d, %d, %d", modelCenter.X.As<int16_t>(), modelCenter.Y.As<int16_t>(), modelCenter.Z.As<int16_t>());
        SRL::Debug::Print(1, 6, "Min: %d %d %d", minV.X.As<int16_t>(), minV.Y.As<int16_t>(), minV.Z.As<int16_t>());
        SRL::Debug::Print(1, 7, "Max: %d %d %d", maxV.X.As<int16_t>(), maxV.Y.As<int16_t>(), maxV.Z.As<int16_t>());
        SRL::Debug::Print(1, 9, "Model pos: %d %d %d", modelCenter.X.As<int16_t>(), modelCenter.Y.As<int16_t>(), modelCenter.Z.As<int16_t>());
    }

    void Update(const Camera::State& cameraState,
                const SRL::Math::Types::Vector3D& modelOffset,
                const SRL::Math::Types::Vector3D& cameraLocation,
                const SRL::Math::Types::Vector3D& modelCenter)
    {
        SRL::Debug::Print(1, 4, "Offset: %d, %d, %d", modelOffset.X.As<int16_t>(), modelOffset.Y.As<int16_t>(), modelOffset.Z.As<int16_t>());
        SRL::Debug::Print(1, 5, "Cam: %d, %d, %d", cameraLocation.X.As<int16_t>(), cameraLocation.Y.As<int16_t>(), cameraLocation.Z.As<int16_t>());
        SRL::Debug::Print(1, 8, "Yaw:%u Pitch:%u R:%d", cameraState.yaw.RawValue(), cameraState.pitch.RawValue(), cameraState.radius.As<int16_t>());

        // VDP usage (live)
        auto bankFree = [&](int bank)->int32_t { return (int32_t)SRL::VDP2::VRAM::GetAvailable((SRL::VDP2::VramBank)bank); };
        SRL::Debug::Print(28, 20, "VDP2 A0:%5dK", (int)(bankFree(0) / 1024));
        SRL::Debug::Print(28, 21, "VDP2 A1:%5dK", (int)(bankFree(1) / 1024));
        SRL::Debug::Print(28, 22, "VDP2 B0:%5dK", (int)(bankFree(2) / 1024));
        SRL::Debug::Print(28, 23, "VDP2 B1:%5dK", (int)(bankFree(3) / 1024));
        SRL::Debug::Print(28, 24, "VDP1 VRAM:%4dK", (int)(SRL::VDP1::GetAvailableMemory() / 1024));
    }
};
