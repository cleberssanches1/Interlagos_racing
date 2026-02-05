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
#include <cstddef>

extern "C" void __throw_bad_array_new_length() {}
extern "C" void __throw_bad_alloc() {}
namespace std { void __throw_bad_array_new_length() {} void __throw_bad_alloc() {} }


#include "resource_loader.hpp"

using namespace SRL::Types;

using namespace SRL::Math::Types;

// Logs essenciais na tela (reduzido)
constexpr bool kLog = true;
constexpr bool kCarLogs = false;
#define MLOG(...) do { if constexpr (kLog) { SRL::Debug::Print(__VA_ARGS__); } } while(0)

enum class TrackPipelineStage
{
    Idle,
    Serializing,
    Buffering,
    Ready,
    Failed
};

static const char* TrackPipelineStageToString(TrackPipelineStage stage)
{
    switch (stage)
    {
        case TrackPipelineStage::Idle:         return "Idle";
        case TrackPipelineStage::Serializing:  return "Serializando";
        case TrackPipelineStage::Buffering:    return "Bufferizando";
        case TrackPipelineStage::Ready:        return "Pronto";
        case TrackPipelineStage::Failed:       return "Falhou";
    }
    return "Desconhecido";
}

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

// Representa o pipeline de carga do carro: cart (DRAM 4MB) e c??????pia opcional na WRAM.
struct CarPipeline
{
    CarLoadResult cart;                     // Resultado da carga obrigat??????ria no cart.
    std::unique_ptr<ModelObject> wramCopy;  // C??????pia independente na work RAM.

    // Retorna o modelo ativo (c??????pia em WRAM se existir, sen?????o o do cart).
    ModelObject* ActiveModel() const { return wramCopy ? wramCopy.get() : cart.car; }

    // Indica se h????? um modelo utiliz?????vel.
    bool Loaded() const { return cart.loaded && ActiveModel(); }
};

