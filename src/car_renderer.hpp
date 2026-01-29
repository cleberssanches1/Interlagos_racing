#pragma once

#include <srl.hpp>
#include "modelObject.hpp"
#include <array>
#include <vector>
#include <cstddef>

// Responsável por desenhar um ModelObject de carro com ordem de meshes e luz configuradas.
class CarRenderer
{
public:
    // Dados de posicionamento, direção de luz e ordem de desenho.
    struct Config
    {
        SRL::Math::Types::Vector3D modelCenter;
        SRL::Math::Types::Vector3D lightDirection;
        std::array<size_t, 5> drawOrder;
        size_t drawOrderCount;
        bool wireframeOnly = false;
        SRL::Math::Types::Vector3D worldPosition = SRL::Math::Types::Vector3D(0.0f, 0.0f, 0.0f);
    };

    // Mantém referência para o ModelObject (não assume propriedade).
    CarRenderer(ModelObject& car, bool isSmoothMesh, const Config& cfg)
        : car_(car), isSmooth_(isSmoothMesh), config_(cfg),
          rotY(SRL::Math::Types::Angle::FromDegrees(0)),
          rotStep(SRL::Math::Types::Angle::FromDegrees(0.0f)),
          carScale_(SRL::Math::Types::Fxp::Convert(1.0f)),
          wheel1Rot(SRL::Math::Types::Angle::FromDegrees(0)), wheel1Step(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel2Rot(SRL::Math::Types::Angle::FromDegrees(0)), wheel2Step(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel3Rot(SRL::Math::Types::Angle::FromDegrees(0)), wheel3Step(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel4Rot(SRL::Math::Types::Angle::FromDegrees(0)), wheel4Step(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel1StepSaved(SRL::Math::Types::Angle::FromDegrees(0)), wheel2StepSaved(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel3StepSaved(SRL::Math::Types::Angle::FromDegrees(0)), wheel4StepSaved(SRL::Math::Types::Angle::FromDegrees(0))
    {
        ComputeMeshCenters();
    }

    // Lógica de rotação das rodas desativada
    void SetWheel1Step(const SRL::Math::Types::Angle&) {}
    void ResetWheel1() {}
    void SetWheel2Step(const SRL::Math::Types::Angle&) {}
    void ResetWheel2() {}
    void SetWheel3Step(const SRL::Math::Types::Angle&) {}
    void ResetWheel3() {}
    void SetWheel4Step(const SRL::Math::Types::Angle&) {}
    void ResetWheel4() {}
    void ReverseAllWheels() {}
    void StartAllWheels(const SRL::Math::Types::Angle&) {}
    void StopAllWheels() {}
    void ResumeAllWheels() {}

    // Desenha o carro na matriz corrente respeitando a ordem de meshes.
    void Render()
    {
        // Minimal logging to avoid flood; keep VDP1 logs in main
        SRL::Scene3D::PushMatrix();
        SRL::Scene3D::Translate(config_.worldPosition);
        // Escala reduzida para evitar overflow em rota??es
        // SRL::Scene3D::Scale(carScale_); // desativado para evitar overflow/artefatos
        // Move model center to origin and flip X
        SRL::Math::Types::Vector3D modelOffset(-config_.modelCenter.X, -config_.modelCenter.Y, -config_.modelCenter.Z);
        SRL::Scene3D::Translate(modelOffset);
        SRL::Scene3D::RotateX(SRL::Math::Types::Angle::FromDegrees(180.0f));
        SRL::Scene3D::RotateY(rotY);

        for (size_t idx = 0; idx < config_.drawOrderCount; ++idx)
        {
            size_t meshId = config_.drawOrder[idx];
            if (meshId >= car_.GetMeshCount())
                continue;

            // Evita desenhar mesh invalido ou vazio
            bool okMesh = false;
            if (isSmooth_)
            {
                auto* m = car_.GetMesh<SRL::Types::SmoothMesh>(meshId);
                okMesh = (m && m->FaceCount > 0 && m->VertexCount > 0);
            }
            else
            {
                auto* m = car_.GetMesh<SRL::Types::Mesh>(meshId);
                okMesh = (m && m->FaceCount > 0 && m->VertexCount > 0);
            }
            if (!okMesh)
                continue;

            if (meshId == skipMeshId_)
            {
            // skip logging for minimal output
                continue;
            }

            lastMeshId_ = meshId;

            auto meshType = isSmooth_ ? "smooth" : "flat";
            size_t faceCount = 0;
            size_t vertCount = 0;
            if (isSmooth_)
            {
                auto* m = car_.GetMesh<SRL::Types::SmoothMesh>(meshId);
                if (m) { faceCount = m->FaceCount; vertCount = m->VertexCount; }
            }
            else
            {
                auto* m = car_.GetMesh<SRL::Types::Mesh>(meshId);
                if (m) { faceCount = m->FaceCount; vertCount = m->VertexCount; }
            }

            SRL::Scene3D::PushMatrix();
            if (config_.wireframeOnly)
            {
                DrawWireframeMesh(meshId, faceCount, vertCount);
            }
            else
            {
                if (isSmooth_)
                    car_.Draw(meshId, config_.lightDirection);
                else
                    car_.Draw(meshId);
            }
            SRL::Scene3D::PopMatrix();
        }

        SRL::Scene3D::PopMatrix();
        // rotY fixo (rotStep desativado)
        wheel1Rot = wheel2Rot = wheel3Rot = wheel4Rot = SRL::Math::Types::Angle::FromDegrees(0);
    }

    void SetSkipMesh(size_t meshId) { skipMeshId_ = meshId; }
    size_t LastMeshDrawn() const { return lastMeshId_; }
    void SetWorldPosition(const SRL::Math::Types::Vector3D& pos) { config_.worldPosition = pos; }

    SRL::Math::Types::Angle rotY;
    SRL::Math::Types::Angle rotStep;
    // Centros pré-calculados (um por mesh) para debug/posicionamento.
    const std::vector<SRL::Math::Types::Vector3D>& MeshCenters() const { return meshCenters_; }

private:
    // Calcula centros (AABB) para cada mesh carregado.
    void ComputeMeshCenters()
    {
        size_t count = car_.GetMeshCount();
        meshCenters_.assign(count, SRL::Math::Types::Vector3D(0.0f, 0.0f, 0.0f));
        auto computeCenter = [&](auto* mesh, size_t idx)
        {
            if (!mesh || mesh->VertexCount == 0) return;
            SRL::Math::Types::Vector3D minV(32767, 32767, 32767);
            SRL::Math::Types::Vector3D maxV(-32768, -32768, -32768);
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
            meshCenters_[idx] = (minV + maxV) / SRL::Math::Types::Fxp::Convert(2);
        };

        if (isSmooth_)
        {
            for (size_t i = 0; i < count; ++i)
                computeCenter(car_.GetMesh<SRL::Types::SmoothMesh>(i), i);
        }
        else
        {
            for (size_t i = 0; i < count; ++i)
                computeCenter(car_.GetMesh<SRL::Types::Mesh>(i), i);
        }
    }

    ModelObject& car_;
    bool isSmooth_;
    Config config_;
    std::vector<SRL::Math::Types::Vector3D> meshCenters_;
    SRL::Math::Types::Fxp carScale_;
    SRL::Math::Types::Angle wheel1Rot, wheel1Step;
    SRL::Math::Types::Angle wheel2Rot, wheel2Step;
    SRL::Math::Types::Angle wheel3Rot, wheel3Step;
    SRL::Math::Types::Angle wheel4Rot, wheel4Step;
    SRL::Math::Types::Angle wheel1StepSaved, wheel2StepSaved, wheel3StepSaved, wheel4StepSaved;
    SRL::Math::Types::Angle wheel1StepSavedDefault = SRL::Math::Types::Angle::FromDegrees(0);
    size_t skipMeshId_ = SIZE_MAX;
    size_t lastMeshId_ = SIZE_MAX;

    void DrawWireframeMesh(size_t meshId, size_t faceCount, size_t vertCount)
    {
        if (isSmooth_)
        {
            auto* mesh = car_.GetMesh<SRL::Types::SmoothMesh>(meshId);
            if (!mesh) return;
            DrawWireframePolygons(mesh->Faces, faceCount, mesh->Vertices, vertCount);
        }
        else
        {
            auto* mesh = car_.GetMesh<SRL::Types::Mesh>(meshId);
            if (!mesh) return;
            DrawWireframePolygons(mesh->Faces, faceCount, mesh->Vertices, vertCount);
        }
    }

    void DrawWireframePolygons(const SRL::Types::Polygon* faces, size_t faceCount, const SRL::Math::Types::Vector3D* verts, size_t vertCount)
    {
        for (size_t f = 0; f < faceCount; ++f)
        {
            const auto& face = faces[f];
            for (int e = 0; e < 4; ++e)
            {
                uint16_t a = face.Vertices[e];
                uint16_t b = face.Vertices[(e + 1) & 3];
                if (a >= vertCount || b >= vertCount) continue;
                SRL::Math::Types::Vector2D p0, p1;
                SRL::Scene3D::ProjectToScreen(verts[a], &p0);
                SRL::Scene3D::ProjectToScreen(verts[b], &p1);
                static const SRL::Types::HighColor kWireColor = SRL::Types::HighColor::Colors::White;
                SRL::Scene2D::DrawLine(p0, p1, kWireColor, 0);
            }
        }
    }
};










