#include <srl.hpp>

#include "modelObject.hpp"

#include "camera_controller.hpp"

#include "car_renderer.hpp"

#include "sky_background.hpp"

#include "hud_stats.hpp"

#include "sky_environment.hpp"

#include "background_manager.hpp"

#include "sky_background_dome.hpp"

#include "sky_background_rbg.hpp"

#include "sky_background.hpp"

#include "srl_tga.hpp"

#include "srl_tilemap_interfaces.hpp"
#include "camera_rig.hpp"

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



            BackgroundManager bgManager;    const char* skyPaths[] = {"cd/data/skybox_1.tga","data/skybox_1.tga","skybox_1.tga","cd/data/SKYBOX_1.TGA","data/SKYBOX_1.TGA","SKYBOX_1.TGA"};    bgManager.Init(skyPaths, sizeof(skyPaths) / sizeof(skyPaths[0]));



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

    CarRenderer carRenderer(car, isSmoothMesh, carConfig);    carRenderer.SetWheel1Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));    carRenderer.SetWheel2Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));    carRenderer.SetWheel3Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));    carRenderer.SetWheel4Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));



    // Input e calculo de camera orbitando o modelo

    SRL::Input::Digital pad(0);

    int32_t carYawDeg = 0;



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



    HudStats hudStats;

    hudStats.Init(faceCount, vertexCount, meshCount, isSmoothMesh, modelCenter, minV, maxV);

    CameraRig::OrbitState xOrbitState{};

    while (1)

    {

        Camera::UpdateInput(cameraState, cameraTuning, pad);

        const bool aHeld = pad.IsHeld(SRL::Input::Digital::Button::A);

        const bool bHeld = pad.IsHeld(SRL::Input::Digital::Button::B);
        const bool xHeld = pad.IsHeld(SRL::Input::Digital::Button::X);
        const bool cHeld = pad.IsHeld(SRL::Input::Digital::Button::C);

        const bool lHeld = pad.IsHeld(SRL::Input::Digital::Button::L);

        const bool rHeld = pad.IsHeld(SRL::Input::Digital::Button::R);

        const int16_t carYawStepDeg = cameraTuning.yawStepDeg;



        // Rotaciona apenas o carro com L/R (plano horizontal)

        if (!aHeld && !bHeld && !cHeld)

        {

            if (lHeld) carYawDeg -= carYawStepDeg;

            if (rHeld) carYawDeg += carYawStepDeg;

            if (carYawDeg < 0) carYawDeg += 360;

            if (carYawDeg >= 360) carYawDeg -= 360;

        }



        // Rotaciona carro e camera (modo X) usando CameraRig utilit?rio
        CameraRig::HandleOrbitAroundCar(cameraState, carYawStepDeg, xHeld, lHeld, rHeld, carYawDeg, xOrbitState, true);

        // Controles de rodas: C inicia/resume, B para
        if (cHeld) { carRenderer.StartAllWheels(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15))); carRenderer.ResumeAllWheels(); }
        if (bHeld) { carRenderer.StopAllWheels(); }

// Atualiza skybox VDP2
        bgManager.Update(cameraState);

        Vector3D cameraLocation = cameraState.location;
        Vector3D lookTarget = Camera::ComputeLookTarget(cameraState, cameraTuning, pad, modelCenter);
        // lookTarget padrao segue o alvo calculado (b livre)
        hudStats.Update(cameraState, modelOffset, cameraLocation, modelCenter);

        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(cameraLocation, lookTarget, Angle::FromDegrees(0.0));
        // Debug: posicoes das rodas
                const auto& centers = carRenderer.MeshCenters();
        if (centers.size() > 4)
        {
            SRL::Debug::Print(1, 10, "Roda_1: %d %d %d", centers[1].X.As<int16_t>(), centers[1].Y.As<int16_t>(), centers[1].Z.As<int16_t>());
            SRL::Debug::Print(1, 11, "Roda_2: %d %d %d", centers[2].X.As<int16_t>(), centers[2].Y.As<int16_t>(), centers[2].Z.As<int16_t>());
            auto r3pivot = centers[3] - modelCenter;
            SRL::Debug::Print(1, 12, "Roda_3: %d %d %d", centers[3].X.As<int16_t>(), centers[3].Y.As<int16_t>(), centers[3].Z.As<int16_t>());
            SRL::Debug::Print(1, 13, "R3 piv: %d %d %d", r3pivot.X.As<int16_t>(), r3pivot.Y.As<int16_t>(), r3pivot.Z.As<int16_t>());
            SRL::Debug::Print(1, 14, "Roda_4: %d %d %d", centers[4].X.As<int16_t>(), centers[4].Y.As<int16_t>(), centers[4].Z.As<int16_t>());
        }
        SRL::Debug::Print(1, 4, "Offset: %d, %d, %d", modelOffset.X.As<int16_t>(), modelOffset.Y.As<int16_t>(), modelOffset.Z.As<int16_t>());
        SRL::Debug::Print(1, 5, "Cam: %d, %d, %d", cameraLocation.X.As<int16_t>(), cameraLocation.Y.As<int16_t>(), cameraLocation.Z.As<int16_t>());
        SRL::Debug::Print(1, 8, "Yaw:%u Pitch:%u R:%d", cameraState.yaw.RawValue(), cameraState.pitch.RawValue(), cameraState.radius.As<int16_t>());
        carRenderer.rotY = Angle::FromDegrees(Fxp::Convert(carYawDeg));
        // roda gira constante (ajuste se necessario)
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




















































































