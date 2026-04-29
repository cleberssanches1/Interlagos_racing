#include "mesh_renderer.hpp"
#include "exception_stubs.hpp"

#include "sgl_poly_renderer.hpp"

MeshRenderer::MeshRenderer(ModelObject& model, bool isSmooth, const Config& cfg)
    : model_(model)
    , isSmooth_(isSmooth)
    , config_(cfg)
{
    ComputeMeshCenters();
}

void MeshRenderer::SetSkipMesh(size_t meshId)
{
    skipMeshId_ = meshId;
}

void MeshRenderer::SetScale(const SRL::Math::Types::Fxp& scale)
{
    scale_ = scale;
}

void MeshRenderer::SetOffset(const SRL::Math::Types::Vector3D& offset)
{
    offset_ = offset;
}

void MeshRenderer::SetLightDirection(const SRL::Math::Types::Vector3D& dir)
{
    config_.lightDirection = dir;
}

void MeshRenderer::Render(const SRL::Math::Types::Vector3D& position,
                          const SRL::Math::Types::Angle& yaw,
                          bool logStats)
{
    SRL::Scene3D::PushMatrix();
    SRL::Scene3D::Translate(position + offset_);
    if (config_.rotateModelX180)
    {
        SRL::Scene3D::RotateX(SRL::Math::Types::Angle::FromDegrees(180.0f));
    }
    SRL::Scene3D::RotateY(yaw);
    if (config_.rotateModelZ180)
    {
        SRL::Scene3D::RotateZ(SRL::Math::Types::Angle::FromDegrees(180.0f));
    }
    SRL::Scene3D::Scale(scale_);
    SRL::Scene3D::Translate(-config_.modelCenter);
    size_t drawnMesh = SIZE_MAX;

    for (size_t idx = 0; idx < config_.drawOrderCount; ++idx)
    {
        const size_t meshId = config_.drawOrder[idx];
        if (meshId >= model_.GetMeshCount()) continue;
        if (meshId == skipMeshId_) continue;

        if (DrawMesh(meshId))
        {
            drawnMesh = meshId;
            lastMeshId_ = meshId;
        }
    }

    if (logStats && drawnMesh != SIZE_MAX)
    {
        SRL::Debug::Print(1, 10, "MeshRenderer Render target:%d", (int)drawnMesh);
    }

    SRL::Scene3D::PopMatrix();
}

bool MeshRenderer::DrawMesh(size_t meshId)
{
    bool hasFaces = false;
    if (isSmooth_)
    {
        auto* mesh = model_.GetMesh<SRL::Types::SmoothMesh>(meshId);
        if (mesh && mesh->FaceCount > 0 && mesh->VertexCount > 0)
        {
            hasFaces = true;
            if (config_.sortPriorityBoost && mesh->Attributes)
            {
                // Force sort mode to Minimum (nearest vertex Z) so this mesh wins
                // any Z-sort tie against track polygons at the same depth.
                // Sort bits [1:0]: 0b01 = UseMin, 0b11 = UseCenter (default).
                constexpr uint8_t kSortModeMask = 0xFCu;
                constexpr uint8_t kSortModeMin =
                    static_cast<uint8_t>(SRL::Types::Attribute::SortMode::Minimum);
                for (size_t f = 0; f < mesh->FaceCount; ++f)
                {
                    mesh->Attributes[f].Sort =
                        static_cast<uint8_t>((mesh->Attributes[f].Sort & kSortModeMask) | kSortModeMin);
                }
            }
            model_.Draw(meshId, config_.lightDirection);
        }
    }
    else
    {
        auto* mesh = model_.GetMesh<SRL::Types::Mesh>(meshId);
        if (mesh && mesh->FaceCount > 0 && mesh->VertexCount > 0)
        {
            hasFaces = true;
            if (config_.sortPriorityBoost && mesh->Attributes)
            {
                constexpr uint8_t kSortModeMask = 0xFCu;
                constexpr uint8_t kSortModeMin =
                    static_cast<uint8_t>(SRL::Types::Attribute::SortMode::Minimum);
                for (size_t f = 0; f < mesh->FaceCount; ++f)
                {
                    mesh->Attributes[f].Sort =
                        static_cast<uint8_t>((mesh->Attributes[f].Sort & kSortModeMask) | kSortModeMin);
                }
            }
            model_.Draw(meshId);
        }
    }
    return hasFaces;
}

void MeshRenderer::ApplyTransform(const SRL::Math::Types::Vector3D& position,
                                  const SRL::Math::Types::Angle& yaw)
{
    SRL::Scene3D::Translate(position + offset_);
    if (config_.rotateModelX180)
    {
        SRL::Scene3D::RotateX(SRL::Math::Types::Angle::FromDegrees(180.0f));
    }
    SRL::Scene3D::RotateY(yaw);
    if (config_.rotateModelZ180)
    {
        SRL::Scene3D::RotateZ(SRL::Math::Types::Angle::FromDegrees(180.0f));
    }
}

void MeshRenderer::ComputeMeshCenters()
{
    const size_t count = model_.GetMeshCount();
    meshCenters_.assign(count, SRL::Math::Types::Vector3D(0.0, 0.0, 0.0));
    for (size_t i = 0; i < count; ++i)
    {
        SRL::Math::Types::Vector3D minV(32767, 32767, 32767);
        SRL::Math::Types::Vector3D maxV(-32768, -32768, -32768);
        if (isSmooth_)
        {
            auto* mesh = model_.GetMesh<SRL::Types::SmoothMesh>(i);
            if (!mesh) continue;
            for (size_t v = 0; v < mesh->VertexCount; ++v)
            {
                const auto& p = mesh->Vertices[v];
                minV.X = SRL::Math::Min(minV.X, p.X);
                minV.Y = SRL::Math::Min(minV.Y, p.Y);
                minV.Z = SRL::Math::Min(minV.Z, p.Z);
                maxV.X = SRL::Math::Max(maxV.X, p.X);
                maxV.Y = SRL::Math::Max(maxV.Y, p.Y);
                maxV.Z = SRL::Math::Max(maxV.Z, p.Z);
            }
        }
        else
        {
            auto* mesh = model_.GetMesh<SRL::Types::Mesh>(i);
            if (!mesh) continue;
            for (size_t v = 0; v < mesh->VertexCount; ++v)
            {
                const auto& p = mesh->Vertices[v];
                minV.X = SRL::Math::Min(minV.X, p.X);
                minV.Y = SRL::Math::Min(minV.Y, p.Y);
                minV.Z = SRL::Math::Min(minV.Z, p.Z);
                maxV.X = SRL::Math::Max(maxV.X, p.X);
                maxV.Y = SRL::Math::Max(maxV.Y, p.Y);
                maxV.Z = SRL::Math::Max(maxV.Z, p.Z);
            }
        }
        meshCenters_[i] = SRL::Math::Types::Vector3D(
            (minV.X + maxV.X) / 2,
            (minV.Y + maxV.Y) / 2,
            (minV.Z + maxV.Z) / 2);
    }
}
