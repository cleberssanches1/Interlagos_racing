#pragma once

#include <srl.hpp>
#include "modelObject.hpp"
#include <array>
#include <vector>

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
        : car_(car), isSmooth_(isSmoothMesh), config_(cfg),
          rotY(SRL::Math::Types::Angle::FromDegrees(0)),
          rotStep(SRL::Math::Types::Angle::FromDegrees(0.0f)),
          wheel1Rot(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel1Step(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel2Rot(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel2Step(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel3Rot(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel3Step(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel4Rot(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel4Step(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel1StepSaved(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel2StepSaved(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel3StepSaved(SRL::Math::Types::Angle::FromDegrees(0)),
          wheel4StepSaved(SRL::Math::Types::Angle::FromDegrees(0))
    {
        ComputeMeshCenters();
    }

    void SetWheel1Step(const SRL::Math::Types::Angle& step) { wheel1Step = step; }
    void ResetWheel1() { wheel1Rot = SRL::Math::Types::Angle::FromDegrees(0); }

    void SetWheel2Step(const SRL::Math::Types::Angle& step) { wheel2Step = step; }
    void ResetWheel2() { wheel2Rot = SRL::Math::Types::Angle::FromDegrees(0); }

    void SetWheel3Step(const SRL::Math::Types::Angle& step) { wheel3Step = step; }
    void ResetWheel3() { wheel3Rot = SRL::Math::Types::Angle::FromDegrees(0); }

    void SetWheel4Step(const SRL::Math::Types::Angle& step) { wheel4Step = step; }
    void ResetWheel4() { wheel4Rot = SRL::Math::Types::Angle::FromDegrees(0); }

    void ReverseAllWheels() {
        wheel1Step = -wheel1Step; wheel2Step = -wheel2Step;
        wheel3Step = -wheel3Step; wheel4Step = -wheel4Step;
    }

    void StartAllWheels(const SRL::Math::Types::Angle& step) {
        wheel1Step = wheel2Step = wheel3Step = wheel4Step = step;
        wheel1StepSaved = wheel2StepSaved = wheel3StepSaved = wheel4StepSaved = step;
    }

    void StopAllWheels() {
        wheel1StepSaved = wheel1Step; wheel1Step = SRL::Math::Types::Angle::FromDegrees(0);
        wheel2StepSaved = wheel2Step; wheel2Step = SRL::Math::Types::Angle::FromDegrees(0);
        wheel3StepSaved = wheel3Step; wheel3Step = SRL::Math::Types::Angle::FromDegrees(0);
        wheel4StepSaved = wheel4Step; wheel4Step = SRL::Math::Types::Angle::FromDegrees(0);
    }

    void ResumeAllWheels() {
        if (wheel1Step.RawValue() == 0) wheel1Step = (wheel1StepSaved.RawValue()!=0 ? wheel1StepSaved : wheel1StepSavedDefault);
        if (wheel2Step.RawValue() == 0) wheel2Step = (wheel2StepSaved.RawValue()!=0 ? wheel2StepSaved : wheel1StepSavedDefault);
        if (wheel3Step.RawValue() == 0) wheel3Step = (wheel3StepSaved.RawValue()!=0 ? wheel3StepSaved : wheel1StepSavedDefault);
        if (wheel4Step.RawValue() == 0) wheel4Step = (wheel4StepSaved.RawValue()!=0 ? wheel4StepSaved : wheel1StepSavedDefault);
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

            SRL::Scene3D::PushMatrix();
            // Roda_1 (mesh 1)
            if (meshId == 1 && meshId < meshCenters_.size())
            {
                const auto& c = meshCenters_[meshId];
                SRL::Scene3D::Translate(c);
                SRL::Scene3D::RotateX(-wheel1Rot);
                SRL::Scene3D::Translate(-c);
            }
            // Roda_2 (mesh 2)
            if (meshId == 2 && meshId < meshCenters_.size())
            {
                const auto& c = meshCenters_[meshId];
                SRL::Scene3D::Translate(c);
                SRL::Scene3D::RotateX(-wheel2Rot);
                SRL::Scene3D::Translate(-c);
            }
            // Roda_3 (mesh 3) piv? ajustado subtraindo 32760 e mais 8 no eixo Z
            if (meshId == 3 && meshId < meshCenters_.size())
            {
                SRL::Math::Types::Vector3D c = meshCenters_[meshId];
                c.Z = c.Z - SRL::Math::Types::Fxp::Convert(32760) - SRL::Math::Types::Fxp::Convert(8);
                SRL::Scene3D::Translate(c);
                SRL::Scene3D::RotateX(-wheel3Rot);
                SRL::Scene3D::Translate(-c);
            }
            // Roda_4 (mesh 4) mesma l?gica de piv?
            if (meshId == 4 && meshId < meshCenters_.size())
            {
                SRL::Math::Types::Vector3D c = meshCenters_[meshId];
                c.Z = c.Z - SRL::Math::Types::Fxp::Convert(32760) - SRL::Math::Types::Fxp::Convert(8);
                SRL::Scene3D::Translate(c);
                SRL::Scene3D::RotateX(-wheel4Rot);
                SRL::Scene3D::Translate(-c);
            }

            if (isSmooth_)
                car_.Draw(meshId, config_.lightDirection);
            else
                car_.Draw(meshId);

            SRL::Scene3D::PopMatrix();
        }

        SRL::Scene3D::PopMatrix();
        rotY += rotStep;
        wheel1Rot -= wheel1Step;
        wheel2Rot -= wheel2Step;
        wheel3Rot -= wheel3Step;
        wheel4Rot -= wheel4Step;
    }

    SRL::Math::Types::Angle rotY;
    SRL::Math::Types::Angle rotStep;
    const std::vector<SRL::Math::Types::Vector3D>& MeshCenters() const { return meshCenters_; }

private:
    SRL::Math::Types::Angle wheel1Rot, wheel1Step;
    SRL::Math::Types::Angle wheel2Rot, wheel2Step;
    SRL::Math::Types::Angle wheel3Rot, wheel3Step;
    SRL::Math::Types::Angle wheel4Rot, wheel4Step;
    SRL::Math::Types::Angle wheel1StepSaved, wheel2StepSaved, wheel3StepSaved, wheel4StepSaved;
    SRL::Math::Types::Angle wheel1StepSavedDefault = SRL::Math::Types::Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15));
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
            {
                computeCenter(car_.GetMesh<SRL::Types::SmoothMesh>(i), i);
            }
        }
        else
        {
            for (size_t i = 0; i < count; ++i)
            {
                computeCenter(car_.GetMesh<SRL::Types::Mesh>(i), i);
            }
        }
    }

    bool IsWheel(size_t meshId) const
    {
        return meshId == 1 || meshId == 2 || meshId == 3 || meshId == 4;
    }

    ModelObject& car_;
    bool isSmooth_;
    Config config_;
    std::vector<SRL::Math::Types::Vector3D> meshCenters_;
};




























