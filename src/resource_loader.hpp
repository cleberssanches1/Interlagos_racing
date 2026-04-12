#pragma once

#include <algorithm>
#include <memory>
#include <vector>
#include <srl.hpp>
#include "modelObject.hpp"
#include "track_renderer.hpp"
#include "track_serialized.hpp"

static constexpr bool kCarLoadLogs = false;

struct CarLoadResult
{
    // Pointer to car model allocated in cart RAM (ownership transferred to caller).
    std::unique_ptr<ModelObject> car{};
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
CarLoadResult LoadCarToCart(const char* const* paths, size_t pathCount, bool forceCart = false, size_t gouraudOffset = 0);
// Load a track NYA into cart RAM and build a TrackRenderer with the requested mesh cap.
TrackLoadResult LoadTrackToCart(const char* const* paths, size_t pathCount, size_t maxMeshes);
// Serialize track data into Cart RAM for fast segment buffering.
TrackSerializedCopy SerializeTrackToCart(const char* path, size_t chunkSize = 0x4000);

// Inline implementations to keep single translation unit usage (avoid duplicate std throw stubs)
inline CarLoadResult LoadCarToCart(const char* const* paths, size_t pathCount, bool forceCart, size_t gouraudOffset)
{
    CarLoadResult res{};
    int32_t hwrBefore = SRL::Memory::CartRam::GetFreeSpace();

    auto tryLoadPath = [&](const char* path) -> bool
    {
        SRL::Cd::File f(path);
        const bool exists = f.Exists() && f.Size.Bytes > 0;
        if constexpr (kCarLoadLogs)
        {
            SRL::Debug::Print(0, 2, "CAR path:%s ex:%d sz:%ld", path, exists ? 1 : 0, (long)f.Size.Bytes);
        }
        if (!exists)
        {
            return false;
        }

        ModelObject candidate(path, gouraudOffset, false, 0, false, forceCart, false);
        if (candidate.GetMeshCount() == 0 || candidate.GetFaceCount() == 0)
        {
            if constexpr (kCarLoadLogs)
            {
                SRL::Debug::Print(0, 3, "Car stream fail %s -> buffer", path);
            }
            ModelObject bufLoad(path, gouraudOffset, false, 0, false, forceCart, false);
            candidate = std::move(bufLoad);
        }

        if (candidate.GetMeshCount() > 0 && candidate.GetFaceCount() > 0)
        {
            res.car = std::make_unique<ModelObject>(std::move(candidate));
            res.loaded = true;
            return true;
        }
        return false;
    };

    SRL::Cd::ChangeDir((const char*)0);
    for (size_t i = 0; i < pathCount; ++i)
    {
        if (tryLoadPath(paths[i])) break;
    }

    // Fallback robusto: usa o mesmo metodo dos segmentos (ChangeDir + nome curto).
    if (!res.loaded)
    {
        const char* names[] = { "CAR1.NYA", "CAR1.NYA;1", "car1.nya", "car1.nya;1" };
        struct DirChain { const char* a; const char* b; };
        const DirChain dirChains[] = {
            { "DATA", nullptr },
            { "data", nullptr },
            { "DATA", "MODEL" },
            { "data", "model" },
            { "MODEL", nullptr },
            { "model", nullptr },
            { nullptr, nullptr }
        };

        for (const auto& chain : dirChains)
        {
            SRL::Cd::ChangeDir((const char*)0);
            if (chain.a) SRL::Cd::ChangeDir(chain.a);
            if (chain.b) SRL::Cd::ChangeDir(chain.b);

            for (const char* name : names)
            {
                if (tryLoadPath(name))
                {
                    SRL::Cd::ChangeDir((const char*)0);
                    break;
                }
            }
            SRL::Cd::ChangeDir((const char*)0);
            if (res.loaded) break;
        }
    }

    // Fallback final: leitura bruta do arquivo e parse por memoria.
    if (!res.loaded)
    {
        const char* names[] = { "CAR1.NYA", "CAR1.NYA;1", "car1.nya", "car1.nya;1" };
        struct DirChain { const char* a; const char* b; };
        const DirChain dirChains[] = {
            { "DATA", nullptr },
            { "data", nullptr },
            { "DATA", "MODEL" },
            { "data", "model" },
            { "MODEL", nullptr },
            { "model", nullptr },
            { nullptr, nullptr }
        };
        for (const auto& chain : dirChains)
        {
            SRL::Cd::ChangeDir((const char*)0);
            if (chain.a) SRL::Cd::ChangeDir(chain.a);
            if (chain.b) SRL::Cd::ChangeDir(chain.b);

            for (const char* name : names)
            {
                SRL::Cd::File f(name);
                const bool exists = f.Exists() && f.Size.Bytes > 0;
                if constexpr (kCarLoadLogs)
                {
                    SRL::Debug::Print(0, 2, "CAR raw:%s ex:%d sz:%ld", name, exists ? 1 : 0, (long)f.Size.Bytes);
                }
                if (!exists || !f.Open()) continue;

                const size_t size = static_cast<size_t>(f.Size.Bytes);
                std::vector<uint8_t> bytes(size);
                const int32_t read = f.Read(static_cast<int32_t>(size), bytes.data());
                if (read <= 0 || static_cast<size_t>(read) != size)
                {
                    if constexpr (kCarLoadLogs)
                    {
                        SRL::Debug::Print(0, 3, "CAR raw read fail %s r:%ld", name, (long)read);
                    }
                    continue;
                }

                ModelObject memCar;
                if (memCar.LoadFromMemory(bytes.data(), size, gouraudOffset, false, 0, false, forceCart))
                {
                    if (memCar.GetMeshCount() > 0 && memCar.GetFaceCount() > 0)
                    {
                        res.car = std::make_unique<ModelObject>(std::move(memCar));
                        res.loaded = true;
                        break;
                    }
                }
                if constexpr (kCarLoadLogs)
                {
                    SRL::Debug::Print(0, 3, "CAR raw parse fail %s", name);
                }
            }

            SRL::Cd::ChangeDir((const char*)0);
            if (res.loaded) break;
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

struct TrackSegmentCopy
{
    void* cartPtr = nullptr;
    size_t size = 0;
};

enum class CartCopyError
{
    None = 0,
    Missing,
    OpenFail,
    NoSpace,
    MallocFail,
    ReadError,
    ShortRead,
    Overflow
};

struct CartCopyToRamResult
{
    CartCopyError error = CartCopyError::None;
    void* cartPtr = nullptr;
    size_t desiredSize = 0;
    size_t allocSize = 0;
    size_t written = 0;
    size_t freeBefore = 0;
    int32_t hwrDelta = 0;
};

inline CartCopyToRamResult CopyCdFileToCartRam(const char* path, size_t chunkSize)
{
    CartCopyToRamResult result{};
    SRL::Cd::File file(path);
    if (!file.Exists())
    {
        result.error = CartCopyError::Missing;
        return result;
    }

    if (!file.Open())
    {
        result.error = CartCopyError::OpenFail;
        return result;
    }

    result.desiredSize = static_cast<size_t>(std::max<int32_t>(0, file.Size.Bytes));
    result.freeBefore = SRL::Memory::CartRam::GetFreeSpace();
    const size_t maxCartSize = SRL::Memory::CartRam::GetSize();
    result.allocSize = std::min({ result.desiredSize > 0 ? result.desiredSize : maxCartSize,
                                  result.freeBefore,
                                  maxCartSize });
    if (result.allocSize == 0)
    {
        result.error = CartCopyError::NoSpace;
        return result;
    }

    void* cartBuffer = SRL::Memory::CartRam::Malloc(result.allocSize);
    if (!cartBuffer)
    {
        result.error = CartCopyError::MallocFail;
        return result;
    }

    std::vector<uint8_t> tempBuffer(std::min(chunkSize, result.allocSize));
    while (result.written < result.allocSize)
    {
        const int32_t toRead = static_cast<int32_t>(std::min(tempBuffer.size(), result.allocSize - result.written));
        if (toRead <= 0) break;

        const int32_t read = file.Read(toRead, tempBuffer.data());
        if (read < 0)
        {
            SRL::Memory::CartRam::Free(cartBuffer);
            result.error = CartCopyError::ReadError;
            return result;
        }
        if (read == 0)
        {
            break;
        }

        slDMACopy(tempBuffer.data(), reinterpret_cast<uint8_t*>(cartBuffer) + result.written, static_cast<size_t>(read));
        result.written += static_cast<size_t>(read);
    }

    if (result.written < result.allocSize)
    {
        SRL::Memory::CartRam::Free(cartBuffer);
        result.error = CartCopyError::ShortRead;
        return result;
    }

    if (result.written == result.allocSize)
    {
        const int32_t extra = file.Read(1, tempBuffer.data());
        if (extra > 0)
        {
            SRL::Memory::CartRam::Free(cartBuffer);
            result.error = CartCopyError::Overflow;
            return result;
        }
    }

    result.cartPtr = cartBuffer;
    result.hwrDelta = static_cast<int32_t>(result.freeBefore - SRL::Memory::CartRam::GetFreeSpace());
    result.error = CartCopyError::None;
    return result;
}

inline TrackSerializedCopy SerializeTrackToCart(const char* path, size_t chunkSize)
{
    TrackSerializedCopy result{};
    const CartCopyToRamResult copy = CopyCdFileToCartRam(path, chunkSize);
    switch (copy.error)
    {
    case CartCopyError::None:
        result.cartPtr = copy.cartPtr;
        result.size = copy.written;
        result.hwrDelta = copy.hwrDelta;
        SRL::Debug::Print(1, 4, "Track serialized OK: %s cart:%08lx copied:%u delta:%d",
                          path, (unsigned long)copy.cartPtr, (unsigned)copy.written, copy.hwrDelta);
        return result;
    case CartCopyError::Missing:
        SRL::Debug::Print(1, 3, "Track serialize fail (missing): %s", path);
        return result;
    case CartCopyError::OpenFail:
        SRL::Debug::Print(1, 3, "Track serialize fail (open): %s", path);
        return result;
    case CartCopyError::NoSpace:
        SRL::Debug::Print(1, 3, "Track serialize fail (no cart space) %s desired:%u free:%u",
                          path, (unsigned)copy.desiredSize, (unsigned)copy.freeBefore);
        return result;
    case CartCopyError::MallocFail:
        SRL::Debug::Print(1, 3, "Track serialize fail (malloc): %s sz:%u", path, (unsigned)copy.allocSize);
        return result;
    case CartCopyError::ReadError:
        SRL::Debug::Print(1, 3, "Track serialize fail (read error): %s", path);
        return result;
    case CartCopyError::ShortRead:
        SRL::Debug::Print(1, 3, "Track serialize fail (short read): %s read:%u expected:%u",
                          path, (unsigned)copy.written, (unsigned)copy.allocSize);
        return result;
    case CartCopyError::Overflow:
        SRL::Debug::Print(1, 3, "Track serialize fail (overflow): %s desired:%u alloc:%u",
                          path, (unsigned)copy.desiredSize, (unsigned)copy.allocSize);
        return result;
    }
    return result;
}

inline TrackSegmentCopy CopyTrackSegmentToCart(const char* path, size_t chunkSize = 0x4000)
{
    TrackSegmentCopy result{};
    const CartCopyToRamResult copy = CopyCdFileToCartRam(path, chunkSize);
    switch (copy.error)
    {
    case CartCopyError::None:
        result.cartPtr = copy.cartPtr;
        result.size = copy.written;
        SRL::Debug::Print(1, 4, "Segment copied: %s cart:%08lx size:%u",
                          path, (unsigned long)copy.cartPtr, (unsigned)copy.written);
        return result;
    case CartCopyError::Missing:
        SRL::Debug::Print(1, 3, "Segment copy fail (missing): %s", path);
        return result;
    case CartCopyError::OpenFail:
        SRL::Debug::Print(1, 3, "Segment copy fail (open): %s", path);
        return result;
    case CartCopyError::NoSpace:
        SRL::Debug::Print(1, 3, "Segment copy fail (no cart space) %s desired:%u free:%u",
                          path, (unsigned)copy.desiredSize, (unsigned)copy.freeBefore);
        return result;
    case CartCopyError::MallocFail:
        SRL::Debug::Print(1, 3, "Segment copy fail (malloc): %s sz:%u", path, (unsigned)copy.allocSize);
        return result;
    case CartCopyError::ReadError:
        SRL::Debug::Print(1, 3, "Segment copy fail (read error): %s", path);
        return result;
    case CartCopyError::ShortRead:
        SRL::Debug::Print(1, 3, "Segment copy fail (short read): %s read:%u expected:%u",
                          path, (unsigned)copy.written, (unsigned)copy.allocSize);
        return result;
    case CartCopyError::Overflow:
        SRL::Debug::Print(1, 3, "Segment copy fail (overflow): %s desired:%u alloc:%u",
                          path, (unsigned)copy.desiredSize, (unsigned)copy.allocSize);
        return result;
    }
    return result;
}
