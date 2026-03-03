#include <algorithm>
#include <srl.hpp>

#include "modelObject.hpp"

#include "camera_system.hpp"

#include "car_system.hpp"
#include "render_pipeline.hpp"
#include "runtime_null_systems.hpp"
#include "simple_audio_events.hpp"
#include "simple_car_physics.hpp"
#include "simple_gameplay_tick.hpp"
#include "track_collision_query.hpp"

#include "hud_system.hpp"

#include "application_state.hpp"
#include "background_manager.hpp"
#include "game_loop_system.hpp"
#include "track_system.hpp"

#include <array>
#include <memory>
#include <cstddef>
#include <vector>
#include <cstdio>

extern "C" [[noreturn]] void __throw_bad_array_new_length() { while (1) {} }
extern "C" [[noreturn]] void __throw_bad_alloc() { while (1) {} }
namespace std { [[noreturn]] void __throw_bad_array_new_length() { while (1) {} } [[noreturn]] void __throw_bad_alloc() { while (1) {} } }


#include "resource_loader.hpp"

using namespace SRL::Types;

using namespace SRL::Math::Types;

// Logs essenciais na tela (reduzido)
constexpr bool kLog = true;
constexpr bool kCarLogs = false;
constexpr bool kVerboseFrameLogs = false;
#define MLOG(...) do { if constexpr (kLog) { SRL::Debug::Print(__VA_ARGS__); } } while(0)

static const char* FindExistingPath(const char* const* paths, size_t count);
static constexpr size_t kCarGouraudOffset = 4096;

