#include <srl.hpp>

#include "modelObject.hpp"

#include "camera_controller.hpp"

#include "car_renderer.hpp"
#include "track_renderer.hpp"

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
#include <memory>

extern "C" void __throw_bad_array_new_length() {}
extern "C" void __throw_bad_alloc() {}
namespace std { void __throw_bad_array_new_length() {} void __throw_bad_alloc() {} }


#include "resource_loader.hpp"

using namespace SRL::Types;

using namespace SRL::Math::Types;

// Logs essenciais na tela (reduzido)
constexpr bool kLog = true;
#define MLOG(...) do { if constexpr (kLog) { SRL::Debug::Print(__VA_ARGS__); } } while(0)

// Procura o primeiro caminho existente em disco.
static const char* FindExistingPath(const char* const* paths, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        SRL::Cd::File f(paths[i]);
        if (f.Exists() && f.Size.Bytes > 0) return paths[i];
    }
    return nullptr;
}

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

// Representa o pipeline de carga do carro: cart (DRAM 4MB) e c├│pia opcional na WRAM.
struct CarPipeline
{
    CarLoadResult cart;                     // Resultado da carga obrigat├│ria no cart.
    std::unique_ptr<ModelObject> wramCopy;  // C├│pia independente na work RAM.

    // Retorna o modelo ativo (c├│pia em WRAM se existir, sen├úo o do cart).
    ModelObject* ActiveModel() const { return wramCopy ? wramCopy.get() : cart.car; }

    // Indica se h├í um modelo utiliz├ível.
    bool Loaded() const { return cart.loaded && ActiveModel(); }
};

// Executa a carga CD -> cart (4MB) e opcionalmente cart -> WRAM.
static CarPipeline LoadCarPipeline(const char* const* paths, size_t pathCount, bool makeWramCopy)
{
    CarPipeline pipe{};
    const char* chosenPath = FindExistingPath(paths, pathCount);

    // 1) Carga principal no cart (forceCart = true garante DRAM 4MB).
    pipe.cart = LoadCarToCart(paths, pathCount, /*forceCart*/true);

    // 2) C├│pia independente em WRAM para evitar compartilhar ponteiros do cart.
    if (makeWramCopy && chosenPath)
    {
        pipe.wramCopy = std::make_unique<ModelObject>(chosenPath, 0, false, 0, false, false, false);
    }
    return pipe;
}


class GameApp {
public:
    // Inicializa engine, carrega recursos e executa o loop principal.
    int Run();
};


int GameApp::Run()

