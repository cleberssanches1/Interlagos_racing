#include <algorithm>
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

#include <array>
#include <memory>
#include <cstddef>
#include <vector>
#include <cstdio>
#include <string>
#include "srl_string.hpp"

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

constexpr size_t kTrackSegmentLimit = 10;

static const char* FindExistingPath(const char* const* paths, size_t count);
static char lastSegmentPath[128] = {};

static const char* kSegmentPathTemplates[] = {
    "CD/DATA/SEG_%03u.NYA",
    "CD/DATA/SEG_%03u.NYA;1",
    "cd/data/SEG_%03u.NYA",
    "cd/data/SEG_%03u.NYA;1",
    "SEG_%03u.NYA",
    "SEG_%03u.NYA;1",
    "SEG/SEG_%03u.NYA",
    "SEG/SEG_%03u.NYA;1",
    "BuildDrop/Interlagos_racing/SEG_%03u.NYA",
    "BuildDrop/Interlagos_racing/SEG_%03u.NYA;1",
    "CD/SEG_%03u.NYA",
    "cd/seg_%03u.nya"
};

static const char* ResolveSegmentPath(size_t id)
{
    constexpr size_t variantCount = sizeof(kSegmentPathTemplates) / sizeof(kSegmentPathTemplates[0]);
    std::array<std::array<char, 64>, variantCount> buffers{};
    const char* candidates[variantCount];

    for (size_t i = 0; i < variantCount; ++i)
    {
        std::snprintf(buffers[i].data(), buffers[i].size(), kSegmentPathTemplates[i], unsigned(id));
        candidates[i] = buffers[i].data();
    }
    return FindExistingPath(candidates, variantCount);
}

struct TrackSegmentEntry
{
    int id = 0;
    TrackSegmentCopy copy;
};

static std::vector<TrackSegmentEntry> CopyAllTrackSegments()
{
    std::vector<TrackSegmentEntry> segments;
    segments.reserve(kTrackSegmentLimit);
    for (size_t i = 1; i <= kTrackSegmentLimit; ++i)
    {
        const char* existingPath = ResolveSegmentPath(i);
        if (!existingPath)
        {
            MLOG(1, 12, "Segment %03u path missing (%u variants)", unsigned(i), unsigned(sizeof(kSegmentPathTemplates) / sizeof(kSegmentPathTemplates[0])));
            break;
        }
        TrackSegmentCopy copy = CopyTrackSegmentToCart(existingPath);
        segments.push_back({ static_cast<int>(i), copy });
        if (copy.cartPtr)
        {
            MLOG(1, 11, "Segment %03u copied (%u bytes)", unsigned(i), unsigned(copy.size));
        }
        else
        {
            MLOG(1, 12, "Segment %03u failed to copy (missing?)", unsigned(i));
            break;
        }
    }
    size_t valid = 0;
    for (const auto& segment : segments)
    {
        if (segment.copy.cartPtr && segment.copy.size > 0) ++valid;
    }
    MLOG(1, 13, "Track segments copied %u/%u", unsigned(valid), unsigned(segments.size()));
    return segments;
}

