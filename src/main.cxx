#include <srl.hpp>
#include "modelObject.hpp"
#include "camera_controller.hpp"
#include "car_renderer.hpp"
#include "sky_background.hpp"
#include "srl_tga.hpp"
#include "srl_tilemap_interfaces.hpp"
#include <vector>
#include <array>

using namespace SRL::Types;
using namespace SRL::Math::Types;

// Simple shading table
HighColor shadingTable[32] = {
    HighColor::FromRGB555(0, 0, 0), HighColor::FromRGB555(1, 1, 1),
    HighColor::FromRGB555(2, 2, 2), HighColor::FromRGB555(3, 3, 3),
    HighColor::FromRGB555(4, 4, 4), HighColor::FromRGB555(5, 5, 5),
    HighColor::FromRGB555(6, 6, 6), HighColor::FromRGB555(7, 7, 7),
    HighColor::FromRGB555(8, 8, 8), HighColor::FromRGB555(9, 9, 9),
    HighColor::FromRGB555(10, 10, 10), HighColor::FromRGB555(11, 11, 11),
    HighColor::FromRGB555(12, 12, 12), HighColor::FromRGB555(13, 13, 13),
    HighColor::FromRGB555(14, 14, 14), HighColor::FromRGB555(15, 15, 15),
    HighColor::FromRGB555(16, 16, 16), HighColor::FromRGB555(17, 17, 17),
    HighColor::FromRGB555(18, 18, 18), HighColor::FromRGB555(19, 19, 19),
    HighColor::FromRGB555(20, 20, 20), HighColor::FromRGB555(21, 21, 21),
    HighColor::FromRGB555(22, 22, 22), HighColor::FromRGB555(23, 23, 23),
    HighColor::FromRGB555(24, 24, 24), HighColor::FromRGB555(25, 25, 25),
    HighColor::FromRGB555(26, 26, 26), HighColor::FromRGB555(27, 27, 27),
    HighColor::FromRGB555(28, 28, 28), HighColor::FromRGB555(29, 29, 29),
    HighColor::FromRGB555(30, 30, 30), HighColor::FromRGB555(31, 31, 31)
};