{

    SRL::Core::Initialize(HighColor(0x10, 0x20, 0x18));

    const bool logCar = true;
    const bool logTrack = true;
    // Log inicial simples do Cart e HWR
    auto crep = SRL::Memory::CartRam::GetReport();
    SRL::Debug::Print(0, 0, "CRT ok:%d free:%d total:%d", crep.TotalSize > 0 ? 1 : 0, (int)crep.FreeSize, (int)crep.TotalSize);
    auto rep = SRL::Memory::HighWorkRam::GetReport();
    SRL::Debug::Print(0, 1, "HWR free:%d total:%d", (int)rep.FreeSize, (int)rep.TotalSize);
    const bool cartOk = crep.TotalSize > 0;

    // Teste simples: escreve string na HWR e l^ de volta (VDP2 debug)
    const char testMsg[] = "Cart DRAM OK";
    int32_t hwrBeforeStr = SRL::Memory::CartRam::GetFreeSpace();
    size_t testLen = sizeof(testMsg); // inclui terminador
    char* hwrStr = reinterpret_cast<char*>(SRL::Memory::CartRam::Malloc(testLen));
    if (hwrStr)
    {
        for (size_t i = 0; i < testLen; ++i) hwrStr[i] = testMsg[i];
        int32_t hwrAfterStr = SRL::Memory::CartRam::GetFreeSpace();
        MLOG(0, 6, "CRT addr:%08lx", (unsigned long)hwrStr);
        MLOG(0, 7, "CRT free b:%d", hwrBeforeStr);
        MLOG(0, 8, "CRT free a:%d", hwrAfterStr);
        MLOG(0, 9, "CRT txt:%s", hwrStr);
    }
    else
    {
    // silencia logs do teste HWR
    }

        // Carrega carro na DRAM do cart (sem usar WRAM)
    const char* carPaths[] = { "CD/DATA/CAR1.NYA", "CD/DATA/CAR1.NYA;1", "cd/data/car1.nya", "cd/data/car1.nya;1", "CAR1.NYA", "CAR1.NYA;1", "car1.nya", "car1.nya;1" };
    const bool useCartCopyPipeline = true; // cart -> WRAM -> VDP1
    CarPipeline carPipe = LoadCarPipeline(carPaths, sizeof(carPaths)/sizeof(carPaths[0]), useCartCopyPipeline);

    ModelObject* carPtr = carPipe.ActiveModel();
    bool carValid = carPipe.Loaded();
    if (!carValid && logCar)
    {
        MLOG(0, 7, "Carro nao carregou (meshes/faces zero)");
    }

// Se faltar cart, travamos o loop exibindo a mensagem
    bool cartOkFlag = cartOk;
    // Carrega pista (INTLAGOS.NYA) na mesma pasta
    TrackRenderer trackRenderer;
    trackRenderer.SetDirect2D(true);
    trackRenderer.SetSglDirect(false);
    const bool hasTrackSegment = false;
    SRL::Math::Types::Vector3D trackSegOffset(0, 0, 0);
    const bool enableTrack = true; // ativa carga da pista na DRAM (sem render)
    bool hasTrack = false;
    bool isTrackSmooth = false;
    uint32_t trackFaceCount = 0;
    uint32_t trackVertexCount = 0;
    size_t trackMeshCount = 0;
    ModelBounds trackBounds{};
    uint32_t trackDrawnFaces = 0;
    uint32_t trackDrawnMeshes = 0;
    const bool renderTrack = false; // pista desligada para focar no comparativo do carro
    const bool renderCar = true; // carro ligado
    const bool renderAxes = false; // desliga eixos de debug

    const bool carWasSmooth = carPtr ? carPtr->IsSmooth() : false;
    const bool isSmoothMesh = carWasSmooth; // restaura carregamento smooth
    MLOG(1, 1, "CAR1.NYA load (smooth flag:%d)", carWasSmooth ? 1 : 0);

    uint32_t faceCount = carPtr ? carPtr->GetFaceCount() : 0;

    uint32_t vertexCount = carPtr ? carPtr->GetVertexCount() : 0;

    uint32_t meshCount = carPtr ? carPtr->GetMeshCount() : 0;

    // Simple frustum

    SRL::Scene3D::SetPerspective(Angle::FromDegrees(60.0f));



    // Sky via VDP2 (componente reutilizavel)

    SRL::VDP2::SetBackColor(HighColor::FromRGB555(0, 0, 31)); // fallback azul



    const bool enableBg = true; // desativa background para liberar HWR
    BackgroundManager bgManager;
    if (enableBg)
    {
        const char* skyPaths[] = {"cd/data/skybox_1.tga","data/skybox_1.tga","skybox_1.tga","cd/data/SKYBOX_1.TGA","data/SKYBOX_1.TGA","SKYBOX_1.TGA"};
        bgManager.Init(skyPaths, sizeof(skyPaths) / sizeof(skyPaths[0]));
    }



    // Camera base (Saturn: Y+ para baixo; Y- acima)

    Camera::State cameraState{
        .yawDeg = 180,
        .pitchDeg = -21,
        .viewYawDeg = 0,
        .viewPitchDeg = 0,
        .radius = Fxp(46.0f),
        .strafe = Vector3D(Fxp::Convert(0), Fxp::Convert(-4), Fxp::Convert(-2)),
        .location = Vector3D(0.0, 0.0, -50.0f),
        .yaw = Angle::FromDegrees(Fxp::Convert(180)),
        .pitch = Angle::FromDegrees(Fxp::Convert(-21)),
        .viewYaw = Angle::FromDegrees(Fxp::Convert(0)),
        .viewPitch = Angle::FromDegrees(Fxp::Convert(0)),
    };

    Camera::Tuning cameraTuning{};

    Camera::RefreshAngles(cameraState);
    cameraState.location = Camera::OrbitPosition(cameraState.yaw, cameraState.pitch, cameraState.radius) + cameraState.strafe;



    Vector3D lightDirection = Vector3D(0.35, -0.15, 0.35);
    SRL::Types::HighColor lightColor = SRL::Types::HighColor::FromRGB555(31, 31, 31);

    SRL::Scene3D::SetDirectionalLight(lightDirection);
    SRL::Scene3D::LightSetColor(lightColor);



    // Prepare Gouraud/light tables if smooth

    std::vector<HighColor> workTable;

    std::vector<uint8_t> vertWork;
    std::vector<HighColor> trackWorkTable;
    std::vector<uint8_t> trackVertWork;

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

    // Draw order: wheels first (1..4), then body (0)

    std::array<size_t, 5> drawOrder = {1, 2, 3, 4, 0};

    size_t orderCount = (meshCount < 5) ? meshCount : 5;

    CarRenderer::Config carConfig{modelCenter, lightDirection, drawOrder, orderCount};
    std::unique_ptr<CarRenderer> carRendererPtr;
    if (carValid && carPtr)
    {
        carRendererPtr = std::make_unique<CarRenderer>(*carPtr, isSmoothMesh, carConfig);
        carRendererPtr->SetWheel1Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));
        carRendererPtr->SetWheel2Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));
        carRendererPtr->SetWheel3Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));
        carRendererPtr->SetWheel4Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));
    }



    // Input e calculo de camera orbitando o modelo

    SRL::Input::Digital pad(0);

    int32_t carYawDeg = 0;



    // Compute bounds for debugging

    SRL::Math::Types::Vector3D minV = Vector3D(32767, 32767, 32767);

    SRL::Math::Types::Vector3D maxV = Vector3D(-32768, -32768, -32768);

    for (size_t m = 0; m < meshCount; ++m)

    {

        if (isSmoothMesh)

        {

            auto* mesh = carPtr ? carPtr->template GetMesh<SRL::Types::SmoothMesh>(m) : nullptr;

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

            auto* mesh = carPtr ? carPtr->template GetMesh<SRL::Types::Mesh>(m) : nullptr;

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

    }



    HudStats hudStats;

    hudStats.Init(faceCount, vertexCount, meshCount, isSmoothMesh, modelCenter, minV, maxV);

    CameraRig::OrbitState xOrbitState{};

    // Pista INTLAGOS: caminhos em cd/data (nome sem variacoes)
    static const char* trackPaths[] = {
        "cd/data/INTLAGOS.NYA",
        "cd/data/INTLAGOS.NYA;1",
        "CD/DATA/INTLAGOS.NYA",
        "CD/DATA/INTLAGOS.NYA;1",
        "INTLAGOS.NYA",
        "INTLAGOS.NYA;1"
    };
    // Usa valor alto para carregar todos os meshes, mas sem texturas (maxMeshes>0 zera texturas no loader)
    const size_t maxTrackMeshes = 65535;
    if (enableTrack)
    {
        TrackLoadResult trRes = LoadTrackToCart(trackPaths, sizeof(trackPaths)/sizeof(trackPaths[0]), maxTrackMeshes);
        hasTrack = trRes.loaded;
        isTrackSmooth = trRes.isSmooth;
        trackFaceCount = trRes.faceCount;
        trackVertexCount = trRes.vertexCount;
        trackMeshCount = trRes.meshCount;
        trackBounds = trRes.renderer.Bounds();
        trackRenderer = std::move(trRes.renderer);
        trackRenderer.SetStartMesh(0);
        trackRenderer.SetScale(Fxp::Convert(1));
        auto segCenter = trackRenderer.StartMeshCenter() + trackRenderer.Offset();
        MLOG(1, 2, "Track center:%d %d %d", segCenter.X.As<int16_t>(), segCenter.Y.As<int16_t>(), segCenter.Z.As<int16_t>());
    }

    while (1)

    {
        if (!cartOkFlag)
        {
            MLOG(1, 3, "ERRO: Cartucho 4MB ausente");
            MLOG(1, 4, "Insira cart DRAM e reinicie");
            continue;
        }

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

        // Rotaciona carro e camera (modo X) usando CameraRig utilitario
        CameraRig::HandleOrbitAroundCar(cameraState, carYawStepDeg, xHeld, lHeld, rHeld, carYawDeg, xOrbitState, true);

        // Controles de rodas: C inicia/resume, B para
        if (carRendererPtr)
        {
            if (cHeld) { carRendererPtr->StartAllWheels(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15))); carRendererPtr->ResumeAllWheels(); }
            if (bHeld) { carRendererPtr->StopAllWheels(); }
        }

