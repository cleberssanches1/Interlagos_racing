#pragma once

#include <array>

#include "track_sgl_renderer.hpp"

namespace CarSglRenderer
{
    inline void DrawCar(ModelObject& car,
                        bool isSmooth,
                        const std::array<size_t, 5>& drawOrder,
                        size_t orderCount,
                        const SRL::Math::Types::Vector3D& worldOffset,
                        bool logAttrs = false)
    {
        if (orderCount == 0) return;

        for (size_t idx = 0; idx < orderCount; ++idx)
        {
            size_t meshId = drawOrder[idx];
            if (meshId >= car.GetMeshCount()) continue;

            if (isSmooth)
            {
                auto* mesh = car.GetMesh<SRL::Types::SmoothMesh>(meshId);
                if (!mesh || mesh->VertexCount == 0 || mesh->FaceCount == 0) continue;
                TrackSglRenderer::DrawMesh(mesh->Vertices, mesh->VertexCount,
                                            mesh->Faces, mesh->FaceCount,
                                            0x83FF,
                                            worldOffset,
                                            SRL::Math::Types::Fxp::Convert(1.0f),
                                            logAttrs && idx == 0);
            }
            else
            {
                auto* mesh = car.GetMesh<SRL::Types::Mesh>(meshId);
                if (!mesh || mesh->VertexCount == 0 || mesh->FaceCount == 0) continue;
                TrackSglRenderer::DrawMesh(mesh->Vertices, mesh->VertexCount,
                                            mesh->Faces, mesh->FaceCount,
                                            0x83FF,
                                            worldOffset,
                                            SRL::Math::Types::Fxp::Convert(1.0f),
                                            logAttrs && idx == 0);
            }
        }
    }
}