// VBlank handler without Event dispatch to avoid invalid callback jumps in OnVblank.
static void SafeVblankNoEvent()
{
    slGetStatus();
    SRL::Input::Management::RefreshPeripherals();
    SRL::Input::Gun::VblankRefresh();
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
static CarPipeline LoadCarPipeline(const char* const* paths, size_t pathCount, bool makeWramCopy, size_t gouraudOffset)
{
    CarPipeline pipe{};
    const char* chosenPath = FindExistingPath(paths, pathCount);

    // 1) Carga principal no cart (forceCart = true garante DRAM 4MB).
    pipe.cart = LoadCarToCart(paths, pathCount, /*forceCart*/true, gouraudOffset);

    // 2) C??????pia independente em WRAM para evitar compartilhar ponteiros do cart.
    if (makeWramCopy && chosenPath)
    {
        pipe.wramCopy = std::make_unique<ModelObject>(chosenPath, gouraudOffset, false, 0, false, false, false);
        if (!pipe.wramCopy || pipe.wramCopy->GetMeshCount() == 0 || pipe.wramCopy->GetFaceCount() == 0)
        {
            // Se a copia por caminho falhar, preserva o modelo carregado no cart.
            pipe.wramCopy.reset();
        }
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
    AppState::Set(AppState::Stage::CoreInit, 0);

    SRL::Core::Initialize(HighColor(0x10, 0x20, 0x18));
    slIntFunction(SafeVblankNoEvent);
    AppState::PresentOverlay(2);

    const bool logCar = kCarLogs;
    const bool logTrack = false;
    // Log inicial simples do Cart e HWR
    auto crep = SRL::Memory::CartRam::GetReport();
    // SRL::Debug::Print(0, 0, "CRT ok:%d free:%d total:%d", crep.TotalSize > 0 ? 1 : 0, (int)crep.FreeSize, (int)crep.TotalSize);
    auto rep = SRL::Memory::HighWorkRam::GetReport();
    // SRL::Debug::Print(0, 1, "HWR free:%d total:%d", (int)rep.FreeSize, (int)rep.TotalSize);
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
        // MLOG(0, 6, "CRT addr:%08lx", (unsigned long)hwrStr);
        // MLOG(0, 7, "CRT free b:%d", hwrBeforeStr);
        // MLOG(0, 8, "CRT free a:%d", hwrAfterStr);
        // MLOG(0, 9, "CRT txt:%s", hwrStr);
    }
    else
    {
    // silencia logs do teste HWR
    }

    const bool renderTrack = true; // teste pista
    const bool renderCar = false; // somente pista
    const bool loadCarAfterTrack = true; // pista primeiro, depois carro
    const bool enableTrackSlaveProducer = false; // diagnostico: desativa Slave para estabilizar
    const bool forceSolidCarWhenTrack = false; // desativado: pode causar comando invalido na VDP1
    const bool renderAxes = false; // desliga eixos de debug

        // Carrega carro na DRAM do cart (somente se renderCar estiver ativo)
    const char* carPaths[] = {
        "CD/DATA/CAR1.NYA;1", "CD/DATA/CAR1.NYA",
        "DATA/CAR1.NYA;1", "DATA/CAR1.NYA",
        "CAR1.NYA;1", "CAR1.NYA",
        "car1.nya;1", "car1.nya"
    };
    const bool useCartCopyPipeline = true; // cart -> WRAM -> VDP1
    AppState::Set(AppState::Stage::CarLoad, 0);
    CarPipeline carPipe{};
    if (renderCar && !loadCarAfterTrack)
    {
        carPipe = LoadCarPipeline(carPaths, sizeof(carPaths)/sizeof(carPaths[0]), useCartCopyPipeline, kCarGouraudOffset);
    }

    ModelObject* carPtr = carPipe.ActiveModel();
    bool carValid = carPipe.Loaded();
    if (!carValid && logCar)
    {
        MLOG(0, 7, "Carro nao carregou (meshes/faces zero)");
    }
// Se faltar cart, travamos o loop exibindo a mensagem
    bool cartOkFlag = cartOk;
    SRL::Math::Types::Vector3D trackSegOffset(0, 0, 0);

    bool carWasSmooth = carPtr ? carPtr->IsSmooth() : false;
    bool isSmoothMesh = carWasSmooth; // restaura carregamento smooth
    // MLOG(1, 1, "CAR1.NYA load (smooth flag:%d)", carWasSmooth ? 1 : 0);

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
                                    MLOG(1, 16, "Car texture slot inv??lido mesh:%lu face:%lu tex:%u outside [%d,%lu)",
                                         (unsigned long)mi, (unsigned long)fi, (unsigned)attr.Texture, firstTexture, (unsigned long)texCount);
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
                                    MLOG(1, 16, "Car texture slot inv??lido mesh:%lu face:%lu tex:%u outside [%d,%lu)",
                                         (unsigned long)mi, (unsigned long)fi, (unsigned)attr.Texture, firstTexture, (unsigned long)texCount);
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
            MLOG(1, 17, "Car texture slots OK first:%d count:%lu", firstTexture, (unsigned long)texCount);
                }
            }
        }
        else
        {
            if constexpr (kCarLogs)
            {
                MLOG(1, 17, "Car texture slots indispon??veis primeiro:%d count:%lu", firstTexture, (unsigned long)texCount);
            }
        }
    }

    // Simple frustum

    SRL::Scene3D::SetPerspective(Angle::FromDegrees(60.0f));



    // Sky via VDP2 (componente reutilizavel)

    SRL::VDP2::SetBackColor(HighColor::FromRGB555(0, 0, 31)); // fallback azul



    const bool enableBg = true; // desativa background para liberar HWR
    AppState::Set(AppState::Stage::BackgroundInit, 0);
    BackgroundManager bgManager;
    bool bgReady = false;
    const char* skyPaths[] = {
        "CD/SKYBOX_1.TGA",
        "CD/SKYBOX_1.TGA;1",
        "CD/DATA/SKYBOX_1.TGA",
        "CD/DATA/SKYBOX_1.TGA;1",
        "cd/skybox_1.tga",
        "cd/skybox_1.tga;1",
        "cd/data/skybox_1.tga",
        "cd/data/skybox_1.tga;1",
        "DATA/SKYBOX_1.TGA",
        "DATA/SKYBOX_1.TGA;1",
        "SKYBOX_1.TGA",
        "SKYBOX_1.TGA;1",
        "skybox_1.tga",
        "skybox_1.tga;1"
    };
    if (enableBg)
    {
        SRL::Cd::ChangeDir((const char*)0);
        bgReady = bgManager.Init(skyPaths, sizeof(skyPaths) / sizeof(skyPaths[0]));
        // log removido
    }

    // Camera system owns camera state, tuning and input workflow.
    CameraSystem cameraSystem;

    Vector3D lightDirection = Vector3D(0.35, -0.15, 0.35);
    SRL::Types::HighColor lightColor = SRL::Types::HighColor::FromRGB555(31, 31, 31);

    SRL::Scene3D::SetDirectionalLight(lightDirection);
    SRL::Scene3D::LightSetColor(lightColor);



    // Prepare Gouraud/light tables after track init, so track-only mode still gets lighting.
    std::vector<HighColor> workTable;
    std::vector<uint8_t> vertWork;
    std::vector<HighColor> trackWorkTable;
    std::vector<uint8_t> trackVertWork;



    // Center of model from bounds to keep imported cars in camera view.
    Vector3D modelCenter = Vector3D(0.0, 0.0, 0.0);
    if (carPtr && meshCount > 0)
    {
        SRL::Math::Types::Vector3D minCar = Vector3D(32767, 32767, 32767);
        SRL::Math::Types::Vector3D maxCar = Vector3D(-32768, -32768, -32768);
        for (size_t m = 0; m < meshCount; ++m)
        {
            if (isSmoothMesh)
            {
                auto* mesh = carPtr->template GetMesh<SRL::Types::SmoothMesh>(m);
                if (!mesh) continue;
                for (size_t v = 0; v < mesh->VertexCount; ++v)
                {
                    const auto& p = mesh->Vertices[v];
                    minCar.X = SRL::Math::Min(minCar.X, p.X);
                    minCar.Y = SRL::Math::Min(minCar.Y, p.Y);
                    minCar.Z = SRL::Math::Min(minCar.Z, p.Z);
                    maxCar.X = SRL::Math::Max(maxCar.X, p.X);
                    maxCar.Y = SRL::Math::Max(maxCar.Y, p.Y);
                    maxCar.Z = SRL::Math::Max(maxCar.Z, p.Z);
                }
            }
            else
            {
                auto* mesh = carPtr->template GetMesh<SRL::Types::Mesh>(m);
                if (!mesh) continue;
                for (size_t v = 0; v < mesh->VertexCount; ++v)
                {
                    const auto& p = mesh->Vertices[v];
                    minCar.X = SRL::Math::Min(minCar.X, p.X);
                    minCar.Y = SRL::Math::Min(minCar.Y, p.Y);
                    minCar.Z = SRL::Math::Min(minCar.Z, p.Z);
                    maxCar.X = SRL::Math::Max(maxCar.X, p.X);
                    maxCar.Y = SRL::Math::Max(maxCar.Y, p.Y);
                    maxCar.Z = SRL::Math::Max(maxCar.Z, p.Z);
                }
            }
        }
        modelCenter = Vector3D(
            (minCar.X + maxCar.X) / 2,
            (minCar.Y + maxCar.Y) / 2,
            (minCar.Z + maxCar.Z) / 2);
    }
    Vector3D modelOffset(-modelCenter.X, -modelCenter.Y, -modelCenter.Z);
    Vector3D carWorldPosition(0.0, 0.0, 0.0);

    AppState::Set(AppState::Stage::TrackInit, 0);
    TrackSystem trackSystem;
    TrackSystem::Config trackConfig{};
    trackConfig.initialSegments = 7;
    trackConfig.minSegments = 7;
    // Conservative track budget to keep VDP1 command list stable with car rendering enabled.
    trackConfig.initialMeshes = 512;
    trackConfig.initialFaces = 5000;
    trackConfig.useSlave = enableTrackSlaveProducer;
    SRL::Cd::ChangeDir((const char*)0);
    const bool trackSystemReady = renderTrack ? trackSystem.Initialize(trackConfig) : false;
    if (enableBg && !bgReady)
    {
        SRL::Cd::ChangeDir((const char*)0);
        bgReady = bgManager.Init(skyPaths, sizeof(skyPaths) / sizeof(skyPaths[0]));
        // log removido
    }
    if (renderTrack && trackSystemReady)
    {
        Vector3D seg01Center(0.0, 0.0, 0.0);
        if (trackSystem.FindSegmentCenterById(1, trackSegOffset, seg01Center))
        {
            // Segment center is an AABB center; keep car spawn Y stable to avoid starting inside geometry.
            carWorldPosition.X = seg01Center.X;
            carWorldPosition.Z = seg01Center.Z;
            carWorldPosition.Y = SRL::Math::Types::Fxp::BuildRaw(0);
            // Car spawn debug log disabled to keep on-screen diagnostics concise.
        }
    }
    if (renderCar && loadCarAfterTrack)
    {
        AppState::Set(AppState::Stage::CarLoad, 1);
        SRL::Cd::ChangeDir((const char*)0);
        carPipe = LoadCarPipeline(carPaths, sizeof(carPaths) / sizeof(carPaths[0]), useCartCopyPipeline, kCarGouraudOffset);
        carPtr = carPipe.ActiveModel();
        carValid = carPipe.Loaded();
        carWasSmooth = carPtr ? carPtr->IsSmooth() : false;
        isSmoothMesh = carWasSmooth;
        faceCount = carPtr ? carPtr->GetFaceCount() : 0;
        vertexCount = carPtr ? carPtr->GetVertexCount() : 0;
        meshCount = carPtr ? carPtr->GetMeshCount() : 0;
        if (!carValid && logCar)
        {
            MLOG(0, 7, "Carro nao carregou (meshes/faces zero)");
        }

        // Recompute model center now that car was loaded after track textures.
        modelCenter = Vector3D(0.0, 0.0, 0.0);
        if (carPtr && meshCount > 0)
        {
            SRL::Math::Types::Vector3D minCar = Vector3D(32767, 32767, 32767);
            SRL::Math::Types::Vector3D maxCar = Vector3D(-32768, -32768, -32768);
            for (size_t m = 0; m < meshCount; ++m)
            {
                if (isSmoothMesh)
                {
                    auto* mesh = carPtr->template GetMesh<SRL::Types::SmoothMesh>(m);
                    if (!mesh) continue;
                    for (size_t v = 0; v < mesh->VertexCount; ++v)
                    {
                        const auto& p = mesh->Vertices[v];
                        minCar.X = SRL::Math::Min(minCar.X, p.X);
                        minCar.Y = SRL::Math::Min(minCar.Y, p.Y);
                        minCar.Z = SRL::Math::Min(minCar.Z, p.Z);
                        maxCar.X = SRL::Math::Max(maxCar.X, p.X);
                        maxCar.Y = SRL::Math::Max(maxCar.Y, p.Y);
                        maxCar.Z = SRL::Math::Max(maxCar.Z, p.Z);
                    }
                }
                else
                {
                    auto* mesh = carPtr->template GetMesh<SRL::Types::Mesh>(m);
                    if (!mesh) continue;
                    for (size_t v = 0; v < mesh->VertexCount; ++v)
                    {
                        const auto& p = mesh->Vertices[v];
                        minCar.X = SRL::Math::Min(minCar.X, p.X);
                        minCar.Y = SRL::Math::Min(minCar.Y, p.Y);
                        minCar.Z = SRL::Math::Min(minCar.Z, p.Z);
                        maxCar.X = SRL::Math::Max(maxCar.X, p.X);
                        maxCar.Y = SRL::Math::Max(maxCar.Y, p.Y);
                        maxCar.Z = SRL::Math::Max(maxCar.Z, p.Z);
                    }
                }
            }
            modelCenter = Vector3D(
                (minCar.X + maxCar.X) / 2,
                (minCar.Y + maxCar.Y) / 2,
                (minCar.Z + maxCar.Z) / 2);
        }
        modelOffset = Vector3D(-modelCenter.X, -modelCenter.Y, -modelCenter.Z);
    }
    if (renderTrack && renderCar && forceSolidCarWhenTrack && carValid && carPtr)
    {
        carPtr->ForceSolidColorPreserveDisplay(SRL::Types::HighColor::FromRGB555(20, 20, 20));
        MLOG(1, 18, "Car force solid ON");
    }
    // Track auto alignment disabled for raw NYA validation.
    // Keep offset at zero while testing exported segments.
    (void)trackSystemReady;
    AppState::PresentOverlay(2);

    uint32_t gouraudFaceCapacity = 0;
    uint32_t gouraudVertexCapacity = 0;
    if (carPtr && carWasSmooth)
    {
        // Car uses a dedicated gouraud offset range to avoid lighting aliasing with track.
        gouraudFaceCapacity = std::max(gouraudFaceCapacity, static_cast<uint32_t>(kCarGouraudOffset + faceCount + 64));
        gouraudVertexCapacity = std::max(gouraudVertexCapacity, vertexCount);
    }
    if (renderTrack && trackSystemReady && trackSystem.HasSmoothSegments())
    {
        gouraudFaceCapacity = std::max(gouraudFaceCapacity, trackSystem.MaxSegmentFaceCount());
        gouraudVertexCapacity = std::max(gouraudVertexCapacity, trackSystem.MaxSegmentVertexCount());
    }
    const bool enableSmoothLighting = (gouraudFaceCapacity > 0) && (gouraudVertexCapacity > 0);
    // Stability guard: disable VBlank gouraud callback until crash root-cause is fully isolated.
    const bool enableVblankGouraudCopy = false;
    if (enableSmoothLighting)
    {
        workTable.resize(static_cast<size_t>(gouraudFaceCapacity) << 2);
        vertWork.resize(static_cast<size_t>(gouraudVertexCapacity));
        SRL::Scene3D::LightInitGouraudTable(0, vertWork.data(), workTable.data(), gouraudFaceCapacity);
        SRL::Scene3D::LightSetGouraudTable(shadingTable);
        if (enableVblankGouraudCopy)
        {
            SRL::Core::OnVblank += SRL::Scene3D::LightCopyGouraudTable;
        }
    }

    // Draw order: wheels first (1..4), then body (0)

    std::array<size_t, 5> drawOrder = {1, 2, 3, 4, 0};

    size_t orderCount = (meshCount < 5) ? meshCount : 5;

    Game::CarSystem::Config carConfig{};
    carConfig.modelCenter = modelCenter;
    carConfig.lightDirection = lightDirection;
    carConfig.drawOrder = drawOrder;
    carConfig.orderCount = orderCount;
    carConfig.wireframeOnly = false;
    std::unique_ptr<Game::CarSystem> carSystem;
    if (carValid && carPtr)
    {
        carSystem = std::make_unique<Game::CarSystem>(carPtr, isSmoothMesh, carConfig);
        carSystem->SetWorldPosition(carWorldPosition);
        if constexpr (kCarLogs)
        {
            MLOG(1, 12, "CarSystem criado ptr:%08lx valid:%d",
                 (unsigned long)carSystem.get(),
                 carSystem->Valid() ? 1 : 0);
        }
    }
    RenderPipeline renderPipeline;
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



    HudSystem hudSystem;
    hudSystem.Initialize(faceCount, vertexCount, meshCount, isSmoothMesh, modelCenter, minV, maxV);
    TrackCollisionQueryFromSystem trackCollision(&trackSystem, &trackSegOffset);
    Game::SimpleCarPhysics carPhysics;
    Game::SimpleGameplayTick gameplayTick;
    Game::SimpleAudioEvents audioEvents;

    GameLoopSystem::Context loopContext{};
    loopContext.cartOkFlag = &cartOkFlag;
    loopContext.enableBg = enableBg;
    loopContext.renderTrack = renderTrack;
    loopContext.renderCar = renderCar;
    loopContext.renderAxes = renderAxes;
    loopContext.trackSystemReady = trackSystemReady;
    loopContext.verboseFrameLogs = kVerboseFrameLogs;
    loopContext.logTrack = logTrack;
    loopContext.logCar = (logCar && kCarLogs);
    // Estabilidade: manter apenas um pipeline na Slave por frame (pista).
    // Simulation/car prepare em Slave junto com producer da pista causa conflito de jobs.
    loopContext.enableSlaveForCarPrepare = false;
    loopContext.enableSlaveForSimulation = false;
    loopContext.faceCount = faceCount;
    loopContext.vertexCount = vertexCount;
    loopContext.trackSegOffset = trackSegOffset;
    loopContext.modelOffset = modelOffset;
    loopContext.carWorldPosition = carWorldPosition;
    loopContext.lightDirection = lightDirection;
    loopContext.bgManager = &bgManager;
    loopContext.cameraSystem = &cameraSystem;
    loopContext.trackSystem = &trackSystem;
    loopContext.carSystem = &carSystem;
    loopContext.renderPipeline = &renderPipeline;
    loopContext.hudSystem = &hudSystem;
    loopContext.trackCollision = &trackCollision;
    loopContext.carPhysics = &carPhysics;
    loopContext.gameplayTick = &gameplayTick;
    loopContext.audioEvents = &audioEvents;

    GameLoopSystem gameLoop(loopContext);
    AppState::Set(AppState::Stage::LoopStart, 0);
    return gameLoop.RunForever();

}

int main()
{
    GameApp app;
    return app.Run();
}























