#pragma once

#include <srl.hpp>
#include "modelObject.hpp"
#include <array>

class CarRenderer
{
public:
    struct Config
    {
        SRL::Math::Types::Vector3D modelCenter;
        SRL::Math::Types::Vector3D lightDirection;
        std::array<size_t, 5> drawOrder;
        size_t drawOrderCount;
    };

    CarRenderer(ModelObject& car, bool isSmoothMesh, const Config& cfg)
        : car_(car), isSmooth_(isSmoothMesh), config_(cfg), rotY(SRL::Math::Types::Angle::FromDegrees(0)), rotStep(SRL::Math::Types::Angle::FromDegrees(0.0f))
    {
    }

    void Render()
    {
        SRL::Scene3D::PushMatrix();
        // Move model center to origin and flip X
        SRL::Math::Types::Vector3D modelOffset(-config_.modelCenter.X, -config_.modelCenter.Y, -config_.modelCenter.Z);
        SRL::Scene3D::Translate(modelOffset);
        SRL::Scene3D::RotateX(SRL::Math::Types::Angle::FromDegrees(180.0f));
        SRL::Scene3D::RotateY(rotY);

        for (size_t idx = 0; idx < config_.drawOrderCount; ++idx)
        {
            size_t meshId = config_.drawOrder[idx];
            if (meshId >= car_.GetMeshCount())
            {
                continue;
            }

            if (isSmooth_)
            {
                car_.Draw(meshId, config_.lightDirection);
            }
            else
            {
                car_.Draw(meshId);
            }
        }

        SRL::Scene3D::PopMatrix();
        rotY += rotStep;
    }

    SRL::Math::Types::Angle rotY;
    SRL::Math::Types::Angle rotStep;

private:
    ModelObject& car_;
    bool isSmooth_;
    Config config_;
};
