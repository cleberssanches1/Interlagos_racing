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
        (void)faceCount;
        (void)vertexCount;
        (void)meshCount;
        (void)isSmooth;
        (void)modelCenter;
        (void)minV;
        (void)maxV;
    }

    void Update(const Camera::State& cameraState,
                const SRL::Math::Types::Vector3D& modelOffset,
                const SRL::Math::Types::Vector3D& cameraLocation,
                const SRL::Math::Types::Vector3D& modelCenter)
    {
        (void)cameraState;
        (void)modelOffset;
        (void)cameraLocation;
        (void)modelCenter;
    }
};
