#include "mesh_renderer.hpp"
#include "exception_stubs.hpp"

#include "sgl_poly_renderer.hpp"

MeshRenderer::MeshRenderer(ModelObject& model, bool isSmooth, const Config& cfg)
    : model_(model)
    , isSmooth_(isSmooth)
    , config_(cfg)
{
    ComputeMeshCenters();
    meshLocalTransforms_.assign(model_.GetMeshCount(), LocalTransform{});
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

void MeshRenderer::SetBodyAttitude(const SRL::Math::Types::Angle& pitch,
                                   const SRL::Math::Types::Angle& roll)
{
    bodyPitch_ = pitch;
    bodyRoll_ = roll;
}

void MeshRenderer::ClearMeshLocalTransforms()
{
    if (meshLocalTransforms_.empty())
    {
        meshLocalTransforms_.assign(model_.GetMeshCount(), LocalTransform{});
        return;
    }

    for (size_t i = 0; i < meshLocalTransforms_.size(); ++i)
    {
        meshLocalTransforms_[i].enabled = false;
        meshLocalTransforms_[i].translation = SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        meshLocalTransforms_[i].rotateX = SRL::Math::Types::Angle::FromDegrees(0.0f);
        meshLocalTransforms_[i].rotateY = SRL::Math::Types::Angle::FromDegrees(0.0f);
        meshLocalTransforms_[i].rotateZ = SRL::Math::Types::Angle::FromDegrees(0.0f);
    }
}

void MeshRenderer::SetMeshLocalTransform(size_t meshId, const LocalTransform& transform)
{
    if (meshId >= model_.GetMeshCount()) return;
    if (meshLocalTransforms_.size() < model_.GetMeshCount())
    {
        meshLocalTransforms_.assign(model_.GetMeshCount(), LocalTransform{});
    }
    meshLocalTransforms_[meshId] = transform;
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
    SRL::Scene3D::RotateX(bodyPitch_);
    SRL::Scene3D::RotateZ(bodyRoll_);
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

        SRL::Scene3D::PushMatrix();
        if (meshId < meshLocalTransforms_.size())
        {
            const LocalTransform& local = meshLocalTransforms_[meshId];
            if (local.enabled)
            {
                SRL::Scene3D::Translate(local.pivot + local.translation);
                SRL::Scene3D::RotateY(local.rotateY);
                SRL::Scene3D::RotateX(local.rotateX);
                SRL::Scene3D::RotateZ(local.rotateZ);
                SRL::Scene3D::Translate(-local.pivot);
            }
        }

        if (DrawMesh(meshId))
        {
            drawnMesh = meshId;
            lastMeshId_ = meshId;
        }

        SRL::Scene3D::PopMatrix();
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
            model_.Draw(meshId, config_.lightDirection);
        }
    }
    else
    {
        auto* mesh = model_.GetMesh<SRL::Types::Mesh>(meshId);
        if (mesh && mesh->FaceCount > 0 && mesh->VertexCount > 0)
        {
            hasFaces = true;
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
    meshLocalTransforms_.assign(count, LocalTransform{});
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