// Atualiza skybox VDP2
        if (enableBg) bgManager.Update(cameraState); // mant'm VDP2 background ativo

        Vector3D cameraLocation = cameraState.location;
        Vector3D lookTarget = Camera::ComputeLookTarget(cameraState, cameraTuning, pad, modelCenter);
        MLOG(0, 18, "Cam pos: %d %d %d", cameraLocation.X.As<int16_t>(), cameraLocation.Y.As<int16_t>(), cameraLocation.Z.As<int16_t>());
        // lookTarget padrao segue o alvo calculado (b livre)
        hudStats.Update(cameraState, modelOffset, cameraLocation, modelCenter);

        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(cameraLocation, lookTarget, Angle::FromDegrees(0.0));
        // Debug: posicoes das rodas
        // Logs restritos para pista; removidos logs das rodas/carro
        // Pista antes do carro
        // Pista desligada

        // Render pista (VDP1 via Scene2D)
        if (renderTrack && hasTrack) {
            trackRenderer.Render(lightDirection, cameraLocation);
        }

        // Carro ligado
        if (carRendererPtr)
        {
            carRendererPtr->rotY = Angle::FromDegrees(Fxp::Convert(carYawDeg));
            carRendererPtr->Render();
        }

        if (renderAxes)
        {
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
        }
        static uint32_t frameCounter = 0;
        ++frameCounter;
        if ((frameCounter & 63) == 0)
        {
            int32_t hwrFree = SRL::Memory::CartRam::GetFreeSpace();
            int32_t hwrTotal = 4 * 1024 * 1024;
            int32_t hwrUsed  = hwrTotal - hwrFree;
            int32_t vdp1TexCount = SRL::VDP1::GetTextureCount();
            size_t vdp1Free  = SRL::VDP1::GetAvailableMemory();
            size_t vdp1Used  = SRL::VDP1::GetUsedMemory();
            size_t vdp1Total = vdp1Used + vdp1Free;
            uint32_t vdp1Pct = (vdp1Total > 0) ? static_cast<uint32_t>((vdp1Used * 100) / vdp1Total) : 0;
            int32_t hwrPct10 = (hwrTotal > 0) ? (hwrUsed * 1000 / hwrTotal) : 0;
            if (logCar) MLOG(0, 24, "Car faces:%u verts:%u",
                              (unsigned)faceCount, (unsigned)vertexCount);
            if (logTrack) MLOG(0, 25, "HWR used:%d free:%d", hwrUsed, hwrFree);
            if (logTrack) MLOG(0, 26, "HWR pct:%d.%d%%", hwrPct10/10, hwrPct10%10);
            if (logTrack) MLOG(0, 27, "VDP1 textures:%d", vdp1TexCount);
            if (logTrack) MLOG(0, 28, "VDP1 mem used:%u free:%u pct:%u%%", (unsigned)vdp1Used, (unsigned)vdp1Free, vdp1Pct);
        }
        SRL::Core::Synchronize();

    }



    return 0;

}

int main()
{
    GameApp app;
    return app.Run();
}






