// Executa a carga CD -> cart (4MB) e opcionalmente cart -> WRAM.
static CarPipeline LoadCarPipeline(const char* const* paths, size_t pathCount, bool makeWramCopy)
{
    CarPipeline pipe{};
    const char* chosenPath = FindExistingPath(paths, pathCount);

    // 1) Carga principal no cart (forceCart = true garante DRAM 4MB).
    pipe.cart = LoadCarToCart(paths, pathCount, /*forceCart*/true);

    // 2) C??????pia independente em WRAM para evitar compartilhar ponteiros do cart.
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

    const bool logCar = kCarLogs;
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
    const bool renderTrack = false; // deixa pipeline intacta, mas não desenha os segmentos
    const bool renderCar = true; // carro ligado
    const bool renderAxes = false; // desliga eixos de debug

    const bool carWasSmooth = carPtr ? carPtr->IsSmooth() : false;
    const bool isSmoothMesh = carWasSmooth; // restaura carregamento smooth
    MLOG(1, 1, "CAR1.NYA load (smooth flag:%d)", carWasSmooth ? 1 : 0);

    uint32_t faceCount = carPtr ? carPtr->GetFaceCount() : 0;

    uint32_t vertexCount = carPtr ? carPtr->GetVertexCount() : 0;

    uint32_t meshCount = carPtr ? carPtr->GetMeshCount() : 0;

    if constexpr (kCarLogs)
    {
        MLOG(1, 10, "CarPipeline OK ptr:%08lx faces:%u verts:%u meshes:%u smooth:%d",
             (unsigned long)carPtr,
             faceCount,
             vertexCount,
             meshCount,
             isSmoothMesh ? 1 : 0);
    }

        if (carPtr)
    {
        int32_t firstTexture = carPtr->GetFirstTextureIndex();
        size_t texCount = carPtr->GetTextureCount();
        if (firstTexture >= 0 && texCount > 0)
        {
            bool textureSlotError = false;
            for (size_t mi = 0; mi < meshCount && !textureSlotError; ++mi)
            {
                if (isSmoothMesh)
                {
                    auto* mesh = carPtr->GetMesh<SRL::Types::SmoothMesh>(mi);
                    if (!mesh || mesh->Attributes == nullptr) continue;
                    for (size_t fi = 0; fi < mesh->FaceCount; ++fi)
                    {
                        const auto& attr = mesh->Attributes[fi];
                        if (attr.Texture != No_Texture)
                        {
                            if (attr.Texture < firstTexture || attr.Texture >= firstTexture + static_cast<int32_t>(texCount))
                            {
                                if constexpr (kCarLogs)
                                {
                                    MLOG(1, 16, "Car texture slot inv??lido mesh:%zu face:%zu tex:%u outside [%d,%zu)", mi, fi, (unsigned)attr.Texture, firstTexture, texCount);
                                }
                                textureSlotError = true;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    auto* mesh = carPtr->GetMesh<SRL::Types::Mesh>(mi);
                    if (!mesh || mesh->Attributes == nullptr) continue;
                    for (size_t fi = 0; fi < mesh->FaceCount; ++fi)
                    {
                        const auto& attr = mesh->Attributes[fi];
                        if (attr.Texture != No_Texture)
                        {
                            if (attr.Texture < firstTexture || attr.Texture >= firstTexture + static_cast<int32_t>(texCount))
                            {
                                if constexpr (kCarLogs)
                                {
                                    MLOG(1, 16, "Car texture slot inv??lido mesh:%zu face:%zu tex:%u outside [%d,%zu)", mi, fi, (unsigned)attr.Texture, firstTexture, texCount);
                                }
                                textureSlotError = true;
                                break;
                            }
                        }
                    }
                }
            }
            if constexpr (kCarLogs)
            {
                if (!textureSlotError)
                {
                    MLOG(1, 17, "Car texture slots OK first:%d count:%zu", firstTexture, texCount);
                }
            }
        }
        else
        {
            if constexpr (kCarLogs)
            {
                MLOG(1, 17, "Car texture slots indispon??veis primeiro:%d count:%zu", firstTexture, texCount);
            }
        }
    }

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
        .pitchDeg = -10,
        .viewYawDeg = 0,
        .viewPitchDeg = 0,
        .radius = Fxp(55.2f),
        .strafe = Vector3D(Fxp::Convert(0), Fxp::Convert(-13.6), Fxp::Convert(-2)),
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
    Vector3D carWorldPosition(0.0, 0.0, 0.0);

    // Draw order: wheels first (1..4), then body (0)

    std::array<size_t, 5> drawOrder = {1, 2, 3, 4, 0};

    size_t orderCount = (meshCount < 5) ? meshCount : 5;

    constexpr size_t kCrashSkipMeshId = SIZE_MAX;
    CarRenderer::Config carConfig{modelCenter, lightDirection, drawOrder, orderCount};
    carConfig.wireframeOnly = false;
    std::unique_ptr<CarRenderer> carRendererPtr;
    if (carValid && carPtr)
    {
        carRendererPtr = std::make_unique<CarRenderer>(*carPtr, isSmoothMesh, carConfig);
        carRendererPtr->SetSkipMesh(kCrashSkipMeshId);
        carRendererPtr->SetWheel1Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));
        carRendererPtr->SetWheel2Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));
        carRendererPtr->SetWheel3Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));
        carRendererPtr->SetWheel4Step(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15)));
        if constexpr (kCarLogs)
        {
            MLOG(1, 12, "CarRenderer criado ptr:%08lx meshCenters:%zu",
                 (unsigned long)carRendererPtr.get(),
                 carRendererPtr->MeshCenters().size());
        }
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
    constexpr size_t kTrackBufferMeshes = 40;
    constexpr size_t kTrackDrawSegments = 20;
    TrackRenderer trackRenderer;
    bool trackReady = false;
    size_t trackSegmentIndex = 0;
    bool trackUpLatch = false;
    bool trackDownLatch = false;
    TrackPipelineStage trackStage = TrackPipelineStage::Idle;
    const char* trackStageMessage = "aguardando iniciar";
    const char* trackStageError = nullptr;
    bool trackErrorLogged = false;
    auto LogTrackStage = [&](TrackPipelineStage stage, const char* message, const char* error = nullptr)
    {
        trackStage = stage;
        trackStageMessage = message;
        trackStageError = error ? error : message;
        trackErrorLogged = false;
        MLOG(1, 5, "Track stage: %s - %s", TrackPipelineStageToString(stage), message);
    };

    // Pista INTLAGOS: caminhos em cd/data (nome sem variacoes)
    static const char* trackPaths[] = {
        "cd/data/INTLAGOS.NYA",
        "cd/data/INTLAGOS.NYA;1",
        "CD/DATA/INTLAGOS.NYA",
        "CD/DATA/INTLAGOS.NYA;1",
        "INTLAGOS.NYA",
        "INTLAGOS.NYA;1"
    };
    const size_t trackPathCount = sizeof(trackPaths)/sizeof(trackPaths[0]);
    const char* trackSource = FindExistingPath(trackPaths, trackPathCount);
    TrackSerializedCopy trackSerial;
    if (trackSource)
    {
        LogTrackStage(TrackPipelineStage::Serializing, "Serializando INTLAGOS.NYA no cart");
        trackSerial = SerializeTrackToCart(trackSource);
        if (trackSerial.Valid())
        {
            MLOG(1, 9, "Track serialized (passo ok) cart:%08lx sz:%u", (unsigned long)trackSerial.cartPtr, (unsigned)trackSerial.size);
            LogTrackStage(TrackPipelineStage::Buffering, "Preparando buffer de segmentos a partir do cart");
            trackReady = trackRenderer.LoadFromSerialized(trackSerial, kTrackBufferMeshes);
            if (trackReady)
            {
                trackRenderer.SetDrawLimit(kTrackDrawSegments);
                trackRenderer.SetSglDirect(true);
                trackRenderer.SetUseOriginal(false);
                trackRenderer.SetDirect2D(false);
                LogTrackStage(TrackPipelineStage::Ready, "Buffer pronto para renderizar segmentos");
                auto firstCenter = trackRenderer.StartMeshCenter();
                carWorldPosition = firstCenter;
                if (carRendererPtr)
                {
                    carRendererPtr->SetWorldPosition(carWorldPosition);
                }
                const int16_t trackX = firstCenter.X.As<int16_t>();
                const int16_t trackY = firstCenter.Y.As<int16_t>();
                const int16_t trackZ = firstCenter.Z.As<int16_t>();
                const int16_t carX = carWorldPosition.X.As<int16_t>();
                const int16_t carY = carWorldPosition.Y.As<int16_t>();
                const int16_t carZ = carWorldPosition.Z.As<int16_t>();
                MLOG(1, 6, "Track seg 0 ct: %d %d %d", trackX, trackY, trackZ);
                MLOG(1, 7, "Car World pos: %d %d %d", carX, carY, carZ);
                MLOG(1, 5, "Track buffer pronto meshes:%zu faces:%u draw:%zu",
                     trackRenderer.MeshCount(), trackRenderer.FaceCount(), trackRenderer.DrawLimit());
            }
            else
            {
                LogTrackStage(TrackPipelineStage::Failed, "Falha ao preparar buffer de track", "trackRenderer.Load falhou");
            }
        }
        else
        {
            LogTrackStage(TrackPipelineStage::Failed, "Falha ao serializar track", "SerializeTrackToCart falhou");
            MLOG(1, 9, "Falha ao serializar track %s", trackSource);
        }
    }
    else
    {
        LogTrackStage(TrackPipelineStage::Failed, "INTLAGOS.NYA nao encontrado", "arquivo nao localizado");
    }
    // Usa valor alto para carregar todos os meshes, mas sem texturas (maxMeshes>0 zera texturas no loader)
    const size_t maxTrackMeshes = 65535;
    if (trackSerial.Valid())
    {
        MLOG(1, 2, "Track serialized, mantendo pipeline de meshes pausada");
        // N??o instanciamos meshes nem renderizamos o track; apenas mantemos o .NYA copiado.
    }

    static uint32_t frameCounter = 0;

    while (1)
    {
        if (!cartOkFlag)
        {
            MLOG(1, 3, "ERRO: Cartucho 4MB ausente");
            MLOG(1, 4, "Insira cart DRAM e reinicie");
            continue;
        }

        if (trackStage == TrackPipelineStage::Failed && !trackErrorLogged)
        {
            MLOG(1, 21, "Track pipeline falhou: %s", trackStageError ? trackStageError : trackStageMessage);
            trackErrorLogged = true;
        }
        Camera::UpdateInput(cameraState, cameraTuning, pad);

        const bool aHeld = pad.IsHeld(SRL::Input::Digital::Button::A);
        const bool bHeld = pad.IsHeld(SRL::Input::Digital::Button::B);
        const bool cHeld = pad.IsHeld(SRL::Input::Digital::Button::C);
        const bool xHeld = pad.IsHeld(SRL::Input::Digital::Button::X);
        const bool lHeld = pad.IsHeld(SRL::Input::Digital::Button::L);
        const bool rHeld = pad.IsHeld(SRL::Input::Digital::Button::R);
        const bool upHeld = pad.IsHeld(SRL::Input::Digital::Button::Up);
        const bool downHeld = pad.IsHeld(SRL::Input::Digital::Button::Down);
        const bool zHeld = pad.IsHeld(SRL::Input::Digital::Button::Z);


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

        if (trackReady && trackRenderer.MeshCount() > 0)
        {
            const size_t segmentCount = trackRenderer.MeshCount();
            if (upHeld && !trackUpLatch)
            {
                trackSegmentIndex = (trackSegmentIndex + segmentCount - 1) % segmentCount;
            }
            trackUpLatch = upHeld;

            if (downHeld && !trackDownLatch)
            {
                trackSegmentIndex = (trackSegmentIndex + 1) % segmentCount;
            }
            trackDownLatch = downHeld;
        }

// Atualiza skybox VDP2
        if (enableBg) bgManager.Update(cameraState); // mant'm VDP2 background ativo

        Vector3D orbitOffset = cameraState.location;
        Vector3D cameraLocation = orbitOffset + carWorldPosition;
        const Fxp kLookDownOffset = Fxp::Convert(6.0f);
        Vector3D lookTarget = carWorldPosition + Vector3D(Fxp::Convert(0), -kLookDownOffset, Fxp::Convert(0));
        if (zHeld)
        {
            Vector3D viewOffset = Camera::OrbitPosition(cameraState.viewYaw, cameraState.viewPitch, cameraTuning.targetDistance);
            lookTarget = cameraLocation + viewOffset;
        }
        if (frameCounter == 0 || (frameCounter & 63) == 0)
        {
            MLOG(1, 9, "Car mesh center %u: %d %d %d",
                 carRendererPtr ? carRendererPtr->LastMeshDrawn() : 0,
                 carWorldPosition.X.As<int16_t>(), carWorldPosition.Y.As<int16_t>(), carWorldPosition.Z.As<int16_t>());
        }
        MLOG(0, 18, "Cam pos: %d %d %d", cameraLocation.X.As<int16_t>(), cameraLocation.Y.As<int16_t>(), cameraLocation.Z.As<int16_t>());
        // lookTarget padrao segue o alvo calculado (b livre)
        hudStats.Update(cameraState, modelOffset, cameraLocation, carWorldPosition);

        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(cameraLocation, lookTarget, Angle::FromDegrees(0.0));
        // Debug: posicoes das rodas
        // Logs restritos para pista; removidos logs das rodas/carro
        // Pista antes do carro
        if (renderTrack && trackReady && trackRenderer.MeshCount() > 0)
        {
            trackRenderer.SetStartMesh(trackSegmentIndex);
            trackRenderer.Render(lightDirection, cameraLocation);
        }

        // Carro ligado
        if (renderCar && carRendererPtr)
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
            if constexpr (kCarLogs)
            {
                if (logCar) MLOG(0, 24, "Car faces:%u verts:%u",
                                  (unsigned)faceCount, (unsigned)vertexCount);
            }
            if (logTrack) MLOG(0, 25, "HWR used:%d free:%d", hwrUsed, hwrFree);
            if (logTrack) MLOG(0, 26, "HWR pct:%d.%d%%", hwrPct10/10, hwrPct10%10);
            if (logTrack) MLOG(0, 27, "VDP1 textures:%d", vdp1TexCount);
            if (logTrack) MLOG(0, 28, "VDP1 mem used:%u free:%u pct:%u%%", (unsigned)vdp1Used, (unsigned)vdp1Free, vdp1Pct);
        }
        MLOG(1, 15, "SRL::Core::Synchronize frame:%u", frameCounter);
        SRL::Core::Synchronize();

    }



    return 0;

}

int main()
{
    GameApp app;
    return app.Run();
}






















