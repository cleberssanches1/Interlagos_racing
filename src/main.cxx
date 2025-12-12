#include <srl.hpp>
#include "modelObject.hpp"
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

    // Camera base (Saturn: Y+ para baixo; Y- acima)
    int32_t camYawDeg = 180;    // atras do carro
    int32_t camPitchDeg = -21;  // pitch que resulta em raw ~61714
    int32_t viewYawDeg = 0;     // yaw de olhar (Z + esquerda/direita)
    int32_t viewPitchDeg = 0;   // pitch de olhar (Z + cima/baixo)
    Fxp camRadius = Fxp(46.0f); // raio padrao solicitado
    Angle camYaw = Angle::FromDegrees(Fxp::Convert(camYawDeg));
    Angle camPitch = Angle::FromDegrees(Fxp::Convert(camPitchDeg));
    Angle viewYaw = Angle::FromDegrees(Fxp::Convert(viewYawDeg));
    Angle viewPitch = Angle::FromDegrees(Fxp::Convert(viewPitchDeg));
    Vector3D cameraLocation = Vector3D(0.0, 0.0, -50.0f); // sera recalculada no loop
    // Posiciona a camera para chegar em (0, -20, -45) atras do carro, centro no horizonte
    Vector3D camStrafe = Vector3D(Fxp::Convert(0), Fxp::Convert(-4), Fxp::Convert(-2));

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

    // Draw order: wheels first (1..4), then body (0)
    size_t order[] = {1, 2, 3, 4, 0};
    size_t orderCount = (meshCount < 5) ? meshCount : 5;
    // Rotation of the whole model
    Angle rotY = Angle::FromDegrees(0);
    Angle rotStep = Angle::FromDegrees(0.0f); // keep static for debugging visibility
    // Center of model from bounds (approx) to bring into view
    Vector3D modelCenter = Vector3D(0.0, 3.607f, -0.398f);
    SRL::Debug::Print(1, 3, "Center: %d, %d, %d", modelCenter.X.As<int16_t>(), modelCenter.Y.As<int16_t>(), modelCenter.Z.As<int16_t>());

    // Input e calculo de camera orbitando o modelo
    SRL::Input::Digital pad(0);
    auto computeCamera = [&](Angle yaw, Angle pitch, Fxp radius) -> Vector3D
    {
        Fxp sinYaw = SRL::Math::Trigonometry::Sin(yaw);
        Fxp cosYaw = SRL::Math::Trigonometry::Cos(yaw);
        Fxp sinPitch = SRL::Math::Trigonometry::Sin(pitch);
        Fxp cosPitch = SRL::Math::Trigonometry::Cos(pitch);
        return Vector3D(
            radius * sinYaw * cosPitch,
            radius * sinPitch,
            radius * cosYaw * cosPitch);
    };

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

    // posicao inicial da camera calculada a partir dos angulos/raio
    cameraLocation = computeCamera(camYaw, camPitch, camRadius) + camStrafe;

    while (1)
    {
        // Controles de camera:
        // Z + direcional = girar yaw/pitch (limite +/-90 graus). Diagonais funcionam.
        // Y + direcional = mover a camera (frente/tras/esquerda/direita) sem girar.
        const int32_t yawStepDeg = 2;    // ~50% mais suave
        const int32_t pitchStepDeg = 2;  // ~50% mais suave
        const Fxp moveStep = Fxp(0.5f);  // ~50% mais suave
        const Fxp strafeStep = Fxp(0.5f);
        bool zHeld = pad.IsHeld(SRL::Input::Digital::Button::Z);
        bool yHeld = pad.IsHeld(SRL::Input::Digital::Button::Y);

        if (zHeld)
        {
            // Ajuste apenas a direcao de olhar (sem mover a camera)
            if (pad.IsHeld(SRL::Input::Digital::Button::Up))   viewPitchDeg -= pitchStepDeg;
            if (pad.IsHeld(SRL::Input::Digital::Button::Down)) viewPitchDeg += pitchStepDeg;
            if (pad.IsHeld(SRL::Input::Digital::Button::Left)) viewYawDeg   -= yawStepDeg;
            if (pad.IsHeld(SRL::Input::Digital::Button::Right)) viewYawDeg += yawStepDeg;
        }
        if (yHeld)
        {
            // Movimento absoluto (mundo)
            Vector3D moveDelta = Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0));
            // Inverte frente/tras para corresponder ao esperado pelo jogador
            if (pad.IsHeld(SRL::Input::Digital::Button::Up))    moveDelta += Vector3D(Fxp::Convert(0), Fxp::Convert(0),  moveStep);
            if (pad.IsHeld(SRL::Input::Digital::Button::Down))  moveDelta += Vector3D(Fxp::Convert(0), Fxp::Convert(0), -moveStep);
            // Invertemos lateral para corresponder ao esperado na tela (Y + Left/Right)
            if (pad.IsHeld(SRL::Input::Digital::Button::Left))  moveDelta += Vector3D(strafeStep, Fxp::Convert(0), Fxp::Convert(0));
            if (pad.IsHeld(SRL::Input::Digital::Button::Right)) moveDelta += Vector3D(-strafeStep, Fxp::Convert(0), Fxp::Convert(0));
            camStrafe += moveDelta;
        }
        // Limites em graus
        if (camYawDeg < 90) camYawDeg = 90;
        if (camYawDeg > 270) camYawDeg = 270;
        if (camPitchDeg < -80) camPitchDeg = -80;
        if (camPitchDeg > 80) camPitchDeg = 80;
        if (viewYawDeg < -90) viewYawDeg = -90;
        if (viewYawDeg > 90) viewYawDeg = 90;
        if (viewPitchDeg < -80) viewPitchDeg = -80;
        if (viewPitchDeg > 80) viewPitchDeg = 80;

        camYaw = Angle::FromDegrees(Fxp::Convert(camYawDeg));
        camPitch = Angle::FromDegrees(Fxp::Convert(camPitchDeg));
        viewYaw = Angle::FromDegrees(Fxp::Convert(viewYawDeg));
        viewPitch = Angle::FromDegrees(Fxp::Convert(viewPitchDeg));

        // Recalcula posicao (orbita + deslocamento linear)
        cameraLocation = computeCamera(camYaw, camPitch, camRadius) + camStrafe;

        // LookAt focando no ponto de fuga/horizonte (em vez do carro)
        SRL::Scene3D::LoadIdentity();
        // Mantem camera atras do carro mirando horizonte a frente (ponto de fuga) conforme Z+direcional
        Vector3D horizonTarget = camStrafe + computeCamera(viewYaw, viewPitch, Fxp(120.0f));
        SRL::Scene3D::LookAt(cameraLocation, horizonTarget, Angle::FromDegrees(0.0));

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

        SRL::Scene3D::PushMatrix();
        {
            // Move model center to origin
            Vector3D modelOffset(-modelCenter.X, -modelCenter.Y, -modelCenter.Z);
            SRL::Scene3D::Translate(modelOffset);
            // Flip X para garantir modelo em pe
            SRL::Scene3D::RotateX(Angle::FromDegrees(180.0f));
            SRL::Debug::Print(1, 4, "Offset: %d, %d, %d", modelOffset.X.As<int16_t>(), modelOffset.Y.As<int16_t>(), modelOffset.Z.As<int16_t>());
            SRL::Debug::Print(1, 5, "Cam: %d, %d, %d", cameraLocation.X.As<int16_t>(), cameraLocation.Y.As<int16_t>(), cameraLocation.Z.As<int16_t>());
            SRL::Debug::Print(1, 8, "Yaw:%u Pitch:%u R:%d", camYaw.RawValue(), camPitch.RawValue(), camRadius.As<int16_t>());
            SRL::Scene3D::RotateY(rotY);
            for (size_t idx = 0; idx < orderCount; ++idx)
            {
                size_t meshId = order[idx];
                if (meshId >= meshCount)
                {
                    continue;
                }

                if (isSmoothMesh)
                {
                    car.Draw(meshId, lightDirection);
                }
                else
                {
                    car.Draw(meshId);
                }
            }
        }
        SRL::Scene3D::PopMatrix();

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

        rotY += rotStep;
        SRL::Core::Synchronize();
    }

    return 0;
}