// Procura o primeiro caminho existente em disco.
static const char* FindExistingPath(const char* const* paths, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        SRL::Cd::File f(paths[i]);
        bool exists = f.Exists() && f.Size.Bytes > 0;
        ::strncpy(lastSegmentPath, paths[i], sizeof(lastSegmentPath));
        lastSegmentPath[sizeof(lastSegmentPath) - 1] = '\0';
        MLOG(1, 6, "Check cd path: %s -> %d", paths[i], exists ? 1 : 0);
        if (exists) return paths[i];
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

constexpr size_t kNearestTrackSegmentCount = 10;

struct SegmentRenderEntry
{
    int id = 0;
    std::unique_ptr<TrackRenderer> renderer;
    SRL::Math::Types::Vector3D center{};
};

static SRL::Math::Types::Vector3D ComputeRendererCenter(const TrackRenderer& renderer)
{
    return renderer.StartMeshCenter() + renderer.Offset();
}

static std::vector<SegmentRenderEntry> BuildSegmentRenderers(std::vector<TrackSegmentEntry>& entries)
{
    std::vector<SegmentRenderEntry> renderers;
    renderers.reserve(entries.size());
    for (auto& entry : entries)
    {
        if (!entry.copy.cartPtr || entry.copy.size == 0) continue;
        auto model = std::make_unique<ModelObject>();
        if (!model->LoadFromMemory(entry.copy.cartPtr, entry.copy.size, 0, false, 0, false, true))
        {
            MLOG(1, 14, "Segment load fail %03d", entry.id);
            SRL::Memory::CartRam::Free(entry.copy.cartPtr);
            entry.copy.cartPtr = nullptr;
            continue;
        }
        auto renderer = std::make_unique<TrackRenderer>();
        ModelObject* rawModel = model.release();
        if (!renderer->InitializeFromModelObject(rawModel, 0))
        {
            MLOG(1, 15, "Renderer init fail %03d", entry.id);
            delete rawModel;
            SRL::Memory::CartRam::Free(entry.copy.cartPtr);
            entry.copy.cartPtr = nullptr;
            continue;
        }
        renderer->SetDrawLimit(renderer->MeshCount());
        SegmentRenderEntry item{};
        item.id = entry.id;
        item.center = ComputeRendererCenter(*renderer);
        item.renderer = std::move(renderer);
        renderers.push_back(std::move(item));
        SRL::Memory::CartRam::Free(entry.copy.cartPtr);
        entry.copy.cartPtr = nullptr;
    }
    return renderers;
}

static std::vector<SegmentRenderEntry*> SelectNearestSegmentRenderers(std::vector<SegmentRenderEntry>& entries, const SRL::Math::Types::Vector3D& reference, size_t limit)
{
    struct DistanceEntry { float distSq; SegmentRenderEntry* entry; };
    std::vector<DistanceEntry> distances;
    distances.reserve(entries.size());
    for (auto& entry : entries)
    {
        auto delta = entry.center - reference;
        float dx = static_cast<float>(delta.X.As<int32_t>()) / 65536.0f;
        float dy = static_cast<float>(delta.Y.As<int32_t>()) / 65536.0f;
        float dz = static_cast<float>(delta.Z.As<int32_t>()) / 65536.0f;
        distances.push_back({ dx*dx + dy*dy + dz*dz, &entry });
    }
    std::sort(distances.begin(), distances.end(), [](const DistanceEntry& a, const DistanceEntry& b)
    {
        return a.distSq < b.distSq;
    });
    std::vector<SegmentRenderEntry*> nearest;
    nearest.reserve(std::min(limit, distances.size()));
    for (size_t i = 0; i < std::min(limit, distances.size()); ++i)
    {
        nearest.push_back(distances[i].entry);
    }
    return nearest;
}

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

    const bool loadTrackSegments = false; // desabilita carregamento da pista
    auto trackSegmentEntries = loadTrackSegments ? CopyAllTrackSegments() : std::vector<TrackSegmentEntry>{};
    MLOG(1, 26, "Track segment registry entries:%zu", trackSegmentEntries.size());

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
    const bool renderTrack = false; // desativa renderização da pista para teste
    const bool renderCar = true; // carro ativado
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
        .viewPitchDeg = 13,
        .radius = Fxp(67.74f),
        .strafe = Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0)),
        .location = Vector3D(0.0, 0.0, -50.0f),
        .yaw = Angle::FromDegrees(Fxp::Convert(180)),
        .pitch = Angle::FromDegrees(Fxp::Convert(-10)),
        .viewYaw = Angle::FromDegrees(Fxp::Convert(0)),
        .viewPitch = Angle::FromDegrees(Fxp::Convert(13)),
    };

    Camera::Tuning cameraTuning{};
    cameraTuning.targetDistance = Fxp::Convert(1174.0f);
    cameraTuning.yawStepDeg = 4;

    const int16_t orbitYawStepDeg = 4;
    const int16_t orbitPitchStepDeg = 2;
    const int16_t orbitPitchLimitDeg = 40;

    Camera::RefreshAngles(cameraState);

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
    Vector3D manualXOffset(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0));
    const Vector3D desiredCamera(0.0, Fxp::Convert(-32.0f), Fxp::Convert(56.0f));
    Vector3D initialOrbit = Camera::OrbitPosition(cameraState.yaw, cameraState.pitch, cameraState.radius);
    manualXOffset = desiredCamera - initialOrbit;

    auto trackSegmentRenderers = BuildSegmentRenderers(trackSegmentEntries);
    const bool trackSegmentsReady = !trackSegmentRenderers.empty();
    if (!trackSegmentsReady && renderTrack)
    {
        MLOG(1, 28, "Track rendering skipped: segments missing");
        if (lastSegmentPath[0] != '\0')
        {
            MLOG(1, 29, "Last segment path tested: %s", lastSegmentPath);
        }
    }
    if (!trackSegmentRenderers.empty())
    {
        std::string ids;
        size_t count = std::min(trackSegmentRenderers.size(), kNearestTrackSegmentCount);
        for (size_t i = 0; i < count; ++i)
        {
            ids += std::to_string(trackSegmentRenderers[i].id);
            if (i + 1 < count) ids += ",";
        }
        MLOG(1, 27, "Nearest segment candidates (%zu): %s", count, ids.c_str());
    }

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

    static uint32_t frameCounter = 0;

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
        const bool cHeld = pad.IsHeld(SRL::Input::Digital::Button::C);
        const bool xHeld = pad.IsHeld(SRL::Input::Digital::Button::X);
        const bool lHeld = pad.IsHeld(SRL::Input::Digital::Button::L);
        const bool rHeld = pad.IsHeld(SRL::Input::Digital::Button::R);
        const bool upHeld = pad.IsHeld(SRL::Input::Digital::Button::Up);
        const bool downHeld = pad.IsHeld(SRL::Input::Digital::Button::Down);
        const bool zHeld = pad.IsHeld(SRL::Input::Digital::Button::Z);
        const bool leftArrowHeld = pad.IsHeld(SRL::Input::Digital::Button::Left);
        const bool rightArrowHeld = pad.IsHeld(SRL::Input::Digital::Button::Right);
        const bool orbitControlActive = zHeld && (lHeld || rHeld || upHeld || downHeld);


        const int16_t carYawStepDeg = cameraTuning.yawStepDeg;

        // Rotaciona apenas o carro com L/R (plano horizontal)
        if (!aHeld && !bHeld && !cHeld && !orbitControlActive && !xHeld)
        {
            if (lHeld) carYawDeg -= carYawStepDeg;
            if (rHeld) carYawDeg += carYawStepDeg;
            if (carYawDeg < 0) carYawDeg += 360;
            if (carYawDeg >= 360) carYawDeg -= 360;
        }

        // Rotaciona carro e camera (modo X) usando CameraRig utilitario
        if (!xHeld)
        {
            CameraRig::HandleOrbitAroundCar(cameraState, carYawStepDeg, xHeld, lHeld, rHeld, carYawDeg, xOrbitState, true);
        }

        // Controles de rodas: C inicia/resume, B para
        if (carRendererPtr)
        {
            if (cHeld) { carRendererPtr->StartAllWheels(Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(15))); carRendererPtr->ResumeAllWheels(); }
            if (bHeld) { carRendererPtr->StopAllWheels(); }
        }

        // Atualiza skybox VDP2
        if (enableBg) bgManager.Update(cameraState); // mant'm VDP2 background ativo

        if (zHeld)
        {
            int16_t newPitch = cameraState.viewPitchDeg;
            if (upHeld) newPitch -= orbitPitchStepDeg;
            if (downHeld) newPitch += orbitPitchStepDeg;
            newPitch = std::clamp<int16_t>(newPitch,
                                           static_cast<int16_t>(-orbitPitchLimitDeg),
                                           static_cast<int16_t>(orbitPitchLimitDeg));
            cameraState.viewPitchDeg = newPitch;
            if (lHeld) cameraState.viewYawDeg -= orbitYawStepDeg;
            if (rHeld) cameraState.viewYawDeg += orbitYawStepDeg;
            cameraState.viewYawDeg = static_cast<int16_t>((cameraState.viewYawDeg + 360) % 360);
            cameraState.viewPitch = Angle::FromDegrees(Fxp::Convert(cameraState.viewPitchDeg));
            cameraState.viewYaw = Angle::FromDegrees(Fxp::Convert(cameraState.viewYawDeg));
        }

        if (xHeld)
        {
            const Fxp cameraMoveStep = Fxp::Convert(4.0f);
            if (upHeld) manualXOffset.Y -= cameraMoveStep;
            if (downHeld) manualXOffset.Y += cameraMoveStep;
            if (leftArrowHeld) manualXOffset.X -= cameraMoveStep;
            if (rightArrowHeld) manualXOffset.X += cameraMoveStep;
        }

        Vector3D orbitOffset = cameraState.location;
        Vector3D cameraLocation = orbitOffset + carWorldPosition;
        cameraLocation += manualXOffset;
        Vector3D viewDirection = Camera::OrbitPosition(cameraState.viewYaw, cameraState.viewPitch, cameraTuning.targetDistance);
        const Vector3D hoodTargetOffset(0.0, Fxp::Convert(-5.0f), 0.0);
        Vector3D lookTarget = carWorldPosition + modelOffset + hoodTargetOffset;
        if (zHeld && ((frameCounter & 31) == 0))
        {
            const int16_t orbitOffsetX = viewDirection.X.As<int16_t>();
            const int16_t orbitOffsetY = viewDirection.Y.As<int16_t>();
            const int16_t orbitOffsetZ = viewDirection.Z.As<int16_t>();
            MLOG(1, 14, "Orbit offset: %d %d %d", orbitOffsetX, orbitOffsetY, orbitOffsetZ);
            MLOG(1, 15, "View angles yaw:%d pitch:%d", cameraState.viewYawDeg, cameraState.viewPitchDeg);
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
        if (renderTrack && trackSegmentsReady)
        {
            auto nearestTrackRenderers = SelectNearestSegmentRenderers(trackSegmentRenderers, carWorldPosition, kNearestTrackSegmentCount);
            for (auto entry : nearestTrackRenderers)
            {
                if (entry && entry->renderer)
                {
                    entry->renderer->SetOffset(trackSegOffset);
                    entry->renderer->Render(lightDirection, cameraLocation);
                    auto segmentCenter = entry->renderer->StartMeshCenter() + entry->renderer->Offset();
                    MLOG(1, 20, "Segment %02d center %d %d %d", entry->id,
                         segmentCenter.X.As<int16_t>(), segmentCenter.Y.As<int16_t>(), segmentCenter.Z.As<int16_t>());
                }
            }
        }
        // Reaplica lookTarget para garantir que a câmera esteja alinhada com o carro
        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(cameraLocation, lookTarget, Angle::FromDegrees(0.0));
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
            if (logTrack)
            {
                MLOG(0, 27, "VDP1 textures:%d", vdp1TexCount);
                MLOG(0, 28, "VDP1 mem used:%u free:%u pct:%u%%", (unsigned)vdp1Used, (unsigned)vdp1Free, vdp1Pct);
            }
            SRL::Debug::Print(2, 26, "VDP1 mem used:%u free:%u pct:%u%%",
                               (unsigned)vdp1Used, (unsigned)vdp1Free, vdp1Pct);
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






















