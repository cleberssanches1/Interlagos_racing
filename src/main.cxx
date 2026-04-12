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
// Telemetria de runtime (RAM/VDP/slide): desligada por padrão.
constexpr bool kEnableRuntimeStatsLogs = false;
#define MLOG(...) do { if constexpr (kLog) { SRL::Debug::Print(__VA_ARGS__); } } while(0)

static const char* FindExistingPath(const char* const* paths, size_t count);
static constexpr size_t kCarGouraudOffset = 4096;
volatile uint32_t g_srlAppVblankCounter = 0;

struct CarAnchorPoints
{
    bool valid = false;
    Vector3D front{0.0, 0.0, 0.0};
    Vector3D rear{0.0, 0.0, 0.0};
};

extern "C" uint32_t SRL_AppGetVblankCounter()
{
    return g_srlAppVblankCounter;
}

// VBlank handler without Event dispatch to avoid invalid callback jumps in OnVblank.
static void SafeVblankNoEvent()
{
    slGetStatus();
    SRL::Input::Management::RefreshPeripherals();
    SRL::Input::Gun::VblankRefresh();
    g_srlAppVblankCounter = g_srlAppVblankCounter + 1u;
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

static bool ReadCdBinaryFileSimple(const char* path, std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!path || path[0] == '\0') return false;

    SRL::Cd::File f(path);
    if (!f.Exists() || f.Size.Bytes <= 0) return false;
    if (!f.Open()) return false;

    const size_t size = static_cast<size_t>(f.Size.Bytes);
    if (size == 0u) return false;

    outBytes.resize(size);
    size_t totalRead = 0u;
    while (totalRead < size)
    {
        const int32_t want = static_cast<int32_t>(std::min<size_t>(2048u, size - totalRead));
        const int32_t got = f.Read(want, outBytes.data() + totalRead);
        if (got <= 0) break;
        totalRead += static_cast<size_t>(got);
        if (got < want) break;
    }

    if (totalRead != size)
    {
        outBytes.clear();
        return false;
    }
    return true;
}

static const char* SkipWs(const char* p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}

static bool ParseJsonVec3ByKey(const char* json, const char* key, Vector3D& outVec)
{
    if (!json || !key) return false;
    const char* k = ::strstr(json, key);
    if (!k) return false;
    const char* lb = ::strchr(k, '[');
    const char* rb = lb ? ::strchr(lb, ']') : nullptr;
    if (!lb || !rb || rb <= lb) return false;

    const char* p = SkipWs(lb + 1);
    char* end = nullptr;
    const float x = std::strtof(p, &end);
    if (end == p) return false;
    p = SkipWs(end);
    if (*p == ',') ++p;

    p = SkipWs(p);
    const float y = std::strtof(p, &end);
    if (end == p) return false;
    p = SkipWs(end);
    if (*p == ',') ++p;

    p = SkipWs(p);
    const float z = std::strtof(p, &end);
    if (end == p) return false;

    auto toRaw = [](float v) -> int32_t
    {
        const float scaled = v * 65536.0f;
        return static_cast<int32_t>(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
    };
    outVec = Vector3D(Fxp::BuildRaw(toRaw(x)),
                      Fxp::BuildRaw(toRaw(y)),
                      Fxp::BuildRaw(toRaw(z)));
    return true;
}

static CarAnchorPoints LoadCarAnchorPointsFromCd()
{
    const char* candidates[] = {
        "CD/DATA/CAR1_ANCHORS.JSON;1",
        "CD/DATA/CAR1_ANCHORS.JSON",
        "DATA/CAR1_ANCHORS.JSON;1",
        "DATA/CAR1_ANCHORS.JSON",
        "CAR1_ANCHORS.JSON;1",
        "CAR1_ANCHORS.JSON",
        "cd/data/CAR1_ANCHORS.JSON",
        "cd/data/CAR1_ANCHORS.json",
        "data/CAR1_ANCHORS.JSON",
        "data/CAR1_ANCHORS.json",
        "CAR1_ANCHORS.json"
    };

    std::vector<uint8_t> bytes{};
    for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i)
    {
        if (!ReadCdBinaryFileSimple(candidates[i], bytes)) continue;
        std::vector<char> text(bytes.begin(), bytes.end());
        text.push_back('\0');

        CarAnchorPoints anchors{};
        if (!ParseJsonVec3ByKey(text.data(), "\"front\"", anchors.front)) continue;
        if (!ParseJsonVec3ByKey(text.data(), "\"rear\"", anchors.rear)) continue;
        anchors.valid = true;
        return anchors;
    }

    return {};
}