int main()
{
    SRL::Core::Initialize(HighColor(0x10, 0x20, 0x18));
    SRL::Debug::Print(1, 1, "CAR1.NYA viewer");

    ModelObject car("CAR1.NYA", 0);
    bool isSmoothMesh = car.IsSmooth();
    uint32_t faceCount = car.GetFaceCount();
    uint32_t vertexCount = car.GetVertexCount();
    uint32_t meshCount = car.GetMeshCount();
    SRL::Debug::Print(1, 2, "Faces:%u Verts:%u Meshes:%u Smooth:%d", faceCount, vertexCount, meshCount, isSmoothMesh ? 1 : 0);

    // Simple frustum
    SRL::Scene3D::SetPerspective(Angle::FromDegrees(60.0f));

    // Sky via VDP2 (componente reutilizável)
    SRL::VDP2::SetBackColor(HighColor::FromRGB555(0, 0, 31)); // fallback azul
    SkyBackground skyBg;
    skyBg.yawFactor = Fxp(0.10f);
    skyBg.driftStep = Fxp(0.02f);
    const char* skyPaths[] = {
        "cd/data/skybox_1.tga",
        "cd/data/SKYBOX_1.TGA",
        "data/skybox_1.tga",
        "data/SKYBOX_1.TGA",
        "skybox_1.tga",
        "SKYBOX_1.TGA"};
    skyBg.Load(skyPaths, sizeof(skyPaths) / sizeof(skyPaths[0]));

    // Camera base (Saturn: Y+ para baixo; Y- acima)
    Camera::State cameraState{
        .yawDeg = 180,          // atras do carro
        .pitchDeg = -21,        // pitch que resulta em raw ~61714
        .viewYawDeg = 0,        // yaw de olhar (Z + esquerda/direita)
        .viewPitchDeg = 0,      // pitch de olhar (Z + cima/baixo)
        .radius = Fxp(46.0f),   // raio padrao solicitado
        .strafe = Vector3D(Fxp::Convert(0), Fxp::Convert(-4), Fxp::Convert(-2)),
        .location = Vector3D(0.0, 0.0, -50.0f), // sera recalculada no loop
        .yaw = Angle::FromDegrees(Fxp::Convert(180)),
        .pitch = Angle::FromDegrees(Fxp::Convert(-21)),
        .viewYaw = Angle::FromDegrees(Fxp::Convert(0)),
        .viewPitch = Angle::FromDegrees(Fxp::Convert(0)),
    };
    Camera::Tuning cameraTuning{};
    Camera::RefreshAngles(cameraState);
    cameraState.location = Camera::OrbitPosition(cameraState.yaw, cameraState.pitch, cameraState.radius) + cameraState.strafe;

    Vector3D lightDirection = Vector3D(0.35, -0.15, 0.35);
    SRL::Scene3D::SetDirectionalLight(lightDirection);

    // Prepare Gouraud/light tables if smooth
    std::vector<HighColor> workTable;
    std::vector<uint8_t> vertWork;
    if (isSmoothMesh)
    {
        workTable.resize(faceCount << 2);
        vertWork.resize(vertexCount);
        SRL::Scene3D::LightInitGouraudTable(0, vertWork.data(), workTable.data(), faceCount);
        SRL::Scene3D::LightSetGouraudTable(shadingTable);
        SRL::Core::OnVblank += SRL::Scene3D::LightCopyGouraudTable;
    }

    // Center of model from bounds (approx) to bring into view
    Vector3D modelCenter = Vector3D(0.0, 3.607f, -0.398f);
    Vector3D modelOffset(-modelCenter.X, -modelCenter.Y, -modelCenter.Z);
    SRL::Debug::Print(1, 3, "Center: %d, %d, %d", modelCenter.X.As<int16_t>(), modelCenter.Y.As<int16_t>(), modelCenter.Z.As<int16_t>());

    // Draw order: wheels first (1..4), then body (0)
    std::array<size_t, 5> drawOrder = {1, 2, 3, 4, 0};
    size_t orderCount = (meshCount < 5) ? meshCount : 5;
    CarRenderer::Config carConfig{modelCenter, lightDirection, drawOrder, orderCount};
    CarRenderer carRenderer(car, isSmoothMesh, carConfig);

    // Input e calculo de camera orbitando o modelo
    SRL::Input::Digital pad(0);

    // Compute bounds for debugging
    SRL::Math::Types::Vector3D minV = Vector3D(32767, 32767, 32767);
    SRL::Math::Types::Vector3D maxV = Vector3D(-32768, -32768, -32768);
    for (size_t m = 0; m < meshCount; ++m)
    {
        auto* mesh = car.GetMesh<SRL::Types::SmoothMesh>(m);
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
    SRL::Debug::Print(1, 6, "Min: %d %d %d", minV.X.As<int16_t>(), minV.Y.As<int16_t>(), minV.Z.As<int16_t>());
    SRL::Debug::Print(1, 7, "Max: %d %d %d", maxV.X.As<int16_t>(), maxV.Y.As<int16_t>(), maxV.Z.As<int16_t>());
    SRL::Debug::Print(1, 9, "Model pos: %d %d %d", modelCenter.X.As<int16_t>(), modelCenter.Y.As<int16_t>(), modelCenter.Z.As<int16_t>());

    while (1)
    {
        // Atualiza skybox VDP2
        skyBg.Update(cameraState.yawDeg);
        Camera::UpdateInput(cameraState, cameraTuning, pad);

        Vector3D cameraLocation = cameraState.location;
        Vector3D lookTarget = Camera::ComputeLookTarget(cameraState, cameraTuning, pad, Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0)));

        // LookAt focando no ponto de fuga/horizonte (em vez do carro)
        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(cameraLocation, lookTarget, Angle::FromDegrees(0.0));

        // Piso (grid) no plano Y = -8
        {
            const int gridHalf = 150;
            const int step = 5;
            const Fxp yPlane = Fxp::Convert(-8);
            for (int x = -gridHalf; x <= gridHalf; x += step)
            {
                Vector2D a, b;
                Vector3D p1 = Vector3D(Fxp::Convert(x), yPlane, Fxp::Convert(-gridHalf));
                Vector3D p2 = Vector3D(Fxp::Convert(x), yPlane, Fxp::Convert(gridHalf));
                SRL::Scene3D::ProjectToScreen(p1, &a);
                SRL::Scene3D::ProjectToScreen(p2, &b);
                SRL::Scene2D::DrawLine(a, b, HighColor::FromRGB555(4, 8, 4), 0);
            }
            for (int z = -gridHalf; z <= gridHalf; z += step)
            {
                Vector2D a, b;
                Vector3D p1 = Vector3D(Fxp::Convert(-gridHalf), yPlane, Fxp::Convert(z));
                Vector3D p2 = Vector3D(Fxp::Convert(gridHalf), yPlane, Fxp::Convert(z));
                SRL::Scene3D::ProjectToScreen(p1, &a);
                SRL::Scene3D::ProjectToScreen(p2, &b);
                SRL::Scene2D::DrawLine(a, b, HighColor::FromRGB555(4, 8, 4), 0);
            }
        }

        SRL::Debug::Print(1, 4, "Offset: %d, %d, %d", modelOffset.X.As<int16_t>(), modelOffset.Y.As<int16_t>(), modelOffset.Z.As<int16_t>());
        SRL::Debug::Print(1, 5, "Cam: %d, %d, %d", cameraLocation.X.As<int16_t>(), cameraLocation.Y.As<int16_t>(), cameraLocation.Z.As<int16_t>());
        SRL::Debug::Print(1, 8, "Yaw:%u Pitch:%u R:%d", cameraState.yaw.RawValue(), cameraState.pitch.RawValue(), cameraState.radius.As<int16_t>());
        carRenderer.Render();

        // Draw axis lines at the origin for reference
        Vector2D o2D, x2D, y2D, z2D;
        SRL::Scene3D::ProjectToScreen(Vector3D(0.0, 0.0, 0.0), &o2D);
        SRL::Scene3D::ProjectToScreen(Vector3D(4.0, 0.0, 0.0), &x2D);
        SRL::Scene3D::ProjectToScreen(Vector3D(0.0, 4.0, 0.0), &y2D);
        SRL::Scene3D::ProjectToScreen(Vector3D(0.0, 0.0, 4.0), &z2D);
        const SRL::Math::Types::Fxp sort2D = 0;
        SRL::Scene2D::DrawLine(o2D, x2D, HighColor::Colors::Red, sort2D);
        SRL::Scene2D::DrawLine(o2D, y2D, HighColor::Colors::Green, sort2D);
        SRL::Scene2D::DrawLine(o2D, z2D, HighColor::Colors::Blue, sort2D);
        SRL::Core::Synchronize();
    }

    return 0;
}




