#pragma once

#include <algorithm>
#include <vector>
#include <srl.hpp>
#include "modelObject.hpp"
#include "track_renderer.hpp"
#include "track_serialized.hpp"

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
// Serialize track data into Cart RAM for fast segment buffering.
TrackSerializedCopy SerializeTrackToCart(const char* path, size_t chunkSize = 0x4000);

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
    TrackRenderer renderer;
    for (size_t i = 0; i < pathCount; ++i)
    {
        SRL::Cd::File f(paths[i]);
        if (!f.Exists() || f.Size.Bytes <= 0) continue;
        if (renderer.Load(paths, pathCount, maxMeshes, false))
        {
            res.loaded = true;
            break;
        }
    }
    int32_t hwrAfter = SRL::Memory::CartRam::GetFreeSpace();
    res.hwrDelta = hwrBefore - hwrAfter;
    res.faceCount = renderer.FaceCount();
    res.vertexCount = renderer.VertexCount();
    res.meshCount = renderer.MeshCount();
    res.isSmooth = renderer.IsSmooth();
    res.estBytes = renderer.MemStats().bytes;
    res.freeAfter = hwrAfter;

    if (res.loaded)
    {
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

inline TrackSerializedCopy SerializeTrackToCart(const char* path, size_t chunkSize)
{
    TrackSerializedCopy result{};
    SRL::Cd::File file(path);
    if (!file.Exists())
    {
        SRL::Debug::Print(1, 3, "Track serialize fail (missing): %s", path);
        return result;
    }

    if (!file.Open())
    {
        SRL::Debug::Print(1, 3, "Track serialize fail (open): %s", path);
        return result;
    }

    size_t desiredSize = static_cast<size_t>(std::max<int32_t>(0, file.Size.Bytes));
    size_t freeBefore = SRL::Memory::CartRam::GetFreeSpace();
    size_t maxCartSize = SRL::Memory::CartRam::GetSize();
    size_t allocSize = std::min({desiredSize > 0 ? desiredSize : maxCartSize, freeBefore, maxCartSize});
    if (allocSize == 0)
    {
        SRL::Debug::Print(1, 3, "Track serialize fail (no cart space) %s desired:%u free:%u", path, (unsigned)desiredSize, (unsigned)freeBefore);
        return result;
    }

    void* cartBuffer = SRL::Memory::CartRam::Malloc(allocSize);
    if (!cartBuffer)
    {
        SRL::Debug::Print(1, 3, "Track serialize fail (malloc): %s sz:%u", path, (unsigned)allocSize);
        return result;
    }

    std::vector<uint8_t> tempBuffer(std::min(chunkSize, allocSize));
    size_t written = 0;
    bool overflow = false;
    while (written < allocSize)
    {
        int32_t toRead = static_cast<int32_t>(std::min(tempBuffer.size(), allocSize - written));
        if (toRead <= 0) break;

        int32_t read = file.Read(toRead, tempBuffer.data());
        if (read < 0)
        {
            SRL::Memory::CartRam::Free(cartBuffer);
            SRL::Debug::Print(1, 3, "Track serialize fail (read error): %s", path);
            return result;
        }
        if (read == 0)
        {
            break;
        }

        slDMACopy(tempBuffer.data(), reinterpret_cast<uint8_t*>(cartBuffer) + written, static_cast<size_t>(read));
        written += static_cast<size_t>(read);
    }

    if (written == allocSize)
    {
        int32_t extra = file.Read(1, tempBuffer.data());
        if (extra > 0)
        {
            overflow = true;
        }
    }

    if (overflow)
    {
        SRL::Memory::CartRam::Free(cartBuffer);
        SRL::Debug::Print(1, 3, "Track serialize fail (overflow): %s desired:%u alloc:%u", path, (unsigned)desiredSize, (unsigned)allocSize);
        return result;
    }

    int32_t hwrAfter = SRL::Memory::CartRam::GetFreeSpace();
    result.cartPtr = cartBuffer;
    result.size = written;
    result.hwrDelta = static_cast<int32_t>(freeBefore - hwrAfter);
    SRL::Debug::Print(1, 4, "Track serialized OK: %s cart:%08lx copied:%u delta:%d", path, (unsigned long)cartBuffer, (unsigned)written, result.hwrDelta);
    return result;
}

struct TrackSegmentCopy
{
    void* cartPtr = nullptr;
    size_t size = 0;
};

inline TrackSegmentCopy CopyTrackSegmentToCart(const char* path, size_t chunkSize = 0x4000)
{
    TrackSegmentCopy result{};
    SRL::Cd::File file(path);
    if (!file.Exists())
    {
        SRL::Debug::Print(1, 3, "Segment copy fail (missing): %s", path);
        return result;
    }

    if (!file.Open())
    {
        SRL::Debug::Print(1, 3, "Segment copy fail (open): %s", path);
        return result;
    }

    size_t desiredSize = static_cast<size_t>(std::max<int32_t>(0, file.Size.Bytes));
    size_t freeBefore = SRL::Memory::CartRam::GetFreeSpace();
    size_t maxCartSize = SRL::Memory::CartRam::GetSize();
    size_t allocSize = std::min({ desiredSize > 0 ? desiredSize : maxCartSize, freeBefore, maxCartSize });
    if (allocSize == 0)
    {
        SRL::Debug::Print(1, 3, "Segment copy fail (no cart space) %s desired:%u free:%u", path, (unsigned)desiredSize, (unsigned)freeBefore);
        return result;
    }

    void* cartBuffer = SRL::Memory::CartRam::Malloc(allocSize);
    if (!cartBuffer)
    {
        SRL::Debug::Print(1, 3, "Segment copy fail (malloc): %s sz:%u", path, (unsigned)allocSize);
        return result;
    }

    std::vector<uint8_t> tempBuffer(std::min(chunkSize, allocSize));
    size_t written = 0;
    bool overflow = false;
    while (written < allocSize)
    {
        int32_t toRead = static_cast<int32_t>(std::min(tempBuffer.size(), allocSize - written));
        if (toRead <= 0) break;

        int32_t read = file.Read(toRead, tempBuffer.data());
        if (read < 0)
        {
            SRL::Memory::CartRam::Free(cartBuffer);
            SRL::Debug::Print(1, 3, "Segment copy fail (read error): %s", path);
            return result;
        }
        if (read == 0)
        {
            break;
        }

        slDMACopy(tempBuffer.data(), reinterpret_cast<uint8_t*>(cartBuffer) + written, static_cast<size_t>(read));
        written += static_cast<size_t>(read);
    }

    if (written == allocSize)
    {
        int32_t extra = file.Read(1, tempBuffer.data());
        if (extra > 0)
        {
            overflow = true;
        }
    }

    if (overflow)
    {
        SRL::Memory::CartRam::Free(cartBuffer);
        SRL::Debug::Print(1, 3, "Segment copy fail (overflow): %s desired:%u alloc:%u", path, (unsigned)desiredSize, (unsigned)allocSize);
        return result;
    }

    result.cartPtr = cartBuffer;
    result.size = written;
    SRL::Debug::Print(1, 4, "Segment copied: %s cart:%08lx size:%u", path, (unsigned long)cartBuffer, (unsigned)written);
    return result;
}
