#include "mesh_renderer.hpp"
#include "exception_stubs.hpp"

#include "sgl_poly_renderer.hpp"
#include <algorithm>

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
        meshLocalTransforms_[i].rotateX = kAngleZero;
        meshLocalTransforms_[i].rotateY = kAngleZero;
        meshLocalTransforms_[i].rotateZ = kAngleZero;
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
    lastRenderFaceCount_ = 0;
    lastRenderMeshCount_ = 0;
    SRL::Scene3D::PushMatrix();
    SRL::Scene3D::Translate(position + offset_);
    if (config_.rotateModelX180)
    {
        SRL::Scene3D::RotateX(kAngleHalfTurn);
    }
    SRL::Scene3D::RotateY(yaw);
    SRL::Scene3D::RotateX(bodyPitch_);
    SRL::Scene3D::RotateZ(bodyRoll_);
    if (config_.rotateModelZ180)
    {
        SRL::Scene3D::RotateZ(kAngleHalfTurn);
    }
    SRL::Scene3D::Scale(scale_);
    SRL::Scene3D::Translate(-config_.modelCenter);
    size_t drawnMesh = SIZE_MAX;

    auto drawMeshWithTransform = [&](size_t meshId)
    {
        if (meshId >= model_.GetMeshCount()) return;
        if (meshId == skipMeshId_) return;

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

        const uint32_t facesDrawn = DrawMesh(meshId);
        if (facesDrawn > 0u)
        {
            drawnMesh = meshId;
            lastMeshId_ = meshId;
            ++lastRenderMeshCount_;
            lastRenderFaceCount_ += facesDrawn;
        }

        SRL::Scene3D::PopMatrix();
    };

    const size_t configuredOrderCount = std::min(config_.drawOrderCount, config_.drawOrder.size());
    if (configuredOrderCount > 0u)
    {
        for (size_t idx = 0; idx < configuredOrderCount; ++idx)
        {
            drawMeshWithTransform(config_.drawOrder[idx]);
        }
    }

    if (lastRenderMeshCount_ == 0u)
    {
        const size_t meshCount = model_.GetMeshCount();
        for (size_t meshId = 0; meshId < meshCount; ++meshId)
        {
            drawMeshWithTransform(meshId);
        }
    }

    if (logStats && drawnMesh != SIZE_MAX)
    {
        SRL::Debug::Print(1, 10, "MeshRenderer Render target:%d", (int)drawnMesh);
    }

    SRL::Scene3D::PopMatrix();
}

uint32_t MeshRenderer::DrawMesh(size_t meshId)
{
    // Robust draw path:
    // Some assets/runtime states may report a mismatched mesh kind flag.
    // Try preferred path first, then fallback to the opposite kind.
    if (isSmooth_)
    {
        auto* mesh = model_.GetMesh<SRL::Types::SmoothMesh>(meshId);
        if (mesh && mesh->FaceCount > 0 && mesh->VertexCount > 0)
        {
            model_.Draw(meshId, config_.lightDirection);
            return static_cast<uint32_t>(mesh->FaceCount);
        }

        auto* flatMesh = model_.GetMesh<SRL::Types::Mesh>(meshId);
        if (flatMesh && flatMesh->FaceCount > 0 && flatMesh->VertexCount > 0)
        {
            model_.Draw(meshId);
            return static_cast<uint32_t>(flatMesh->FaceCount);
        }
    }
    else
    {
        auto* mesh = model_.GetMesh<SRL::Types::Mesh>(meshId);
        if (mesh && mesh->FaceCount > 0 && mesh->VertexCount > 0)
        {
            model_.Draw(meshId);
            return static_cast<uint32_t>(mesh->FaceCount);
        }

        auto* smoothMesh = model_.GetMesh<SRL::Types::SmoothMesh>(meshId);
        if (smoothMesh && smoothMesh->FaceCount > 0 && smoothMesh->VertexCount > 0)
        {
            model_.Draw(meshId, config_.lightDirection);
            return static_cast<uint32_t>(smoothMesh->FaceCount);
        }
    }
    return 0u;
}

void MeshRenderer::ApplyTransform(const SRL::Math::Types::Vector3D& position,
                                  const SRL::Math::Types::Angle& yaw)
{
    SRL::Scene3D::Translate(position + offset_);
    if (config_.rotateModelX180)
    {
        SRL::Scene3D::RotateX(kAngleHalfTurn);
    }
    SRL::Scene3D::RotateY(yaw);
    if (config_.rotateModelZ180)
    {
        SRL::Scene3D::RotateZ(kAngleHalfTurn);
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