static int32_t NormalizeYawDeg360Main(int32_t yawDeg)
{
    yawDeg %= 360;
    if (yawDeg < 0) yawDeg += 360;
    return yawDeg;
}

static int32_t NormalizeSignedYawDegMain(int32_t yawDeg)
{
    yawDeg = NormalizeYawDeg360Main(yawDeg);
    if (yawDeg > 180) yawDeg -= 360;
    return yawDeg;
}

static bool ComputeGameplayYawFromModelForwardRaw(int32_t forwardXRaw,
                                                  int32_t forwardZRaw,
                                                  int32_t& outYawDeg)
{
    if (forwardXRaw == 0 && forwardZRaw == 0) return false;

    // MeshRenderer applies X180 + Z180 before yaw, equivalent to Y180 for direction vectors.
    const int32_t rdxRaw = -forwardXRaw;
    const int32_t rdzRaw = -forwardZRaw;
    const auto angle = SRL::Math::Trigonometry::Atan2(
        SRL::Math::Types::Fxp::BuildRaw(rdxRaw),
        SRL::Math::Types::Fxp::BuildRaw(-rdzRaw));
    const auto yawDegFxp = angle.ToDegrees();
    outYawDeg = NormalizeYawDeg360Main((yawDegFxp.RawValue() + (1 << 15)) >> 16);
    return true;
}

static bool ComputeCarVisualYawOffsetFromAnchors(const CarAnchorPoints& anchors, int32_t& outOffsetDeg)
{
    if (!anchors.valid) return false;
    const int32_t dxRaw = (anchors.front.X - anchors.rear.X).RawValue();
    const int32_t dzRaw = (anchors.front.Z - anchors.rear.Z).RawValue();
    int32_t modelForwardYawDeg = 0;
    if (!ComputeGameplayYawFromModelForwardRaw(dxRaw, dzRaw, modelForwardYawDeg)) return false;
    outOffsetDeg = NormalizeSignedYawDegMain(-modelForwardYawDeg);
    return true;
}

static bool ComputeCarMeshCenterByIndex(ModelObject* carPtr,
                                        uint32_t meshCount,
                                        bool isSmoothMesh,
                                        size_t meshIndex,
                                        Vector3D& outCenter,
                                        uint32_t* outVertexCount = nullptr,
                                        uint32_t* outFaceCount = nullptr)
{
    outCenter = Vector3D(0.0, 0.0, 0.0);
    if (!carPtr || meshIndex >= static_cast<size_t>(meshCount)) return false;

    if (isSmoothMesh)
    {
        auto* mesh = carPtr->GetMesh<SRL::Types::SmoothMesh>(meshIndex);
        if (!mesh || mesh->VertexCount == 0) return false;
        if (outVertexCount) *outVertexCount = static_cast<uint32_t>(mesh->VertexCount);
        if (outFaceCount) *outFaceCount = static_cast<uint32_t>(mesh->FaceCount);
        int64_t sumX = 0;
        int64_t sumY = 0;
        int64_t sumZ = 0;
        for (size_t v = 0; v < mesh->VertexCount; ++v)
        {
            sumX += static_cast<int64_t>(mesh->Vertices[v].X.RawValue());
            sumY += static_cast<int64_t>(mesh->Vertices[v].Y.RawValue());
            sumZ += static_cast<int64_t>(mesh->Vertices[v].Z.RawValue());
        }
        const int64_t count = static_cast<int64_t>(mesh->VertexCount);
        outCenter = Vector3D(
            Fxp::BuildRaw(static_cast<int32_t>(sumX / count)),
            Fxp::BuildRaw(static_cast<int32_t>(sumY / count)),
            Fxp::BuildRaw(static_cast<int32_t>(sumZ / count)));
        return true;
    }

    auto* mesh = carPtr->GetMesh<SRL::Types::Mesh>(meshIndex);
    if (!mesh || mesh->VertexCount == 0) return false;
    if (outVertexCount) *outVertexCount = static_cast<uint32_t>(mesh->VertexCount);
    if (outFaceCount) *outFaceCount = static_cast<uint32_t>(mesh->FaceCount);
    int64_t sumX = 0;
    int64_t sumY = 0;
    int64_t sumZ = 0;
    for (size_t v = 0; v < mesh->VertexCount; ++v)
    {
        sumX += static_cast<int64_t>(mesh->Vertices[v].X.RawValue());
        sumY += static_cast<int64_t>(mesh->Vertices[v].Y.RawValue());
        sumZ += static_cast<int64_t>(mesh->Vertices[v].Z.RawValue());
    }
    const int64_t count = static_cast<int64_t>(mesh->VertexCount);
    outCenter = Vector3D(
        Fxp::BuildRaw(static_cast<int32_t>(sumX / count)),
        Fxp::BuildRaw(static_cast<int32_t>(sumY / count)),
        Fxp::BuildRaw(static_cast<int32_t>(sumZ / count)));
    return true;
}

