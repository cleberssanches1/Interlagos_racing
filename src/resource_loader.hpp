#pragma once

#include <srl.hpp>
#include "modelObject.hpp"
#include "track_renderer.hpp"

struct CarLoadResult
{
    // Pointer to car model allocated in cart RAM (ownership transferred to caller).
    ModelObject* car = nullptr; // allocated when loaded
    bool loaded = false;
    int32_t hwrDelta = 0;
    uint32_t estBytes = 0;
    uintptr_t meshPtr = 0;
    uint32_t faceCount = 0;
    uint32_t vertexCount = 0;
    bool isSmooth = false;
    int32_t freeAfter = 0;
};

struct TrackLoadResult
{
    // Track renderer prepared from cart RAM assets.
    TrackRenderer renderer;
    bool loaded = false;
    int32_t hwrDelta = 0;
    uint32_t estBytes = 0;
    uint32_t faceCount = 0;
    uint32_t vertexCount = 0;
    size_t meshCount = 0;
    bool isSmooth = false;
    int32_t freeAfter = 0;
};

// Load a car NYA into cart RAM (optionally forcing cart) and return stats/pointers.
CarLoadResult LoadCarToCart(const char* const* paths, size_t pathCount, bool forceCart = false);
// Load a track NYA into cart RAM and build a TrackRenderer with the requested mesh cap.
TrackLoadResult LoadTrackToCart(const char* const* paths, size_t pathCount, size_t maxMeshes);

// Inline implementations to keep single translation unit usage (avoid duplicate std throw stubs)
inline CarLoadResult LoadCarToCart(const char* const* paths, size_t pathCount, bool forceCart)
{
    CarLoadResult res{};
    int32_t hwrBefore = SRL::Memory::CartRam::GetFreeSpace();

    for (size_t i = 0; i < pathCount; ++i)
    {
        SRL::Cd::File f(paths[i]);
        SRL::Debug::Print(0, 2, "CAR path:%s ex:%d sz:%ld", paths[i], f.Exists() ? 1 : 0, (long)f.Size.Bytes);

        ModelObject candidate(paths[i], 0, false, 0, false, forceCart, false);
        if (candidate.GetMeshCount() == 0 || candidate.GetFaceCount() == 0)
        {
            SRL::Debug::Print(0, 3, "Car stream fail %s -> buffer", paths[i]);
            ModelObject bufLoad(paths[i], 0, false, 0, false, forceCart, false);
            candidate = std::move(bufLoad);
        }
        if (candidate.GetMeshCount() > 0 && candidate.GetFaceCount() > 0)
        {
            res.car = new ModelObject(std::move(candidate));
            res.loaded = true;
            break;
        }
    }

    int32_t hwrAfter = SRL::Memory::CartRam::GetFreeSpace();
    res.hwrDelta = hwrBefore - hwrAfter;
    if (res.car)
    {
        res.meshPtr = reinterpret_cast<uintptr_t>(res.car->RawMeshesPtr());
        res.faceCount = res.car->GetFaceCount();
        res.vertexCount = res.car->GetVertexCount();
        res.isSmooth = res.car->IsSmooth();
    }
    res.freeAfter = hwrAfter;
    res.estBytes = (uint32_t)(res.vertexCount * sizeof(SRL::Math::Types::Vector3D));
    res.estBytes += (uint32_t)(res.faceCount * (sizeof(SRL::Types::Polygon) + sizeof(SRL::Types::Attribute)));
    if (res.isSmooth) res.estBytes += (uint32_t)(res.vertexCount * sizeof(SRL::Math::Types::Vector3D));

    if (res.loaded)
    {
        SRL::Debug::Print(0, 4, "Car ok m:%u f:%u v:%u sm:%d", res.car ? (unsigned)res.car->GetMeshCount() : 0, (unsigned)res.faceCount, (unsigned)res.vertexCount, res.isSmooth ? 1 : 0);
        SRL::Debug::Print(0, 5, "Car HWR delta:%d ptr:%08lx", res.hwrDelta, (unsigned long)res.meshPtr);
        SRL::Debug::Print(0, 6, "Car est bytes:%u", (unsigned)res.estBytes);
        SRL::Debug::Print(0, 7, "Car cart ok free:%d", (int)res.freeAfter);
    }
    else
    {
        SRL::Debug::Print(0, 7, "Carro nao carregou (meshes/faces zero)");
    }

    return res;
}

inline TrackLoadResult LoadTrackToCart(const char* const* paths, size_t pathCount, size_t maxMeshes)
{
    TrackLoadResult res{};
    int32_t hwrBefore = SRL::Memory::CartRam::GetFreeSpace();
    for (size_t i = 0; i < pathCount; ++i)
    {
        SRL::Cd::File f(paths[i]);
        if (!f.Exists() || f.Size.Bytes <= 0) continue;
        if (res.renderer.Load(paths, pathCount, maxMeshes, false))
        {
            res.loaded = true;
            break;
        }
    }
    int32_t hwrAfter = SRL::Memory::CartRam::GetFreeSpace();
    res.hwrDelta = hwrBefore - hwrAfter;
    res.faceCount = res.renderer.FaceCount();
    res.vertexCount = res.renderer.VertexCount();
    res.meshCount = res.renderer.MeshCount();
    res.isSmooth = res.renderer.IsSmooth();
    res.estBytes = res.renderer.MemStats().bytes;
    res.freeAfter = hwrAfter;

    if (res.loaded)
    {
        auto center = (res.renderer.Bounds().min + res.renderer.Bounds().max) / SRL::Math::Types::Fxp::Convert(2);
        SRL::Debug::Print(1, 4, "Track center: %d %d %d", center.X.As<int16_t>(), center.Y.As<int16_t>(), center.Z.As<int16_t>());
        SRL::Debug::Print(1, 5, "Track counts m:%u f:%u v:%u smooth:%d", (unsigned)res.meshCount, (unsigned)res.faceCount, (unsigned)res.vertexCount, res.isSmooth ? 1 : 0);
        SRL::Debug::Print(1, 6, "Track HWR delta:%d bytes:%u", res.hwrDelta, (unsigned)res.estBytes);
        SRL::Debug::Print(1, 7, "Track cart ok free:%d", (int)res.freeAfter);
    }
    else
    {
        SRL::Debug::Print(1, 4, "Track nao carregada (load falhou)");
    }

    return res;
}
