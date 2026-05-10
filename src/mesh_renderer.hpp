#pragma once

#include <srl.hpp>
#include <array>
#include <vector>
#include <cstddef>

#include "modelObject.hpp"

class MeshRenderer
{
public:
    struct LocalTransform
    {
        bool enabled{false};
        SRL::Math::Types::Vector3D pivot{0.0, 0.0, 0.0};
        SRL::Math::Types::Vector3D translation{0.0, 0.0, 0.0};
        SRL::Math::Types::Angle rotateX{SRL::Math::Types::Angle::FromDegrees(0.0f)};
        SRL::Math::Types::Angle rotateY{SRL::Math::Types::Angle::FromDegrees(0.0f)};
        SRL::Math::Types::Angle rotateZ{SRL::Math::Types::Angle::FromDegrees(0.0f)};
    };

    struct Config
    {
        SRL::Math::Types::Vector3D modelCenter{0.0, 0.0, 0.0};
        SRL::Math::Types::Vector3D lightDirection{0.0, 0.0, 1.0};
        std::array<size_t, 8> drawOrder{};
        size_t drawOrderCount{0};
        bool wireframeOnly{false};
        bool useBudget{true};
        bool rotateModelX180{false};
        bool rotateModelZ180{false};
    };

    MeshRenderer(ModelObject& model, bool isSmooth, const Config& cfg);

    void SetSkipMesh(size_t meshId);
    void SetScale(const SRL::Math::Types::Fxp& scale);
    void SetOffset(const SRL::Math::Types::Vector3D& offset);
    void SetLightDirection(const SRL::Math::Types::Vector3D& dir);
    void SetBodyAttitude(const SRL::Math::Types::Angle& pitch, const SRL::Math::Types::Angle& roll);
    void ClearMeshLocalTransforms();
    void SetMeshLocalTransform(size_t meshId, const LocalTransform& transform);

    void Render(const SRL::Math::Types::Vector3D& position,
                const SRL::Math::Types::Angle& yaw,
                bool logStats = false);

    const std::vector<SRL::Math::Types::Vector3D>& MeshCenters() const { return meshCenters_; }
    size_t LastMeshDrawn() const { return lastMeshId_; }

private:
    void ComputeMeshCenters();
    bool DrawMesh(size_t meshId);
    void ApplyTransform(const SRL::Math::Types::Vector3D& position,
                        const SRL::Math::Types::Angle& yaw);

    ModelObject& model_;
    bool isSmooth_;
    Config config_;
    SRL::Math::Types::Angle rotation_{SRL::Math::Types::Angle::FromDegrees(0)};
    SRL::Math::Types::Fxp scale_{SRL::Math::Types::Fxp::Convert(1.0f)};
    SRL::Math::Types::Vector3D offset_{0.0, 0.0, 0.0};
    size_t skipMeshId_{SIZE_MAX};
    size_t lastMeshId_{SIZE_MAX};
    std::vector<SRL::Math::Types::Vector3D> meshCenters_;
    std::vector<LocalTransform> meshLocalTransforms_;
    SRL::Math::Types::Angle bodyPitch_{SRL::Math::Types::Angle::FromDegrees(0.0f)};
    SRL::Math::Types::Angle bodyRoll_{SRL::Math::Types::Angle::FromDegrees(0.0f)};
};