static bool ComputeCarVisualYawOffsetFromFrontMarkerMesh(ModelObject* carPtr,
                                                         uint32_t meshCount,
                                                         bool isSmoothMesh,
                                                         const Vector3D& modelCenter,
                                                         size_t markerMeshIndex,
                                                         int32_t& outOffsetDeg,
                                                         int32_t& outMarkerYawDeg,
                                                         uint32_t& outMarkerVertexCount,
                                                         uint32_t& outMarkerFaceCount)
{
    Vector3D markerCenter{};
    outMarkerVertexCount = 0;
    outMarkerFaceCount = 0;
    if (!ComputeCarMeshCenterByIndex(carPtr,
                                     meshCount,
                                     isSmoothMesh,
                                     markerMeshIndex,
                                     markerCenter,
                                     &outMarkerVertexCount,
                                     &outMarkerFaceCount))
    {
        return false;
    }

    const int32_t dxRaw = (markerCenter.X - modelCenter.X).RawValue();
    const int32_t dzRaw = (markerCenter.Z - modelCenter.Z).RawValue();
    const int32_t absDx = (dxRaw < 0) ? -dxRaw : dxRaw;
    const int32_t absDz = (dzRaw < 0) ? -dzRaw : dzRaw;
    if ((absDx + absDz) < (1 << 8))
    {
        return false;
    }

    if (!ComputeGameplayYawFromModelForwardRaw(dxRaw, dzRaw, outMarkerYawDeg))
    {
        return false;
    }
    outOffsetDeg = NormalizeSignedYawDegMain(-outMarkerYawDeg);
    return true;
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
    ModelObject* ActiveModel() const { return wramCopy ? wramCopy.get() : cart.car.get(); }

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

static void RefreshCarModelMetrics(ModelObject* carPtr,
                                   bool& outCarWasSmooth,
                                   bool& outIsSmoothMesh,
                                   uint32_t& outFaceCount,
                                   uint32_t& outVertexCount,
                                   uint32_t& outMeshCount)
{
    outCarWasSmooth = carPtr ? carPtr->IsSmooth() : false;
    outIsSmoothMesh = outCarWasSmooth;
    outFaceCount = carPtr ? carPtr->GetFaceCount() : 0;
    outVertexCount = carPtr ? carPtr->GetVertexCount() : 0;
    outMeshCount = carPtr ? carPtr->GetMeshCount() : 0;
}

static void ValidateCarTextureSlots(ModelObject* carPtr, uint32_t meshCount, bool isSmoothMesh);

static void SyncLoadedCarState(ModelObject* carPtr,
                               bool carValid,
                               bool logCar,
                               bool& outCarWasSmooth,
                               bool& outIsSmoothMesh,
                               uint32_t& outFaceCount,
                               uint32_t& outVertexCount,
                               uint32_t& outMeshCount)
{
    RefreshCarModelMetrics(carPtr, outCarWasSmooth, outIsSmoothMesh, outFaceCount, outVertexCount, outMeshCount);
    if (!carValid && logCar)
    {
        MLOG(0, 7, "Carro nao carregou (meshes/faces zero)");
    }
    ValidateCarTextureSlots(carPtr, outMeshCount, outIsSmoothMesh);
}

static Vector3D ComputeCarModelCenter(ModelObject* carPtr, uint32_t meshCount, bool isSmoothMesh)
{
    Vector3D center(0.0, 0.0, 0.0);
    if (!carPtr || meshCount == 0) return center;

    Vector3D minCar(32767, 32767, 32767);
    Vector3D maxCar(-32768, -32768, -32768);
    for (size_t m = 0; m < meshCount; ++m)
    {
        if (isSmoothMesh)
        {
            auto* mesh = carPtr->GetMesh<SRL::Types::SmoothMesh>(m);
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
            auto* mesh = carPtr->GetMesh<SRL::Types::Mesh>(m);
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

    center.X = (minCar.X + maxCar.X) / 2;
    center.Y = (minCar.Y + maxCar.Y) / 2;
    center.Z = (minCar.Z + maxCar.Z) / 2;
    return center;
}

static void ComputeCarModelBounds(ModelObject* carPtr,
                                  uint32_t meshCount,
                                  bool isSmoothMesh,
                                  Vector3D& outMin,
                                  Vector3D& outMax)
{
    outMin = Vector3D(32767, 32767, 32767);
    outMax = Vector3D(-32768, -32768, -32768);
    if (!carPtr || meshCount == 0) return;

    for (size_t m = 0; m < meshCount; ++m)
    {
        if (isSmoothMesh)
        {
            auto* mesh = carPtr->GetMesh<SRL::Types::SmoothMesh>(m);
            if (!mesh) continue;
            for (size_t v = 0; v < mesh->VertexCount; ++v)
            {
                const auto& p = mesh->Vertices[v];
                outMin.X = SRL::Math::Min(outMin.X, p.X);
                outMin.Y = SRL::Math::Min(outMin.Y, p.Y);
                outMin.Z = SRL::Math::Min(outMin.Z, p.Z);
                outMax.X = SRL::Math::Max(outMax.X, p.X);
                outMax.Y = SRL::Math::Max(outMax.Y, p.Y);
                outMax.Z = SRL::Math::Max(outMax.Z, p.Z);
            }
        }
        else
        {
            auto* mesh = carPtr->GetMesh<SRL::Types::Mesh>(m);
            if (!mesh) continue;
            for (size_t v = 0; v < mesh->VertexCount; ++v)
            {
                const auto& p = mesh->Vertices[v];
                outMin.X = SRL::Math::Min(outMin.X, p.X);
                outMin.Y = SRL::Math::Min(outMin.Y, p.Y);
                outMin.Z = SRL::Math::Min(outMin.Z, p.Z);
                outMax.X = SRL::Math::Max(outMax.X, p.X);
                outMax.Y = SRL::Math::Max(outMax.Y, p.Y);
                outMax.Z = SRL::Math::Max(outMax.Z, p.Z);
            }
        }
    }
}

static void ValidateCarTextureSlots(ModelObject* carPtr, uint32_t meshCount, bool isSmoothMesh)
{
    if (!carPtr) return;

    const int32_t firstTexture = carPtr->GetFirstTextureIndex();
    const size_t texCount = carPtr->GetTextureCount();
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
                    if (attr.Texture == No_Texture) continue;
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
            else
            {
                auto* mesh = carPtr->GetMesh<SRL::Types::Mesh>(mi);
                if (!mesh || mesh->Attributes == nullptr) continue;
                for (size_t fi = 0; fi < mesh->FaceCount; ++fi)
                {
                    const auto& attr = mesh->Attributes[fi];
                    if (attr.Texture == No_Texture) continue;
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

static void BuildCarDrawOrder(uint32_t meshCount, std::array<size_t, 5>& outOrder, size_t& outOrderCount)
{
    outOrder = {1, 2, 3, 4, 0};
    if (meshCount == 1)
    {
        outOrder = {0, 1, 2, 3, 4};
        outOrderCount = 1;
        return;
    }
    outOrderCount = (meshCount < 5) ? meshCount : 5;
}

static GameLoopSystem::Context BuildGameLoopContext(bool* cartOkFlag,
                                                    bool enableBg,
                                                    bool renderTrack,
                                                    bool renderCar,
                                                    bool renderAxes,
                                                    bool trackSystemReady,
                                                    bool logTrack,
                                                    bool logCar,
                                                    bool enableRuntimeStatsLogs,
                                                    bool enableManualGouraudCopy,
                                                    uint32_t faceCount,
                                                    uint32_t vertexCount,
                                                    const Vector3D& trackSegOffset,
                                                    const Vector3D& modelOffset,
                                                    const Vector3D& carWorldPosition,
                                                    const Vector3D& lightDirection,
                                                    BackgroundManager& bgManager,
                                                    CameraSystem& cameraSystem,
                                                    TrackSystem& trackSystem,
                                                    std::unique_ptr<Game::CarSystem>& carSystem,
                                                    RenderPipeline& renderPipeline,
                                                    HudSystem& hudSystem,
                                                    bool enableRuntimeSimulation,
                                                    Game::ITrackCollisionQuery* trackCollision,
                                                    Game::ICarPhysics* carPhysics,
                                                    Game::IGameplayTick* gameplayTick,
                                                    Game::IAudioEvents* audioEvents)
{
    GameLoopSystem::Context loopContext{};
    loopContext.cartOkFlag = cartOkFlag;
    loopContext.enableBg = enableBg;
    loopContext.renderTrack = renderTrack;
    loopContext.renderCar = renderCar;
    loopContext.renderAxes = renderAxes;
    loopContext.trackSystemReady = trackSystemReady;
    loopContext.verboseFrameLogs = kVerboseFrameLogs;
    loopContext.logTrack = logTrack;
    loopContext.logCar = (logCar && kCarLogs);
    loopContext.enableRuntimeStatsLogs = enableRuntimeStatsLogs;
    // Estabilidade: manter apenas um pipeline na Slave por frame (pista).
    // Simulation/car prepare em Slave junto com producer da pista causa conflito de jobs.
    loopContext.enableSlaveForCarPrepare = false;
    loopContext.enableSlaveForSimulation = false;
    loopContext.enableManualGouraudCopy = enableManualGouraudCopy;
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
    loopContext.trackCollision = enableRuntimeSimulation ? trackCollision : nullptr;
    loopContext.carPhysics = enableRuntimeSimulation ? carPhysics : nullptr;
    loopContext.gameplayTick = enableRuntimeSimulation ? gameplayTick : nullptr;
    loopContext.audioEvents = enableRuntimeSimulation ? audioEvents : nullptr;
    return loopContext;
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

    auto PrintBootRam = [](int row, const char* tag)
    {
        const auto hwr = SRL::Memory::HighWorkRam::GetReport();
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        const auto crt = SRL::Memory::CartRam::GetReport();
        SRL::Debug::Print(0, row, "%s hf:%lu lf:%lu cf:%lu",
                          tag,
                          static_cast<unsigned long>(hwr.FreeSize),
                          static_cast<unsigned long>(lwr.FreeSize),
                          static_cast<unsigned long>(crt.FreeSize));
    };
    if constexpr (kEnableRuntimeStatsLogs)
    {
        PrintBootRam(0, "RAM boot");
    }

    const bool logCar = kCarLogs;
    const bool logTrack = kEnableRuntimeStatsLogs;
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

    const bool renderTrack = true; // pista habilitada
    const bool renderCar = true; // carro habilitado
    const bool loadCarAfterTrack = true; // mantem fluxo padrao de carga da pista
    const bool enableTrackSlaveProducer = true; // teste: habilita Slave
    const bool forceSolidCarWhenTrack = false; // desativado: pode causar comando invalido na VDP1
    const bool renderAxes = false; // desliga eixos de debug

        // Carrega carro na DRAM do cart (somente se renderCar estiver ativo)
    const char* carPaths[] = {
        "CD/DATA/CAR1.NYA;1", "CD/DATA/CAR1.NYA",
        "DATA/CAR1.NYA;1", "DATA/CAR1.NYA",
        "CAR1.NYA;1", "CAR1.NYA",
        "car1.nya;1", "car1.nya"
    };
    const bool useCartCopyPipeline = false; // cart -> VDP1
    AppState::Set(AppState::Stage::CarLoad, 0);
    CarPipeline carPipe{};
    if (renderCar && !loadCarAfterTrack)
    {
        carPipe = LoadCarPipeline(carPaths, sizeof(carPaths)/sizeof(carPaths[0]), useCartCopyPipeline, kCarGouraudOffset);
    }

    ModelObject* carPtr = carPipe.ActiveModel();
    bool carValid = carPipe.Loaded();
// Se faltar cart, travamos o loop exibindo a mensagem
    bool cartOkFlag = cartOk;
    SRL::Math::Types::Vector3D trackSegOffset(0, 0, 0);

    bool carWasSmooth = false;
    bool isSmoothMesh = false;
    uint32_t faceCount = 0;
    uint32_t vertexCount = 0;
    uint32_t meshCount = 0;
    SyncLoadedCarState(carPtr, carValid, logCar, carWasSmooth, isSmoothMesh, faceCount, vertexCount, meshCount);
    // MLOG(1, 1, "CAR1.NYA load (smooth flag:%d)", carWasSmooth ? 1 : 0);

    if constexpr (kCarLogs)
    {
        MLOG(1, 10, "CarPipeline OK ptr:%08lx faces:%u verts:%u meshes:%u smooth:%d",
             (unsigned long)carPtr,
             faceCount,
             vertexCount,
             meshCount,
             isSmoothMesh ? 1 : 0);
    }

    // Simple frustum

    // Balanced FOV: reduce fisheye without flattening car proportions.
    constexpr float kCameraFovDeg = 34.0f;
    SRL::Scene3D::SetPerspective(Angle::FromDegrees(kCameraFovDeg));



    // Sky via VDP2 (componente reutilizavel)
    // Camada base (back screen) em azul celeste.
    SRL::VDP2::SetBackColor(HighColor::FromRGB555(0, 31, 31));
    // Mantem configuracao padrao de prioridades da engine para evitar ocultar NBG3/HUD.
    SRL::VDP2::NBG3::SetPriority(SRL::VDP2::Priority::Layer7);



    const bool enableBg = true; // desativa background para liberar HWR
    AppState::Set(AppState::Stage::BackgroundInit, 0);
    BackgroundManager bgManager;
    bool bgReady = false;
    const char* skyPaths[] = {
        // Ordem de preferência: assets já validados no projeto.
               
        "cd/data/SKY1.TGA",
        "cd/data/SKY1.tga",
        "cd/data/sky1.tga",       
        "data/SKY1.TGA",
        "SKY1.TGA",       
        "SKY1.TGA;1"
    };
    const size_t skyPathCount = sizeof(skyPaths) / sizeof(skyPaths[0]);
    if (enableBg)
    {
        SRL::Cd::ChangeDir((const char*)0);
        bgReady = bgManager.Init(skyPaths, skyPathCount);
    }
    if constexpr (kEnableRuntimeStatsLogs)
    {
        PrintBootRam(1, "RAM bg  ");
    }

    // Camera system owns camera state, tuning and input workflow.
    CameraSystem cameraSystem;
    cameraSystem.SetDebugLogsEnabled(false);

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
    Vector3D modelCenter = ComputeCarModelCenter(carPtr, meshCount, isSmoothMesh);
    Vector3D modelOffset(-modelCenter.X, -modelCenter.Y, -modelCenter.Z);
    Vector3D carWorldPosition(0.0, 0.0, 0.0);

    AppState::Set(AppState::Stage::TrackInit, 0);
    // Keep TrackSystem out of the main thread stack.
    // Its runtime state is large and stack growth can corrupt return addresses on SH2.
    static TrackSystem trackSystem;
    trackSystem.SetRuntimeStatsLogsEnabled(kEnableRuntimeStatsLogs);
    TrackSystem::Config trackConfig{};
    trackConfig.initialSegments = 20;
    trackConfig.minSegments = 20;
    // Keep per-frame SGL submissions under compile-time work area limits.
    trackConfig.initialMeshes = 512;
    trackConfig.initialFaces = static_cast<uint32_t>((SGL_MAX_POLYGONS > 64) ? (SGL_MAX_POLYGONS - 64) : SGL_MAX_POLYGONS);
    trackConfig.useSlave = enableTrackSlaveProducer;
    SRL::Cd::ChangeDir((const char*)0);
    const bool trackSystemReady = renderTrack ? trackSystem.Initialize(trackConfig) : false;
    if constexpr (kEnableRuntimeStatsLogs)
    {
        PrintBootRam(2, "RAM trk ");
    }
    if (enableBg)
    {
        if (!bgManager.loaded)
        {
            SRL::Cd::ChangeDir((const char*)0);
            const bool bgReadyAfterTrack = bgManager.Init(skyPaths, skyPathCount);
            bgReady = bgReady || bgReadyAfterTrack;
        }
    }
    if (renderTrack && trackSystemReady)
    {
        Vector3D seg01Center(0.0, 0.0, 0.0);
        if (trackSystem.FindSegmentCenterById(1, trackSegOffset, seg01Center))
        {
            // Spawn aligned to segment 1 center.
            carWorldPosition.X = seg01Center.X;
            carWorldPosition.Z = seg01Center.Z;
            carWorldPosition.Y = seg01Center.Y;
            // Car spawn debug log disabled to keep on-screen diagnostics concise.
        }
    }
    if (renderCar && loadCarAfterTrack)
    {
        constexpr size_t kMinHighWorkRamForCarLoad = 128u * 1024u;
        const size_t hwrFreeBeforeCarLoad = SRL::Memory::HighWorkRam::GetFreeSpace();
        if (hwrFreeBeforeCarLoad <= kMinHighWorkRamForCarLoad)
        {
            if (logCar)
            {
                MLOG(0, 7, "Car load low HWR:%lu (continuing)", (unsigned long)hwrFreeBeforeCarLoad);
            }
        }

        AppState::Set(AppState::Stage::CarLoad, 1);
        SRL::Cd::ChangeDir((const char*)0);
        carPipe = LoadCarPipeline(carPaths, sizeof(carPaths) / sizeof(carPaths[0]), useCartCopyPipeline, kCarGouraudOffset);
        carPtr = carPipe.ActiveModel();
        carValid = carPipe.Loaded();
        SyncLoadedCarState(carPtr, carValid, logCar, carWasSmooth, isSmoothMesh, faceCount, vertexCount, meshCount);
        if (renderTrack && trackSystemReady)
        {
            // Protect car texture slots from track-heap recycle/residency rebuild.
            trackSystem.RebaseTrackTextureHeapBase();
        }
        // Boot-time RAM snapshot for car load disabled to keep runtime HUD focused
        // on track streaming and slide diagnostics.

        // Recompute model center now that car was loaded after track textures.
        modelCenter = ComputeCarModelCenter(carPtr, meshCount, isSmoothMesh);
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
    // Guard smooth lighting allocation when High Work RAM is tight.
    const size_t hwrFreeBeforeLighting = SRL::Memory::HighWorkRam::GetFreeSpace();
    const size_t lightingBytesEstimate =
        (static_cast<size_t>(gouraudFaceCapacity) << 2) * sizeof(HighColor) +
        static_cast<size_t>(gouraudVertexCapacity) * sizeof(uint8_t) +
        (32u * 1024u);
    const bool enableSmoothLighting =
        (gouraudFaceCapacity > 0) &&
        (gouraudVertexCapacity > 0) &&
        (hwrFreeBeforeLighting > lightingBytesEstimate);
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

    // Car draw order: adapt to mesh count.
    // For single-mesh cars, always draw mesh 0.
    std::array<size_t, 5> drawOrder{};
    size_t orderCount = 0;
    BuildCarDrawOrder(meshCount, drawOrder, orderCount);

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
        int32_t visualYawOffsetDeg = 180;
        constexpr size_t kFrontMarkerMeshIndex = 5u; // 6o objeto: marcador da frente
        int32_t markerYawDeg = 0;
        uint32_t markerVerts = 0;
        uint32_t markerFaces = 0;
        const bool markerOffsetValid = ComputeCarVisualYawOffsetFromFrontMarkerMesh(
            carPtr,
            meshCount,
            isSmoothMesh,
            modelCenter,
            kFrontMarkerMeshIndex,
            visualYawOffsetDeg,
            markerYawDeg,
            markerVerts,
            markerFaces);

        const CarAnchorPoints anchors = LoadCarAnchorPointsFromCd();
        int32_t anchorYawDeg = 0;
        bool anchorOffsetValid = false;
        if (!markerOffsetValid)
        {
            anchorOffsetValid = ComputeCarVisualYawOffsetFromAnchors(anchors, visualYawOffsetDeg);
            if (anchors.valid)
            {
                const int32_t dxRaw = (anchors.front.X - anchors.rear.X).RawValue();
                const int32_t dzRaw = (anchors.front.Z - anchors.rear.Z).RawValue();
                (void)ComputeGameplayYawFromModelForwardRaw(dxRaw, dzRaw, anchorYawDeg);
            }
        }
        if constexpr (kLog)
        {
            const char* src = markerOffsetValid ? "M6" : (anchorOffsetValid ? "JS" : "DF");
            const int32_t srcYaw = markerOffsetValid ? markerYawDeg : anchorYawDeg;
            SRL::Debug::Print(1, 12, "CAR fwd:%s off:%d y:%d m6v:%u m6f:%u",
                              src,
                              static_cast<int>(visualYawOffsetDeg),
                              static_cast<int>(srcYaw),
                              static_cast<unsigned>(markerVerts),
                              static_cast<unsigned>(markerFaces));
        }
        // Rotate only the rendered car model so spawn orientation is correct
        // without changing gameplay/camera yaw reference.
        carSystem->SetVisualYawOffsetDegrees(visualYawOffsetDeg);
        cameraSystem.SetCarForwardYawOffsetDegrees(visualYawOffsetDeg);
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
    SRL::Math::Types::Vector3D minV{};
    SRL::Math::Types::Vector3D maxV{};
    ComputeCarModelBounds(carPtr, meshCount, isSmoothMesh, minV, maxV);
    {
        // Camera 2 calibration baseline requested:
        // CAM2 off x:0 y:-20 z:-144
        constexpr int16_t kCam2BehindUnits = 190;
        cameraSystem.SetChaseNearFollowDistance(kCam2BehindUnits);
    }



    HudSystem hudSystem;
    hudSystem.Initialize(faceCount, vertexCount, meshCount, isSmoothMesh, modelCenter, minV, maxV);
    TrackCollisionQueryFromSystem trackCollision(&trackSystem, &trackSegOffset);
    Game::SimpleCarPhysics carPhysics;
    Game::SimpleGameplayTick gameplayTick;
    Game::SimpleAudioEvents audioEvents;
    // Safety mode: keep runtime simulation disabled while stabilizing render path.
    const bool enableRuntimeSimulation = false;

    GameLoopSystem::Context loopContext = BuildGameLoopContext(&cartOkFlag,
                                                               enableBg,
                                                               renderTrack,
                                                               renderCar,
                                                               renderAxes,
                                                               trackSystemReady,
                                                               logTrack,
                                                               logCar,
                                                               kEnableRuntimeStatsLogs,
                                                               enableSmoothLighting,
                                                               faceCount,
                                                               vertexCount,
                                                               trackSegOffset,
                                                               modelOffset,
                                                               carWorldPosition,
                                                               lightDirection,
                                                               bgManager,
                                                               cameraSystem,
                                                               trackSystem,
                                                               carSystem,
                                                               renderPipeline,
                                                               hudSystem,
                                                               enableRuntimeSimulation,
                                                               static_cast<Game::ITrackCollisionQuery*>(&trackCollision),
                                                               static_cast<Game::ICarPhysics*>(&carPhysics),
                                                               static_cast<Game::IGameplayTick*>(&gameplayTick),
                                                               static_cast<Game::IAudioEvents*>(&audioEvents));

    GameLoopSystem gameLoop(loopContext);
    AppState::Set(AppState::Stage::LoopStart, 0);
    return gameLoop.RunForever();

}

int main()
{
    GameApp app;
    return app.Run();
}























