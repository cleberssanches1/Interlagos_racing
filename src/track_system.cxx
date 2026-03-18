#include "track_system.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <string.h>
#include <vector>
#include <utility>
#include <errno.h>
#include <limits>

#include "modelObject.hpp"
#include "resource_loader.hpp"
#include "segment_component_loader.hpp"
#include "segment_draw_ready_loader.hpp"
#include "segment_runtime_draw_loader.hpp"
#include "track_runtime_pack_loader.hpp"
#include "batch_draw_ready_loader.hpp"
#include "srl_tga.hpp"

using SRL::Math::Types::Vector3D;

namespace
{
struct Segment1TextureJson
{
    int familyIds[512]{};
    char familyTex64[512][64]{};
    size_t familyCount = 0;
    TrackLowWorkVector<int> faceFamily{};
};

struct TexbankIndexLod
{
    int familyIds[128]{};
    char files[128][64]{};
    size_t count = 0;
};

struct RenTextureMapEntry
{
    int lod = 0;
    char sourceName[64]{};
    char targetName[32]{};
};

struct RenTextureMap
{
    TrackLowWorkVector<RenTextureMapEntry> entries{};
};

struct PackedAssetEntryMeta
{
    char name[65]{};
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct PackedAssetCache
{
    void* cartPtr = nullptr;
    uint32_t size = 0;
    char sourcePath[96]{}; 
    TrackLowWorkVector<PackedAssetEntryMeta> entries{};
    uint32_t trackedEntryBytes = 0;
};

struct TrackRuntimePackCache
{
    void* cartPtr = nullptr;
    uint32_t size = 0;
    char sourcePath[96]{};
    TrackRuntimePack::Loader::View view{};
};

struct RdrBuildScratch
{
    SegmentRuntimeDraw::Blob blob{};
    std::vector<SRL::Math::Types::Vector3D> verts{};
    std::vector<SRL::Types::Polygon> faces{};
    std::vector<SRL::Types::Attribute> attrs{};
};

struct SdrBuildScratch
{
    SegmentDrawReady::Blob blob{};
    std::vector<SRL::Math::Types::Vector3D> verts{};
    std::vector<SRL::Types::Polygon> faces{};
    std::vector<SRL::Types::Attribute> attrs{};
};

struct BdrBatchBuildResult
{
    std::unique_ptr<TrackRenderer> renderer{};
    Vector3D center{};
    TrackLowWorkU16Vector familyIds{};
    TrackLowWorkU8Vector faceRankOffsets{};
};

// Sticky diagnostics for TGA preload path resolution.
static char g_tgaLastTry[96] = "none";
static char g_tgaLastResult[96] = "none";
static char g_tgaLastName[64] = "none";
static uint32_t g_smapBytes = 0;
static char g_smapSig[24] = "none";
static char g_smapHead[48] = "none";
static Segment1TextureJson g_seg1MapCache{};
static bool g_seg1MapCacheValid = false;
static uint32_t g_packedAssetCacheEntryBytesLwr = 0;
static RdrBuildScratch g_rdrBuildScratch{};
static SdrBuildScratch g_sdrBuildScratch{};
static SegmentRuntimeDraw::Blob g_rdrFamilyIdsScratch{};
static SegmentDrawReady::Blob g_sdrFamilyIdsScratch{};
// Visible window LOD distribution (20 segments total):
// 4x 64x64, 5x 32x32, 5x 16x16, 6x 8x8.
static constexpr uint32_t kLodBand64Count = 4u;
static constexpr uint32_t kLodBand32Count = 5u;
static constexpr uint32_t kLodBand16Count = 5u;
static constexpr uint32_t kLodBand8Count = 6u;
static constexpr size_t kWorkRamPlanningHeadroomBytes = 48u * 1024u;
static constexpr size_t kWorkRamHardFloorBytes = 24u * 1024u;
// Temporary stabilization mode:
// - keep the sliding window running
// - keep tail segments entering in 8x8
// - disable visible-segment lod promotions/demotions during gameplay
// - disable destructive texture/palette release while racing
// This isolates runtime lifetime bugs from the offline asset pipeline.
static constexpr bool kEnableTrackRuntimeStabilization = true;

static int32_t WrapSegmentIdToRange(int32_t segmentId, uint16_t totalSegmentCount)
{
    if (totalSegmentCount == 0) return -1;
    const int32_t total = static_cast<int32_t>(totalSegmentCount);
    // Convert arbitrary integer to 1..N id range while preserving valid 1-based ids.
    int32_t normalized = (segmentId - 1) % total;
    if (normalized < 0) normalized += total;
    return normalized + 1;
}

static bool IsVdp1TextureSlotLive(uint16_t slot)
{
    if (slot == No_Texture) return false;
    if (slot >= SRL_MAX_TEXTURES) return false;
    if (slot >= SRL::VDP1::GetTextureCount()) return false;
    return SRL::VDP1::Metadata[slot].Texture != nullptr;
}

static size_t GetHighWorkRamFreeBytesSafe(bool* outValid = nullptr)
{
    const auto report = SRL::Memory::HighWorkRam::GetReport();
    const bool valid = (report.TotalSize > 0u) && (report.FreeSize <= report.TotalSize);
    if (outValid) *outValid = valid;
    return valid ? report.FreeSize : 0u;
}

template <typename SlotT>
static bool HasMissingFaceTextureSlots(const std::vector<SlotT>& slots)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (static_cast<int32_t>(slots[i]) < 0) return true;
    }
    return false;
}

template <typename SlotT, typename FamilyVecT>
static bool HasMissingRequiredFaceTextureSlots(const std::vector<SlotT>& slots,
                                               const FamilyVecT* familyIds)
{
    if (!familyIds || familyIds->size() != slots.size())
    {
        return HasMissingFaceTextureSlots(slots);
    }

    for (size_t i = 0; i < slots.size(); ++i)
    {
        if ((*familyIds)[i] == 0) continue;
        if (static_cast<int32_t>(slots[i]) < 0) return true;
    }
    return false;
}

template <typename SlotT, typename FamilyVecT>
static uint32_t CountMissingOrDeadRequiredFaceTextureSlots(const std::vector<SlotT>& slots,
                                                           const FamilyVecT* familyIds)
{
    if (!familyIds || familyIds->size() != slots.size())
    {
        uint32_t missing = 0;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            const int32_t slot = static_cast<int32_t>(slots[i]);
            if (slot < 0) ++missing;
        }
        return missing;
    }

    uint32_t missing = 0;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if ((*familyIds)[i] == 0) continue;
        const int32_t slot = static_cast<int32_t>(slots[i]);
        if (slot < 0)
        {
            ++missing;
            continue;
        }
        if (slot >= static_cast<int32_t>(SRL_MAX_TEXTURES))
        {
            ++missing;
            continue;
        }
        if (!IsVdp1TextureSlotLive(static_cast<uint16_t>(slot)))
        {
            ++missing;
        }
    }
    return missing;
}

template <typename VecT>
static uint32_t VectorCapacityBytesSafe(const VecT& v)
{
    using T = typename VecT::value_type;
    const uint64_t bytes = static_cast<uint64_t>(v.capacity()) * static_cast<uint64_t>(sizeof(T));
    return (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(bytes);
}

template <typename VecT>
static uint32_t VectorCapacityElementsSafe(const VecT& v)
{
    return (v.capacity() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(v.capacity());
}

template <typename VecT>
static bool TrimVectorSlack(VecT& v, size_t keepCapacityElements, bool aggressive)
{
    using T = typename VecT::value_type;
    const size_t desired = std::max(keepCapacityElements, v.size());
    if (v.capacity() <= desired) return false;

    const size_t slackElements = v.capacity() - desired;
    const size_t slackBytes = slackElements * sizeof(T);
    if (!aggressive)
    {
        if (v.capacity() <= (desired * 2u + 8u)) return false;
        if (slackBytes < (2u * 1024u)) return false;
    }

    VecT compact{};
    compact.reserve(desired);
    compact.insert(compact.end(), v.begin(), v.end());
    v.swap(compact);
    return true;
}

template <typename VecT>
static bool CompactEmptyVectorForTarget(VecT& v, size_t targetCapacityElements)
{
    using T = typename VecT::value_type;
    if (!v.empty()) return false;
    const size_t cap = v.capacity();
    if (cap <= targetCapacityElements) return false;
    if (cap <= (targetCapacityElements * 2u + 8u)) return false;

    const size_t slackElements = cap - targetCapacityElements;
    const size_t slackBytes = slackElements * sizeof(T);
    if (slackBytes < 512u) return false;

    VecT compact{};
    compact.reserve(targetCapacityElements);
    v.swap(compact);
    return true;
}

template <typename TBlob>
static void ResetBlobBytes(TBlob& blob)
{
    std::vector<uint8_t> empty{};
    blob.bytes.swap(empty);
    blob.size = 0;
    blob.loaded = false;
}

static void TrimRuntimeBlobScratchCaches(bool aggressive)
{
    if (aggressive)
    {
        ResetBlobBytes(g_rdrBuildScratch.blob);
        ResetBlobBytes(g_sdrBuildScratch.blob);
        ResetBlobBytes(g_rdrFamilyIdsScratch);
        ResetBlobBytes(g_sdrFamilyIdsScratch);
        std::vector<Vector3D>{}.swap(g_rdrBuildScratch.verts);
        std::vector<Vector3D>{}.swap(g_sdrBuildScratch.verts);
        std::vector<SRL::Types::Polygon>{}.swap(g_rdrBuildScratch.faces);
        std::vector<SRL::Types::Polygon>{}.swap(g_sdrBuildScratch.faces);
        std::vector<SRL::Types::Attribute>{}.swap(g_rdrBuildScratch.attrs);
        std::vector<SRL::Types::Attribute>{}.swap(g_sdrBuildScratch.attrs);
        return;
    }

    (void)TrimVectorSlack(g_rdrBuildScratch.blob.bytes, 0u, true);
    (void)TrimVectorSlack(g_sdrBuildScratch.blob.bytes, 0u, true);
    (void)TrimVectorSlack(g_rdrFamilyIdsScratch.bytes, 0u, true);
    (void)TrimVectorSlack(g_sdrFamilyIdsScratch.bytes, 0u, true);
    (void)TrimVectorSlack(g_rdrBuildScratch.verts, 0u, true);
    (void)TrimVectorSlack(g_sdrBuildScratch.verts, 0u, true);
    (void)TrimVectorSlack(g_rdrBuildScratch.faces, 0u, true);
    (void)TrimVectorSlack(g_sdrBuildScratch.faces, 0u, true);
    (void)TrimVectorSlack(g_rdrBuildScratch.attrs, 0u, true);
    (void)TrimVectorSlack(g_sdrBuildScratch.attrs, 0u, true);
}

static uint32_t EstimateRuntimeBlobScratchBytes()
{
    uint64_t bytes = 0;
    bytes += VectorCapacityBytesSafe(g_rdrBuildScratch.blob.bytes);
    bytes += VectorCapacityBytesSafe(g_sdrBuildScratch.blob.bytes);
    bytes += VectorCapacityBytesSafe(g_rdrFamilyIdsScratch.bytes);
    bytes += VectorCapacityBytesSafe(g_sdrFamilyIdsScratch.bytes);
    bytes += VectorCapacityBytesSafe(g_rdrBuildScratch.verts);
    bytes += VectorCapacityBytesSafe(g_sdrBuildScratch.verts);
    bytes += VectorCapacityBytesSafe(g_rdrBuildScratch.faces);
    bytes += VectorCapacityBytesSafe(g_sdrBuildScratch.faces);
    bytes += VectorCapacityBytesSafe(g_rdrBuildScratch.attrs);
    bytes += VectorCapacityBytesSafe(g_sdrBuildScratch.attrs);
    return (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(bytes);
}

static void NormalizeTextureFileName(const char* in, char* out, size_t outSize);
static void InvalidatePackedAssetCache(PackedAssetCache& cache);
static void InvalidateTrackRuntimePackCache(TrackRuntimePackCache& cache);
static void UpdatePackedAssetCacheTrackedBytes(PackedAssetCache& cache);
static bool ReadCdFileFully(SRL::Cd::File& file, uint32_t totalBytes, uint8_t* dst, uint32_t& outReadBytes);
static bool LoadTrackRuntimePackToCart(const char* const* candidates, size_t count, TrackRuntimePackCache& cache);
static bool LoadPackedAssetIndexToCart(const char* const* candidates, size_t count, PackedAssetCache& cache);
static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentComponent::Blob& out);
static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentDrawReady::Blob& out);
static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentRuntimeDraw::Blob& out);
static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, BatchDrawReady::Blob& out);


// Validate a renderer before issuing draw calls.
// This prevents invalid state from reaching VDP1 command generation.
static bool IsRendererStateIntegral(const TrackRenderer& renderer)
{
    if (!renderer.HasTrack()) return false;
    if (renderer.MeshCount() == 0) return false;
    if (renderer.FaceCount() == 0) return false;
    if (renderer.VertexCount() == 0) return false;
    if (renderer.DrawLimit() == 0) return false;
    if (renderer.DrawLimit() > renderer.MeshCount()) return false;
    const auto stats = renderer.MemStats();
    if (stats.faces > 0 && renderer.FaceCount() != stats.faces) return false;
    if (stats.verts > 0 && renderer.VertexCount() != stats.verts) return false;
    return true;
}

// Try to repair a renderer state with safe defaults.
// Returns true when the renderer becomes integral after repair.
static bool TryRepairRendererState(TrackRenderer& renderer)
{
    if (IsRendererStateIntegral(renderer)) return true;
    if (!renderer.HasTrack()) return false;
    if (renderer.MeshCount() == 0) return false;
    renderer.SetDrawLimit(renderer.MeshCount());
    return IsRendererStateIntegral(renderer);
}

static void ConfigureStreamedRendererDefaults(TrackRenderer& renderer)
{
    renderer.SetUseOriginal(false);
    renderer.SetSglDirect(false);
    renderer.SetVdp1Commands(false);
    renderer.SetDirect2D(false);
    renderer.SetForceDoubleSided(false);
    renderer.SetScale(SRL::Math::Types::Fxp::BuildRaw(1 << 16));
    renderer.SetDrawLimit(renderer.MeshCount());
}

struct CartTextCacheEntry
{
    char key[96]{};
    void* cartPtr = nullptr;
    uint32_t size = 0;
};

static std::vector<CartTextCacheEntry> g_textCache{};

static void NormalizeLoadedTextEncoding(std::vector<char>& text)
{
    if (text.empty()) return;

    auto asU8 = [](char c) -> uint8_t { return static_cast<uint8_t>(c); };

    // Remove UTF-8 BOM
    if (text.size() >= 3 &&
        asU8(text[0]) == 0xEF &&
        asU8(text[1]) == 0xBB &&
        asU8(text[2]) == 0xBF)
    {
        text.erase(text.begin(), text.begin() + 3);
    }

    if (text.empty()) return;

    const bool hasUtf16LeBom =
        text.size() >= 2 &&
        asU8(text[0]) == 0xFF &&
        asU8(text[1]) == 0xFE;

    // Heuristic: if many NUL bytes in first chunk, assume UTF-16LE.
    bool looksUtf16Le = hasUtf16LeBom;
    if (!looksUtf16Le)
    {
        const size_t sample = (text.size() > 128) ? 128 : text.size();
        size_t nulCount = 0;
        for (size_t i = 0; i < sample; ++i)
        {
            if (text[i] == '\0') ++nulCount;
        }
        if (nulCount > sample / 4) looksUtf16Le = true;
    }

    if (looksUtf16Le)
    {
        const size_t start = hasUtf16LeBom ? 2 : 0;
        std::vector<char> out{};
        out.reserve((text.size() - start) / 2 + 1);
        for (size_t i = start; i + 1 < text.size(); i += 2)
        {
            out.push_back(text[i]); // keep low byte (ASCII subset)
        }
        text.swap(out);
    }

    // Strip trailing NULs and ensure a single terminator.
    while (!text.empty() && text.back() == '\0') text.pop_back();
    text.push_back('\0');
}

static void BuildTextCacheKey(const char* path, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!path || path[0] == '\0') return;

    const char* base = path;
    for (const char* p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    size_t n = 0;
    while (base[n] != '\0' && base[n] != ';' && n + 1 < outSize)
    {
        char c = base[n];
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        out[n] = c;
        ++n;
    }
    out[n] = '\0';
}

static bool TryReadCachedTextByKey(const char* key, std::vector<char>& outText)
{
    if (!key || key[0] == '\0') return false;
    for (size_t i = 0; i < g_textCache.size(); ++i)
    {
        const auto& e = g_textCache[i];
        if (::strcmp(e.key, key) != 0) continue;
        if (!e.cartPtr || e.size == 0) return false;

        const char* src = static_cast<const char*>(e.cartPtr);
        outText.assign(src, src + e.size);
        return !outText.empty();
    }
    return false;
}

static void StoreCachedTextByKey(const char* key, const std::vector<char>& text)
{
    if (!key || key[0] == '\0' || text.empty()) return;
    for (size_t i = 0; i < g_textCache.size(); ++i)
    {
        if (::strcmp(g_textCache[i].key, key) == 0) return;
    }

    const uint32_t bytes = static_cast<uint32_t>(text.size());
    void* mem = SRL::Memory::CartRam::Malloc(bytes);
    if (!mem) return;
    ::memcpy(mem, text.data(), bytes);

    CartTextCacheEntry e{};
    ::strncpy(e.key, key, sizeof(e.key) - 1);
    e.key[sizeof(e.key) - 1] = '\0';
    e.cartPtr = mem;
    e.size = bytes;
    g_textCache.push_back(e);
}

static bool ReadCdFileText(const char* const* names, size_t count, std::vector<char>& outText)
{
    auto tryReadOne = [&](const char* path) -> bool
    {
        if (!path || path[0] == '\0') return false;
        SRL::Debug::Print(1, 3, "TGA map cd candidate:%s", path);
        SRL::Cd::File f(path);
        if (!f.Exists() || f.Size.Bytes <= 0) return false;
        if (!f.Open()) return false;
        const size_t size = static_cast<size_t>(f.Size.Bytes);
        outText.clear();
        outText.reserve(size + 1);
        std::vector<char> chunk(2048);
        size_t totalRead = 0;
        while (totalRead < size)
        {
            const int32_t want = static_cast<int32_t>(std::min<size_t>(chunk.size(), size - totalRead));
            const int32_t got = f.Read(want, chunk.data());
            if (got <= 0) break;
            outText.insert(outText.end(), chunk.begin(), chunk.begin() + got);
            totalRead += static_cast<size_t>(got);
            if (got < want) break;
        }
        if (outText.empty()) return false;
        outText.push_back('\0');
        NormalizeLoadedTextEncoding(outText);
        char key[96]{};
        BuildTextCacheKey(path, key, sizeof(key));
        StoreCachedTextByKey(key, outText);
        SRL::Debug::Print(1, 3, "TGA map cd ok:%s", path);
        return true;
    };

    for (size_t i = 0; i < count; ++i)
    {
        const char* n = names[i];
        if (!n || n[0] == '\0') continue;
        char key[96]{};
        BuildTextCacheKey(n, key, sizeof(key));
        if (TryReadCachedTextByKey(key, outText)) return true;
        if (tryReadOne(n)) return true;

        // Some ISO builds expose versions other than ;1 (e.g. ;11).
        if (::strchr(n, ';') == nullptr)
        {
            for (int v = 1; v <= 31; ++v)
            {
                char nv[128]{};
                std::snprintf(nv, sizeof(nv), "%s;%d", n, v);
                if (tryReadOne(nv)) return true;
            }
        }
    }
    SRL::Debug::Print(1, 3, "TGA map local not found");
    return false;
}

static bool ReadLocalSegmentsMap(std::vector<char>& outText)
{
    const char* names[] = {
       // "sap.json",
      //  "SAP.JSON",
      //  "SAP",
      //  "SAP.json",
      //  "SAP;1",
        "smap",
      //  "seg_map",
      //  "SEG_MAP",
       // "segmap",
      //  "segments_map.json",
      //  "segments_map_before_rebuild.json",
      //  "segments_map",
        "SMAP"
    };
    const char* dirs[] = {
        "",
        "cd/data",
        "CD/DATA"
    };

    for (size_t d = 0; d < sizeof(dirs)/sizeof(dirs[0]); ++d)
    {
        const char* dir = dirs[d];
        for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); ++i)
        {
            const char* base = names[i];
            char path[256]{};
            if (dir[0] == '\0')
            {
                std::snprintf(path, sizeof(path), "%s", base);
            }
            else
            {
                std::snprintf(path, sizeof(path), "%s/%s", dir, base);
            }
            SRL::Debug::Print(1, 3, "TGA map candidate:%s", path);
            FILE* f = std::fopen(path, "rb");
            if (!f)
            {
                SRL::Debug::Print(1, 6, "TGA map local miss:%s errno:%d", path, errno);
                continue;
            }
            std::fseek(f, 0, SEEK_END);
            const long size = std::ftell(f);
            if (size <= 0)
            {
                std::fclose(f);
                SRL::Debug::Print(1, 6, "TGA map local empty:%s", path);
                continue;
            }
            std::fseek(f, 0, SEEK_SET);
            outText.assign(static_cast<size_t>(size + 1), '\0');
            const size_t read = std::fread(outText.data(), 1, static_cast<size_t>(size), f);
            std::fclose(f);
            if (read == 0)
            {
                SRL::Debug::Print(1, 6, "TGA map local read fail:%s", path);
                continue;
            }
            outText.resize(read);
            outText.push_back('\0');
            NormalizeLoadedTextEncoding(outText);
            SRL::Debug::Print(1, 3, "TGA map local ok:%s", path);
            return true;
        }
    }
    return false;
}

static bool ReadCdFileBinary(const char* const* names, size_t count, std::vector<uint8_t>& outData)
{
    auto tryReadOne = [&](const char* path) -> bool
    {
        if (!path || path[0] == '\0') return false;
        SRL::Cd::File f(path);
        if (!f.Exists() || f.Size.Bytes <= 0) return false;
        if (!f.Open()) return false;
        const size_t size = static_cast<size_t>(f.Size.Bytes);
        outData.clear();
        outData.reserve(size);
        std::vector<uint8_t> chunk(2048);
        size_t totalRead = 0;
        while (totalRead < size)
        {
            const int32_t want = static_cast<int32_t>(std::min<size_t>(chunk.size(), size - totalRead));
            const int32_t got = f.Read(want, chunk.data());
            if (got <= 0) break;
            outData.insert(outData.end(), chunk.begin(), chunk.begin() + got);
            totalRead += static_cast<size_t>(got);
            if (got < want) break;
        }
        if (outData.empty()) return false;
        return true;
    };

    for (size_t i = 0; i < count; ++i)
    {
        const char* n = names[i];
        if (!n || n[0] == '\0') continue;
        if (tryReadOne(n)) return true;

        if (::strchr(n, ';') == nullptr)
        {
            for (int v = 1; v <= 31; ++v)
            {
                char nv[128]{};
                std::snprintf(nv, sizeof(nv), "%s;%d", n, v);
                if (tryReadOne(nv)) return true;
            }
        }
    }
    return false;
}

static bool ParseIntAfterKey(const char* p, const char* key, int& out)
{
    char pattern[64]{};
    std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* k = strstr(p, pattern);
    if (!k) return false;
    const char* c = strchr(k, ':');
    if (!c) return false;
    ++c;
    while (*c && std::isspace(static_cast<unsigned char>(*c))) ++c;
    const long v = strtol(c, nullptr, 10);
    out = static_cast<int>(v);
    return true;
}

static bool ParseString64AfterKey(const char* p, const char* key, char* out, size_t outSize)
{
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    char pattern[64]{};
    std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* k = strstr(p, pattern);
    if (!k) return false;
    const char* c = strchr(k, ':');
    if (!c) return false;
    const char* q0 = strchr(c, '"');
    if (!q0) return false;
    ++q0;
    const char* q1 = strchr(q0, '"');
    if (!q1) return false;
    size_t n = static_cast<size_t>(q1 - q0);
    if (n >= outSize) n = outSize - 1;
    memcpy(out, q0, n);
    out[n] = '\0';
    return n > 0;
}

static bool ParseTexbankIndex(const char* json, TexbankIndexLod& out)
{
    out.count = 0;
    if (!json || json[0] == '\0') return false;

    const char* entries = strstr(json, "\"entries\"");
    if (!entries) return false;

    const char* p = entries;
    while (p && out.count < 128)
    {
        const char* idk = strstr(p, "\"familyId\"");
        if (!idk) break;

        int fam = -1;
        if (!ParseIntAfterKey(idk, "familyId", fam) || fam <= 0)
        {
            p = idk + 10;
            continue;
        }

        char file[64]{};
        if (!ParseString64AfterKey(idk, "file", file, sizeof(file)))
        {
            p = idk + 10;
            continue;
        }

        out.familyIds[out.count] = fam;
        strncpy(out.files[out.count], file, sizeof(out.files[out.count]) - 1);
        ++out.count;
        p = idk + 10;
    }
    return out.count > 0;
}

static bool ParseRenTextureCopyMap(const char* json, RenTextureMap& out)
{
    out.entries.clear();
    out.entries.reserve(1024);
    if (!json || json[0] == '\0') return false;

    const char* items = strstr(json, "\"items\"");
    if (!items) return false;
    const char* p = items;
    while (p)
    {
        const char* sk = strstr(p, "\"source_name\"");
        if (!sk) break;

        char src[64]{};
        char dst[32]{};
        int lod = 0;
        if (!ParseString64AfterKey(sk, "source_name", src, sizeof(src)))
        {
            p = sk + 12;
            continue;
        }
        if (!ParseString64AfterKey(sk, "target_name", dst, sizeof(dst)))
        {
            p = sk + 12;
            continue;
        }
        (void)ParseIntAfterKey(sk, "lod", lod);
        if (lod != 8 && lod != 16 && lod != 32 && lod != 64)
        {
            p = sk + 12;
            continue;
        }

        RenTextureMapEntry e{};
        strncpy(e.sourceName, src, sizeof(e.sourceName) - 1);
        strncpy(e.targetName, dst, sizeof(e.targetName) - 1);
        e.lod = lod;
        out.entries.push_back(e);
        p = sk + 12;
    }
    return !out.entries.empty();
}

static int FindTexbankFileIndexByFamily(const TexbankIndexLod& idx, int familyId)
{
    for (size_t i = 0; i < idx.count; ++i)
    {
        if (idx.familyIds[i] == familyId) return static_cast<int>(i);
    }
    return -1;
}

static bool FindRenamedTarget(const RenTextureMap& map, const char* sourceName, int lod, char* out, size_t outSize)
{
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (!sourceName || sourceName[0] == '\0') return false;

    char srcNorm[64]{};
    NormalizeTextureFileName(sourceName, srcNorm, sizeof(srcNorm));

    auto eqIgnoreCase = [](const char* a, const char* b) -> bool
    {
        if (!a || !b) return false;
        while (*a && *b)
        {
            char ca = *a;
            char cb = *b;
            if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 'a' + 'A');
            if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
            if (ca != cb) return false;
            ++a; ++b;
        }
        return (*a == '\0' && *b == '\0');
    };

    for (size_t i = 0; i < map.entries.size(); ++i)
    {
        const auto& e = map.entries[i];
        if (e.lod != lod) continue;
        if (eqIgnoreCase(e.sourceName, srcNorm))
        {
            strncpy(out, e.targetName, outSize - 1);
            out[outSize - 1] = '\0';
            return out[0] != '\0';
        }
    }
    return false;
}

static bool ParseFaceFamilyArrayForSegment1(const char* json, Segment1TextureJson& out)
{
    const char* segs = strstr(json, "\"segments\"");
    if (!segs) return false;
    const char* arrKey = nullptr;
    const char* scanPos = segs;
    while (scanPos && *scanPos)
    {
        const char* idk = strstr(scanPos, "\"id\"");
        if (!idk) break;

        int segId = -1;
        if (!ParseIntAfterKey(idk, "id", segId))
        {
            scanPos = idk + 4;
            continue;
        }

        const char* nextId = strstr(idk + 4, "\"id\"");
        const char* objEnd = nextId ? nextId : (json + strlen(json));
        if (segId == 1)
        {
            const char* candidate = strstr(idk, "\"faceTextureFamily\"");
            if (candidate && candidate < objEnd)
            {
                arrKey = candidate;
                break;
            }
        }
        scanPos = idk + 4;
    }
    if (!arrKey) return false;
    const char* b0 = strchr(arrKey, '[');
    if (!b0) return false;
    const char* b1 = strchr(b0, ']');
    if (!b1) return false;

    out.faceFamily.clear();
    const char* arrPos = b0 + 1;
    while (arrPos < b1)
    {
        while (arrPos < b1 && (std::isspace(static_cast<unsigned char>(*arrPos)) || *arrPos == ',')) ++arrPos;
        if (arrPos >= b1) break;
        char* endp = nullptr;
        const long v = strtol(arrPos, &endp, 10);
        if (endp == arrPos)
        {
            ++arrPos;
            continue;
        }
        out.faceFamily.push_back(static_cast<int>(v));
        arrPos = endp;
    }
    return !out.faceFamily.empty();
}

static bool ParseTextureFamilies64(const char* json, Segment1TextureJson& out)
{
    out.familyCount = 0;
    const char* tf = strstr(json, "\"textureFamilies\"");
    if (!tf) return false;
    const char* end = strstr(tf, "\"segments\"");
    if (!end) end = json + strlen(json);

    const char* p = tf;
    while (p && p < end && out.familyCount < 512)
    {
        const char* idk = strstr(p, "\"id\"");
        if (!idk || idk >= end) break;
        int famId = -1;
        if (!ParseIntAfterKey(idk, "id", famId)) { p = idk + 4; continue; }

        // Prefer imageFiles -> "64", fallback variants -> "64"
        char tex[64]{};
        const char* nextId = strstr(idk + 4, "\"id\"");
        if (!nextId || nextId > end) nextId = end;

        const char* img = strstr(idk, "\"imageFiles\"");
        if (img && img < nextId)
        {
            (void)ParseString64AfterKey(img, "64", tex, sizeof(tex));
        }
        if (tex[0] == '\0')
        {
            const char* var = strstr(idk, "\"variants\"");
            if (var && var < nextId)
            {
                (void)ParseString64AfterKey(var, "64", tex, sizeof(tex));
            }
        }
        if (famId >= 0 && tex[0] != '\0')
        {
            out.familyIds[out.familyCount] = famId;
            strncpy(out.familyTex64[out.familyCount], tex, sizeof(out.familyTex64[out.familyCount]) - 1);
            ++out.familyCount;
        }
        p = idk + 4;
    }
    return out.familyCount > 0;
}

static void NormalizeTextureFileName(const char* in, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!in || in[0] == '\0') return;

    const char* base = in;
    for (const char* p = in; *p; ++p)
    {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    strncpy(out, base, outSize - 1);
    out[outSize - 1] = '\0';

    const char* dot = strrchr(out, '.');
    if (!dot)
    {
        const size_t n = strlen(out);
        if (n + 4 < outSize) strcat(out, ".tga");
        return;
    }

    auto ieqExt = [](const char* a, const char* b) -> bool
    {
        if (!a || !b) return false;
        while (*a && *b)
        {
            char ca = *a;
            char cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
            if (ca != cb) return false;
            ++a; ++b;
        }
        return (*a == '\0' && *b == '\0');
    };
    if (ieqExt(dot, ".tga") || ieqExt(dot, ".png"))
    {
        return;
    }

    const size_t idx = static_cast<size_t>(dot - out);
    out[idx] = '\0';
    const size_t n = strlen(out);
    if (n + 4 < outSize) strcat(out, ".tga");
}

static void BuildLodTextureName(const char* srcName, int lod, bool compactNoUnderscore, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!srcName || srcName[0] == '\0') return;

    char norm[64]{};
    NormalizeTextureFileName(srcName, norm, sizeof(norm));
    if (norm[0] == '\0') return;

    const char* dot = ::strrchr(norm, '.');
    char base[64]{};
    char ext[16]{};
    if (dot)
    {
        const size_t bn = static_cast<size_t>(dot - norm);
        const size_t cpy = (bn < sizeof(base) - 1) ? bn : (sizeof(base) - 1);
        ::memcpy(base, norm, cpy);
        base[cpy] = '\0';
        ::strncpy(ext, dot, sizeof(ext) - 1);
    }
    else
    {
        ::strncpy(base, norm, sizeof(base) - 1);
        ::strncpy(ext, ".tga", sizeof(ext) - 1);
    }

    char lodTxt[4]{};
    std::snprintf(lodTxt, sizeof(lodTxt), "%d", lod);

    auto ends_with = [](const char* s, const char* suf) -> bool
    {
        const size_t ls = ::strlen(s);
        const size_t lf = ::strlen(suf);
        if (ls < lf) return false;
        return ::strncmp(s + (ls - lf), suf, lf) == 0;
    };

    bool replaced = false;
    const char* tails[] = { "_64", "_32", "_16", "_8", "64", "32", "16", "8" };
    for (size_t i = 0; i < sizeof(tails) / sizeof(tails[0]); ++i)
    {
        if (!ends_with(base, tails[i])) continue;
        const size_t lb = ::strlen(base);
        const size_t lt = ::strlen(tails[i]);
        const size_t keep = (lb >= lt) ? (lb - lt) : 0;
        char tmp[64]{};
        if (keep > 0) ::memcpy(tmp, base, keep);
        tmp[keep] = '\0';
        if (tails[i][0] == '_')
        {
            std::snprintf(base, sizeof(base), "%s_%s", tmp, lodTxt);
        }
        else
        {
            std::snprintf(base, sizeof(base), "%s%s", tmp, lodTxt);
        }
        replaced = true;
        break;
    }

    if (!replaced)
    {
        const size_t lb = ::strlen(base);
        const size_t needU = lb + 1 + ::strlen(lodTxt);
        if (!compactNoUnderscore && needU <= 8)
        {
            std::snprintf(base, sizeof(base), "%s_%s", base, lodTxt);
        }
        else
        {
            std::snprintf(base, sizeof(base), "%s%s", base, lodTxt);
        }
    }

    if (compactNoUnderscore)
    {
        const size_t lb = ::strlen(base);
        if (lb >= 2)
        {
            const char c0 = base[lb - 1];
            const char c1 = base[lb - 2];
            const bool isDigit0 = (c0 >= '0' && c0 <= '9');
            const bool isDigit1 = (c1 >= '0' && c1 <= '9');
            if (isDigit0 && base[lb - 2] == '_' && (lb >= 3))
            {
                ::memmove(base + lb - 2, base + lb - 1, 2);
            }
            else if (isDigit0 && isDigit1 && (lb >= 3) && base[lb - 3] == '_')
            {
                ::memmove(base + lb - 3, base + lb - 2, 3);
            }
        }
    }

    std::snprintf(out, outSize, "%s%s", base, ext);
}

static int32_t TryLoadTextureFromCd(const char* fileName)
{
    if (!fileName || fileName[0] == '\0') return -1;

    static char sLastTriedTexPath[96]{};
    static uint8_t sLastTriedTexOpen = 0;

    char upperName[80]{};
    ::strncpy(upperName, fileName, sizeof(upperName) - 1);
    for (size_t i = 0; upperName[i] != '\0'; ++i)
    {
        if (upperName[i] >= 'a' && upperName[i] <= 'z')
        {
            upperName[i] = static_cast<char>(upperName[i] - 'a' + 'A');
        }
    }

    // ISO9660 Level-1 fallback alias (8.3): e.g. ASFALTO_8.TGA -> ASFALTO_.TGA
    char iso83[80]{};
    {
        const char* dot = ::strrchr(upperName, '.');
        if (dot)
        {
            const size_t baseLen = static_cast<size_t>(dot - upperName);
            const char* ext = dot + 1;
            char base[16]{};
            size_t n = baseLen;
            if (n > 8) n = 8;
            for (size_t i = 0; i < n; ++i) base[i] = upperName[i];
            base[n] = '\0';

            char ext3[8]{};
            size_t e = 0;
            while (ext[e] != '\0' && e < 3) { ext3[e] = ext[e]; ++e; }
            ext3[e] = '\0';

            if (base[0] != '\0' && ext3[0] != '\0')
            {
                std::snprintf(iso83, sizeof(iso83), "%s.%s", base, ext3);
            }
        }
    }

    char p0[96]{}, p1[96]{}, p2[96]{}, p3[96]{}, p4[80]{}, p5[80]{}, p6[96]{}, p7[96]{};
    char u0[96]{}, u1[96]{}, u2[96]{}, u3[96]{}, u4[80]{}, u5[80]{}, u6[96]{}, u7[96]{};
    std::snprintf(p0, sizeof(p0), "CD/DATA/%s", fileName);
    std::snprintf(p1, sizeof(p1), "CD/DATA/%s;1", fileName);
    std::snprintf(p2, sizeof(p2), "DATA/%s", fileName);
    std::snprintf(p3, sizeof(p3), "DATA/%s;1", fileName);
    std::snprintf(p4, sizeof(p4), "%s", fileName);
    std::snprintf(p5, sizeof(p5), "%s;1", fileName);
    std::snprintf(p6, sizeof(p6), "data/%s", fileName);
    std::snprintf(p7, sizeof(p7), "data/%s;1", fileName);
    std::snprintf(u0, sizeof(u0), "CD/DATA/%s", upperName);
    std::snprintf(u1, sizeof(u1), "CD/DATA/%s;1", upperName);
    std::snprintf(u2, sizeof(u2), "DATA/%s", upperName);
    std::snprintf(u3, sizeof(u3), "DATA/%s;1", upperName);
    std::snprintf(u4, sizeof(u4), "%s", upperName);
    std::snprintf(u5, sizeof(u5), "%s;1", upperName);
    std::snprintf(u6, sizeof(u6), "data/%s", upperName);
    std::snprintf(u7, sizeof(u7), "data/%s;1", upperName);

    char i0[96]{}, i1[96]{}, i2[96]{}, i3[96]{}, i4[80]{}, i5[80]{};
    if (iso83[0] != '\0')
    {
        std::snprintf(i0, sizeof(i0), "CD/DATA/%s", iso83);
        std::snprintf(i1, sizeof(i1), "CD/DATA/%s;1", iso83);
        std::snprintf(i2, sizeof(i2), "DATA/%s", iso83);
        std::snprintf(i3, sizeof(i3), "DATA/%s;1", iso83);
        std::snprintf(i4, sizeof(i4), "%s", iso83);
        std::snprintf(i5, sizeof(i5), "%s;1", iso83);
    }

    const char* cands[] = { p0, p1, p2, p3, p4, p5, p6, p7, u0, u1, u2, u3, u4, u5, u6, u7, i0, i1, i2, i3, i4, i5 };
    for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]); ++i)
    {
        ::strncpy(sLastTriedTexPath, cands[i], sizeof(sLastTriedTexPath) - 1);
        sLastTriedTexPath[sizeof(sLastTriedTexPath) - 1] = '\0';
        sLastTriedTexOpen = 0;
        SRL::Cd::File f(cands[i]);
        if (!f.Exists() || f.Size.Bytes <= 0) continue;
        if (!f.Open()) continue;
        sLastTriedTexOpen = 1;
        SRL::Bitmap::TGA bmp(&f);
        const int32_t slot = SRL::VDP1::TryLoadTexture((SRL::Bitmap::IBitmap*)&bmp);
        if (slot > 0) return slot;
    }

    // Probe version suffixes ;1..;31 (some ISO builds can expose higher versions).
    auto tryVersionRange = [&](const char* base) -> int32_t
    {
        if (!base || base[0] == '\0') return -1;
        for (int v = 1; v <= 31; ++v)
        {
            char n[96]{};
            std::snprintf(n, sizeof(n), "%s;%d", base, v);
            ::strncpy(sLastTriedTexPath, n, sizeof(sLastTriedTexPath) - 1);
            sLastTriedTexPath[sizeof(sLastTriedTexPath) - 1] = '\0';
            sLastTriedTexOpen = 0;
            SRL::Cd::File f(n);
            if (!f.Exists() || f.Size.Bytes <= 0) continue;
            if (!f.Open()) continue;
            sLastTriedTexOpen = 1;
            SRL::Bitmap::TGA bmp(&f);
            const int32_t slot = SRL::VDP1::TryLoadTexture((SRL::Bitmap::IBitmap*)&bmp);
            if (slot > 0) return slot;
        }
        return -1;
    };
    {
        const char* bases[] = { p0, p2, p4, p6, u0, u2, u4, u6, i0, i2, i4 };
        for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i)
        {
            const int32_t s = tryVersionRange(bases[i]);
            if (s > 0) return s;
        }
    }

    // Fallback: some CD setups only resolve by current directory.
    const char* dir1[] = { "DATA", "data", nullptr };
    const char* names[] = { fileName, upperName };
    for (size_t d = 0; d < sizeof(dir1) / sizeof(dir1[0]); ++d)
    {
        SRL::Cd::ChangeDir((const char*)0);
        if (dir1[d]) SRL::Cd::ChangeDir(dir1[d]);
        for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); ++n)
        {
            if (!names[n] || names[n][0] == '\0') continue;
            char n0[80]{}, n1[80]{};
            std::snprintf(n0, sizeof(n0), "%s", names[n]);
            std::snprintf(n1, sizeof(n1), "%s;1", names[n]);
            const char* nc[] = { n0, n1 };
            for (size_t i = 0; i < sizeof(nc) / sizeof(nc[0]); ++i)
            {
                ::strncpy(sLastTriedTexPath, nc[i], sizeof(sLastTriedTexPath) - 1);
                sLastTriedTexPath[sizeof(sLastTriedTexPath) - 1] = '\0';
                sLastTriedTexOpen = 0;
                SRL::Cd::File f(nc[i]);
                if (!f.Exists() || f.Size.Bytes <= 0) continue;
                if (!f.Open()) continue;
                sLastTriedTexOpen = 1;
                SRL::Bitmap::TGA bmp(&f);
                const int32_t slot = SRL::VDP1::TryLoadTexture((SRL::Bitmap::IBitmap*)&bmp);
                if (slot > 0)
                {
                    SRL::Cd::ChangeDir((const char*)0);
                    return slot;
                }
            }
        }
    }
    SRL::Cd::ChangeDir((const char*)0);
    SRL::Debug::Print(1, 19, "TXf op:%u p:%s", (unsigned)sLastTriedTexOpen, sLastTriedTexPath);
    return -1;
}

static uint32_t TextureByteSize(uint16_t width, uint16_t height, SRL::CRAM::TextureColorMode colorMode)
{
    uint16_t sh = 0;
    switch (colorMode)
    {
    case SRL::CRAM::TextureColorMode::Paletted256:
    case SRL::CRAM::TextureColorMode::Paletted128:
    case SRL::CRAM::TextureColorMode::Paletted64:
        sh = 1;
        break;
    case SRL::CRAM::TextureColorMode::Paletted16:
        sh = 2;
        break;
    default:
        sh = 0;
        break;
    }
    return static_cast<uint32_t>(((static_cast<uint32_t>(width) * static_cast<uint32_t>(height)) << 1) >> sh);
}

static bool TryOverwriteTextureSlotFromCd(int32_t slot, const char* fileName)
{
    if (slot <= 0 || slot >= SRL_MAX_TEXTURES) return false;
    auto* tex = SRL::VDP1::Metadata[slot].Texture;
    if (!tex) return false;

    const char* dirs[][2] = {
        { "DATA", nullptr },
        { "data", nullptr },
        { nullptr, nullptr }
    };
    for (const auto& d : dirs)
    {
        SRL::Cd::ChangeDir((const char*)0);
        if (d[0]) SRL::Cd::ChangeDir(d[0]);
        if (d[1]) SRL::Cd::ChangeDir(d[1]);

        SRL::Cd::File f(fileName);
        if (!f.Exists() || f.Size.Bytes <= 0) continue;
        SRL::Bitmap::TGA bmp(&f);
        const SRL::Bitmap::BitmapInfo info = bmp.GetInfo();
        const auto expectedMode = SRL::VDP1::Metadata[slot].ColorMode;
        if (static_cast<uint16_t>(info.Width) != tex->Width || static_cast<uint16_t>(info.Height) != tex->Height)
        {
            SRL::Cd::ChangeDir((const char*)0);
            return false;
        }
        if (static_cast<SRL::CRAM::TextureColorMode>(info.ColorMode) != expectedMode)
        {
            SRL::Cd::ChangeDir((const char*)0);
            return false;
        }
        uint8_t* dst = SRL::VDP1::Metadata[slot].GetData();
        if (!dst || !bmp.GetData())
        {
            SRL::Cd::ChangeDir((const char*)0);
            return false;
        }
        const uint32_t bytes = TextureByteSize(tex->Width, tex->Height, expectedMode);
        slDMACopy(bmp.GetData(), dst, bytes);
        SRL::Cd::ChangeDir((const char*)0);
        return true;
    }
    SRL::Cd::ChangeDir((const char*)0);
    return false;
}

static int FindFamilyIndex(const Segment1TextureJson& map1, int familyId)
{
    for (size_t i = 0; i < map1.familyCount; ++i)
    {
        if (map1.familyIds[i] == familyId) return static_cast<int>(i);
    }
    return -1;
}

template <typename FaceFamilyVecT>
static size_t BuildUniqueUsedFamilies(const FaceFamilyVecT& faceFamily, int* outIds, size_t outCap)
{
    if (!outIds || outCap == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < faceFamily.size(); ++i)
    {
        const int fam = faceFamily[i];
        if (fam < 0) continue;
        bool exists = false;
        for (size_t j = 0; j < count; ++j)
        {
            if (outIds[j] == fam) { exists = true; break; }
        }
        if (!exists && count < outCap)
        {
            outIds[count++] = fam;
        }
    }
    return count;
}

static bool ContainsFamily(const int* ids, size_t count, int familyId)
{
    if (!ids) return false;
    for (size_t i = 0; i < count; ++i)
    {
        if (ids[i] == familyId) return true;
    }
    return false;
}

static bool ParseSegment1TextureJson(const char* json, Segment1TextureJson& out)
{
    out.faceFamily.clear();
    out.familyCount = 0;
    if (!json || json[0] == '\0') return false;
    (void)ParseTextureFamilies64(json, out);
    const bool facesOk = ParseFaceFamilyArrayForSegment1(json, out);
    return facesOk;
}

static size_t CollectAllVariantTextureNames(const char* json, char outNames[][64], size_t outCap)
{
    if (!json || !outNames || outCap == 0) return 0;
    auto isTgaNameChar = [](char c) -> bool
    {
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               c == '_' || c == '-' || c == '.';
    };
    auto nameEqualsIgnoreCase = [](const char* a, const char* b) -> bool
    {
        if (!a || !b) return false;
        while (*a && *b)
        {
            char ca = *a;
            char cb = *b;
            if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 'a' + 'A');
            if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
            if (ca != cb) return false;
            ++a; ++b;
        }
        return (*a == '\0' && *b == '\0');
    };

    size_t count = 0;
    const char* p = json;
    while (p && *p)
    {
        const char* v = strstr(p, "\"variants\"");
        if (!v) break;
        const char* obj0 = strchr(v, '{');
        if (!obj0) { p = v + 10; continue; }
        const char* obj1 = strchr(obj0, '}');
        if (!obj1) { p = obj0 + 1; continue; }

        const char* q = obj0;
        while (q && q < obj1)
        {
            const char* tga = strstr(q, ".TGA");
            if (!tga || tga >= obj1) break;
            const char* b = tga;
            while (b > obj0 && isTgaNameChar(*(b - 1))) --b;
            char name[64]{};
            size_t n = static_cast<size_t>((tga - b) + 4);
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            memcpy(name, b, n);
            name[n] = '\0';

            bool exists = false;
            for (size_t i = 0; i < count; ++i)
            {
                if (nameEqualsIgnoreCase(outNames[i], name)) { exists = true; break; }
            }
            if (!exists && count < outCap)
            {
                ::strncpy(outNames[count], name, 63);
                outNames[count][63] = '\0';
                ++count;
            }
            q = tga + 4;
        }
        p = obj1 + 1;
    }
    return count;
}

static uint16_t ReadLe16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

static uint32_t ReadLe32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

struct TrackedPaletteBankState
{
    std::array<uint8_t, 128> pal16{};
    std::array<uint8_t, 32> pal64{};
    std::array<uint8_t, 16> pal128{};
    std::array<uint8_t, 8> pal256{};
};

static TrackedPaletteBankState g_trackPaletteBanks{};
static std::vector<uint16_t> g_trackReusableTextureSlots{};
static std::array<uint8_t, SRL_MAX_TEXTURES> g_trackReusableTextureSlotFlags{};

static void ResetReusableTrackTextureSlots()
{
    g_trackReusableTextureSlots.clear();
    g_trackReusableTextureSlotFlags.fill(0u);
}

static void RemoveReusableTrackTextureSlotAt(size_t index)
{
    if (index >= g_trackReusableTextureSlots.size()) return;
    const uint16_t slot = g_trackReusableTextureSlots[index];
    if (slot < g_trackReusableTextureSlotFlags.size())
    {
        g_trackReusableTextureSlotFlags[slot] = 0u;
    }
    if (index + 1u < g_trackReusableTextureSlots.size())
    {
        g_trackReusableTextureSlots[index] = g_trackReusableTextureSlots.back();
    }
    g_trackReusableTextureSlots.pop_back();
}

static void QueueReusableTrackTextureSlot(uint16_t slot)
{
    if (slot == No_Texture || slot == 0u) return;
    if (slot >= SRL_MAX_TEXTURES) return;
    if (!IsVdp1TextureSlotLive(slot)) return;
    if (g_trackReusableTextureSlotFlags[slot] != 0u) return;
    g_trackReusableTextureSlotFlags[slot] = 1u;
    g_trackReusableTextureSlots.push_back(slot);
}

static uint16_t CountReusableTrackTextureSlots()
{
    return static_cast<uint16_t>(std::min<size_t>(
        g_trackReusableTextureSlots.size(),
        static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
}

static void MarkTrackPaletteBank(SRL::CRAM::TextureColorMode mode, uint16_t bank)
{
    switch (mode)
    {
    case SRL::CRAM::TextureColorMode::Paletted16:
        if (bank < g_trackPaletteBanks.pal16.size()) g_trackPaletteBanks.pal16[bank] = 1;
        break;
    case SRL::CRAM::TextureColorMode::Paletted64:
        if (bank < g_trackPaletteBanks.pal64.size()) g_trackPaletteBanks.pal64[bank] = 1;
        break;
    case SRL::CRAM::TextureColorMode::Paletted128:
        if (bank < g_trackPaletteBanks.pal128.size()) g_trackPaletteBanks.pal128[bank] = 1;
        break;
    case SRL::CRAM::TextureColorMode::Paletted256:
        if (bank < g_trackPaletteBanks.pal256.size()) g_trackPaletteBanks.pal256[bank] = 1;
        break;
    default:
        break;
    }
}

static void ReleaseTrackedPaletteBanks()
{
    for (uint16_t bank = 0; bank < g_trackPaletteBanks.pal16.size(); ++bank)
    {
        if (!g_trackPaletteBanks.pal16[bank]) continue;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted16, false);
        g_trackPaletteBanks.pal16[bank] = 0;
    }
    for (uint16_t bank = 0; bank < g_trackPaletteBanks.pal64.size(); ++bank)
    {
        if (!g_trackPaletteBanks.pal64[bank]) continue;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted64, false);
        g_trackPaletteBanks.pal64[bank] = 0;
    }
    for (uint16_t bank = 0; bank < g_trackPaletteBanks.pal128.size(); ++bank)
    {
        if (!g_trackPaletteBanks.pal128[bank]) continue;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted128, false);
        g_trackPaletteBanks.pal128[bank] = 0;
    }
    for (uint16_t bank = 0; bank < g_trackPaletteBanks.pal256.size(); ++bank)
    {
        if (!g_trackPaletteBanks.pal256[bank]) continue;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted256, false);
        g_trackPaletteBanks.pal256[bank] = 0;
    }
}

static void ReleaseTrackedPaletteBankForSlot(uint16_t slot)
{
    if (!IsVdp1TextureSlotLive(slot)) return;

    const auto& meta = SRL::VDP1::Metadata[slot];
    const uint16_t bank = static_cast<uint16_t>(meta.PaletteId);
    switch (meta.ColorMode)
    {
    case SRL::CRAM::TextureColorMode::Paletted16:
        if (bank < g_trackPaletteBanks.pal16.size()) g_trackPaletteBanks.pal16[bank] = 0;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted16, false);
        break;
    case SRL::CRAM::TextureColorMode::Paletted64:
        if (bank < g_trackPaletteBanks.pal64.size()) g_trackPaletteBanks.pal64[bank] = 0;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted64, false);
        break;
    case SRL::CRAM::TextureColorMode::Paletted128:
        if (bank < g_trackPaletteBanks.pal128.size()) g_trackPaletteBanks.pal128[bank] = 0;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted128, false);
        break;
    case SRL::CRAM::TextureColorMode::Paletted256:
        if (bank < g_trackPaletteBanks.pal256.size()) g_trackPaletteBanks.pal256[bank] = 0;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted256, false);
        break;
    default:
        break;
    }
}

static void ReleaseTrackedPaletteBankById(SRL::CRAM::TextureColorMode mode, uint16_t bank)
{
    switch (mode)
    {
    case SRL::CRAM::TextureColorMode::Paletted16:
        if (bank < g_trackPaletteBanks.pal16.size()) g_trackPaletteBanks.pal16[bank] = 0;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted16, false);
        break;
    case SRL::CRAM::TextureColorMode::Paletted64:
        if (bank < g_trackPaletteBanks.pal64.size()) g_trackPaletteBanks.pal64[bank] = 0;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted64, false);
        break;
    case SRL::CRAM::TextureColorMode::Paletted128:
        if (bank < g_trackPaletteBanks.pal128.size()) g_trackPaletteBanks.pal128[bank] = 0;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted128, false);
        break;
    case SRL::CRAM::TextureColorMode::Paletted256:
        if (bank < g_trackPaletteBanks.pal256.size()) g_trackPaletteBanks.pal256[bank] = 0;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted256, false);
        break;
    default:
        break;
    }
}

template <size_t N>
static uint16_t CountTrackedBanks(const std::array<uint8_t, N>& banks)
{
    uint16_t count = 0;
    for (size_t i = 0; i < banks.size(); ++i)
    {
        if (banks[i] != 0u) ++count;
    }
    return count;
}

static int32_t AllocatePaletteBankForMode(SRL::CRAM::TextureColorMode mode)
{
    if (mode == SRL::CRAM::TextureColorMode::RGB555) return 0;
    uint16_t start = 0;
    uint16_t limit = 0;
    switch (mode)
    {
    case SRL::CRAM::TextureColorMode::Paletted256: start = 1; limit = 8; break;
    case SRL::CRAM::TextureColorMode::Paletted128: start = 2; limit = 16; break;
    case SRL::CRAM::TextureColorMode::Paletted64:  start = 4; limit = 32; break;
    case SRL::CRAM::TextureColorMode::Paletted16:  start = 16; limit = 128; break;
    default: return -1;
    }
    for (uint16_t bank = start; bank < limit; ++bank)
    {
        if (!SRL::CRAM::GetBankUsedState(bank, mode))
        {
            SRL::CRAM::SetBankUsedState(bank, mode, true);
            MarkTrackPaletteBank(mode, bank);
            return static_cast<int32_t>(bank);
        }
    }
    return -1;
}

struct DecodedTgaTexture
{
    uint16_t width = 0;
    uint16_t height = 0;
    SRL::CRAM::TextureColorMode mode = SRL::CRAM::TextureColorMode::RGB555;
    std::vector<SRL::Types::HighColor> palette{};
    std::vector<uint8_t> pixels{};
};

// Compact paletted TGA data down to the indices that are actually used.
static size_t CompactUsedPalette(const uint8_t* srcPixels,
                                 size_t pixelCount,
                                 const std::vector<SRL::Types::HighColor>& srcPalette,
                                 std::vector<SRL::Types::HighColor>& outPalette,
                                 std::vector<uint8_t>& outIndices)
{
    outPalette.clear();
    outIndices.clear();
    if (!srcPixels || pixelCount == 0 || srcPalette.empty()) return 0;

    std::array<int16_t, 256> remap{};
    remap.fill(-1);

    if (srcPalette.size() > 0)
    {
        outPalette.push_back(srcPalette[0]);
        remap[0] = 0;
    }

    for (size_t i = 0; i < pixelCount; ++i)
    {
        const uint8_t idx = srcPixels[i];
        if (idx >= srcPalette.size()) return 0;
        if (remap[idx] >= 0) continue;
        if (outPalette.size() >= 256) return 0;
        remap[idx] = static_cast<int16_t>(outPalette.size());
        outPalette.push_back(srcPalette[idx]);
    }

    outIndices.resize(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const uint8_t idx = srcPixels[i];
        const int16_t mapped = remap[idx];
        if (mapped < 0 || mapped > 255) return 0;
        outIndices[i] = static_cast<uint8_t>(mapped);
    }

    return outPalette.size();
}

static bool DecodePalettedTgaMemory(const uint8_t* data, size_t size, DecodedTgaTexture& out)
{
    out = {};
    if (!data || size < 18) return false;
    const uint8_t idLen = data[0];
    const uint8_t colorMapType = data[1];
    const uint8_t imageType = data[2];
    out.width = ReadLe16(data + 12);
    out.height = ReadLe16(data + 14);
    const uint8_t pixelDepth = data[16];
    if (out.width == 0 || out.height == 0) return false;

    size_t off = 18 + static_cast<size_t>(idLen);
    if (colorMapType == 1 && imageType == 1)
    {
        const uint16_t cmapFirst = ReadLe16(data + 3);
        const uint16_t cmapLen = ReadLe16(data + 5);
        const uint8_t cmapDepth = data[7];
        (void)cmapFirst;
        if (pixelDepth != 8) return false;
        if (cmapLen == 0 || cmapLen > 256) return false;
        if (!(cmapDepth == 24 || cmapDepth == 32 || cmapDepth == 16)) return false;

        const size_t cmapBytes = static_cast<size_t>(cmapLen) * static_cast<size_t>(cmapDepth / 8);
        if (off + cmapBytes > size) return false;

        std::vector<SRL::Types::HighColor> srcPalette{};
        srcPalette.resize(cmapLen);
        for (size_t i = 0; i < cmapLen; ++i)
        {
            const uint8_t* c = data + off + i * (cmapDepth / 8);
            SRL::Types::HighColor hc{};
            if (cmapDepth == 24)
            {
                const uint32_t rgb = (static_cast<uint32_t>(c[2]) << 16) |
                                     (static_cast<uint32_t>(c[1]) << 8) |
                                     static_cast<uint32_t>(c[0]);
                hc = SRL::Types::HighColor::FromRGB24(rgb);
            }
            else if (cmapDepth == 32)
            {
                const uint32_t argb = (static_cast<uint32_t>(c[3]) << 24) |
                                      (static_cast<uint32_t>(c[2]) << 16) |
                                      (static_cast<uint32_t>(c[1]) << 8) |
                                      static_cast<uint32_t>(c[0]);
                hc = SRL::Types::HighColor::FromARGB32(argb);
            }
            else
            {
                hc = SRL::Types::HighColor::FromARGB15(ReadLe16(c));
            }
            srcPalette[i] = hc;
        }
        off += cmapBytes;

        const size_t srcPixels = static_cast<size_t>(out.width) * static_cast<size_t>(out.height);
        if (off + srcPixels > size) return false;
        const uint8_t* src = data + off;

        std::vector<SRL::Types::HighColor> compactPalette{};
        std::vector<uint8_t> compactIndices{};
        size_t usedPaletteCount = CompactUsedPalette(src, srcPixels, srcPalette, compactPalette, compactIndices);
        if (usedPaletteCount == 0)
        {
            compactPalette = srcPalette;
            compactIndices.assign(src, src + srcPixels);
            usedPaletteCount = compactPalette.size();
        }

        out.palette = compactPalette;

        if (usedPaletteCount <= 16)
        {
            out.mode = SRL::CRAM::TextureColorMode::Paletted16;
            out.pixels.resize((srcPixels + 1) / 2);
            for (size_t i = 0; i + 1 < srcPixels; i += 2)
            {
                out.pixels[i / 2] = static_cast<uint8_t>(((compactIndices[i] & 0x0F) << 4) | (compactIndices[i + 1] & 0x0F));
            }
            if ((srcPixels & 1u) != 0u)
            {
                out.pixels[srcPixels / 2] = static_cast<uint8_t>((compactIndices[srcPixels - 1] & 0x0F) << 4);
            }
        }
        else if (usedPaletteCount <= 64)
        {
            out.mode = SRL::CRAM::TextureColorMode::Paletted64;
            out.pixels = compactIndices;
        }
        else if (usedPaletteCount <= 128)
        {
            out.mode = SRL::CRAM::TextureColorMode::Paletted128;
            out.pixels = compactIndices;
        }
        else
        {
            out.mode = SRL::CRAM::TextureColorMode::Paletted256;
            out.pixels = compactIndices;
        }
        return true;
    }

    if (colorMapType == 0 && imageType == 2)
    {
        const size_t bpp = static_cast<size_t>(pixelDepth / 8);
        if (!(pixelDepth == 16 || pixelDepth == 24 || pixelDepth == 32)) return false;
        const size_t srcBytes = static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * bpp;
        if (off + srcBytes > size) return false;

        out.mode = SRL::CRAM::TextureColorMode::RGB555;
        out.palette.clear();
        out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 2);

        const uint8_t* src = data + off;
        uint8_t* dst = out.pixels.data();
        const size_t pixelCount = static_cast<size_t>(out.width) * static_cast<size_t>(out.height);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const uint8_t* px = src + (i * bpp);
            uint16_t c16 = 0;
            if (pixelDepth == 16)
            {
                c16 = ReadLe16(px);
            }
            else
            {
                const uint8_t b = px[0];
                const uint8_t g = px[1];
                const uint8_t r = px[2];
                const uint8_t r5 = static_cast<uint8_t>(r >> 3);
                const uint8_t g5 = static_cast<uint8_t>(g >> 3);
                const uint8_t b5 = static_cast<uint8_t>(b >> 3);
                c16 = static_cast<uint16_t>(0x8000u | (static_cast<uint16_t>(r5) << 10) |
                                            (static_cast<uint16_t>(g5) << 5) |
                                            static_cast<uint16_t>(b5));
            }
            dst[i * 2 + 0] = static_cast<uint8_t>(c16 & 0xFF);
            dst[i * 2 + 1] = static_cast<uint8_t>((c16 >> 8) & 0xFF);
        }
        return true;
    }

    return false;
}

static int32_t UploadDecodedTextureToVdp1(const DecodedTgaTexture& tex)
{
    // SGL uses texture slot 0 as No_Texture.
    // Reserve slot 0 with a dummy texture before the first dynamic upload so
    // all real runtime textures start at slot 1.
    if (SRL::VDP1::GetTextureCount() == 0)
    {
        static uint16_t sDummyTexture[8 * 8]{};
        (void)SRL::VDP1::TryLoadTexture(
            8,
            8,
            SRL::CRAM::TextureColorMode::RGB555,
            0,
            sDummyTexture);
    }

    auto tryReuseQueuedSlot = [&]() -> int32_t
    {
        if (g_trackReusableTextureSlots.empty()) return -1;

        const uint32_t requiredBytes = TextureByteSize(tex.width, tex.height, tex.mode);

        for (size_t i = 0; i < g_trackReusableTextureSlots.size();)
        {
            const uint16_t slot = g_trackReusableTextureSlots[i];
            if (!IsVdp1TextureSlotLive(slot))
            {
                RemoveReusableTrackTextureSlotAt(i);
                continue;
            }

            auto& meta = SRL::VDP1::Metadata[slot];
            auto* liveTex = meta.Texture;
            if (!liveTex)
            {
                RemoveReusableTrackTextureSlotAt(i);
                continue;
            }

            const uint32_t allocatedBytes = TextureByteSize(liveTex->Width, liveTex->Height, meta.ColorMode);
            if (allocatedBytes < requiredBytes)
            {
                ++i;
                continue;
            }

            uint16_t paletteId = 0;
            if (tex.mode != SRL::CRAM::TextureColorMode::RGB555)
            {
                const int32_t bankId = AllocatePaletteBankForMode(tex.mode);
                if (bankId < 0) return -1;
                paletteId = static_cast<uint16_t>(bankId);
                SRL::CRAM::Palette cramPalette(tex.mode, paletteId);
                if (!tex.palette.empty())
                {
                    cramPalette.Load(const_cast<SRL::Types::HighColor*>(tex.palette.data()),
                                     static_cast<int16_t>(tex.palette.size()));
                }
            }

            const uint16_t address = liveTex->Address;
            *liveTex = SRL::VDP1::Texture(tex.width, tex.height, address);
            meta.ColorMode = tex.mode;
            meta.PaletteId = paletteId;
            slDMACopy(const_cast<uint8_t*>(tex.pixels.data()),
                      meta.GetData(),
                      requiredBytes);
            RemoveReusableTrackTextureSlotAt(i);
            return static_cast<int32_t>(slot);
        }

        return -1;
    };

    const int32_t reusedSlot = tryReuseQueuedSlot();
    if (reusedSlot >= 0) return reusedSlot;

    uint16_t paletteId = 0;
    if (tex.mode != SRL::CRAM::TextureColorMode::RGB555)
    {
        const int32_t bankId = AllocatePaletteBankForMode(tex.mode);
        if (bankId < 0) return -1;
        paletteId = static_cast<uint16_t>(bankId);
        SRL::CRAM::Palette cramPalette(tex.mode, paletteId);
        if (!tex.palette.empty())
        {
            cramPalette.Load(const_cast<SRL::Types::HighColor*>(tex.palette.data()),
                             static_cast<int16_t>(tex.palette.size()));
        }
    }
    const int32_t slot = SRL::VDP1::TryLoadTexture(tex.width, tex.height, tex.mode, paletteId, const_cast<uint8_t*>(tex.pixels.data()));
    if (slot < 0 && tex.mode != SRL::CRAM::TextureColorMode::RGB555)
    {
        ReleaseTrackedPaletteBankById(tex.mode, paletteId);
    }
    return slot;
}

static bool LoadMat8ForSegment(int segmentId, SegmentComponent::Blob& outBlob, SegmentComponent::Loader::MatView& outView)
{
    static PackedAssetCache sMat8PackCache{};
    char packedName[20]{};
    std::snprintf(packedName, sizeof(packedName), "S%03dM8.MAT", segmentId);
    const char* packCandidates[] = {
        "CD/DATA/MAT8.BIN",
        "CD/DATA/MAT8.BIN;1",
        "DATA/MAT8.BIN",
        "DATA/MAT8.BIN;1",
        "MAT8.BIN",
        "MAT8.BIN;1"
    };
    if (LoadPackedAssetIndexToCart(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]), sMat8PackCache) &&
        LoadPackedAssetEntryToBlob(sMat8PackCache, packedName, outBlob))
    {
        return SegmentComponent::Loader::ParseMat(outBlob, outView);
    }
    (void)segmentId;
    outBlob = {};
    outView = {};
    return false;
}

static bool LoadSdrForSegment(int segmentId, SegmentDrawReady::Blob& outBlob, SegmentDrawReady::Loader::View& outView)
{
static PackedAssetCache sSdrPackCache{};
    static char sSdrPinnedPath[96]{};
    static size_t sSdrTrustedEntries = 0;
    static char sSdrBestPath[96]{};
    static size_t sSdrBestEntries = 0;
    static std::array<int32_t, 4097> sSdrEntryById{};
    static bool sSdrEntryByIdBuilt = false;
    static size_t sSdrEntryByIdCount = 0;
    static char sSdrEntryByIdSource[96]{};
    char packedName[16]{};
    std::snprintf(packedName, sizeof(packedName), "S%03d.SDR", segmentId);
    const char* packCandidates[] = {
        "/CD/DATA/SDR.BIN",
        "/CD/DATA/SDR.BIN;1",
        "/DATA/SDR.BIN",
        "/DATA/SDR.BIN;1",
        "CD/DATA/SDR.BIN",
        "CD/DATA/SDR.BIN;1",
        "DATA/SDR.BIN",
        "DATA/SDR.BIN;1",
        "cd/data/SDR.BIN",
        "cd/data/SDR.BIN;1",
        "cd/data/sdr.bin",
        "cd/data/sdr.bin;1",
        "data/SDR.BIN",
        "data/SDR.BIN;1",
        "data/sdr.bin",
        "data/sdr.bin;1",
        "SDR.BIN",
        "SDR.BIN;1",
        "sdr.bin",
        "sdr.bin;1"
    };

    auto indexAcceptable = [&](size_t entryCount) -> bool
    {
        if (entryCount == 0) return false;
        if (segmentId > 0 && entryCount < static_cast<size_t>(segmentId)) return false;
        // Never downgrade to a smaller SDR index once a larger trusted source
        // has been observed, otherwise runtime sliding can stall on higher ids.
        if (sSdrTrustedEntries > 0 && entryCount < sSdrTrustedEntries) return false;
        return true;
    };

    auto parseSdrNameToId = [&](const char* name, int32_t& outId) -> bool
    {
        outId = -1;
        if (!name) return false;
        if (name[0] != 'S' && name[0] != 's') return false;
        if (name[1] < '0' || name[1] > '9') return false;
        if (name[2] < '0' || name[2] > '9') return false;
        if (name[3] < '0' || name[3] > '9') return false;
        if (name[4] != '.') return false;
        outId = (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');
        return outId > 0;
    };

    auto rebuildSdrEntryLookup = [&]()
    {
        for (size_t i = 0; i < sSdrEntryById.size(); ++i) sSdrEntryById[i] = -1;
        for (size_t i = 0; i < sSdrPackCache.entries.size(); ++i)
        {
            int32_t id = -1;
            if (!parseSdrNameToId(sSdrPackCache.entries[i].name, id)) continue;
            if (id <= 0 || static_cast<size_t>(id) >= sSdrEntryById.size()) continue;
            if (sSdrEntryById[static_cast<size_t>(id)] < 0)
            {
                sSdrEntryById[static_cast<size_t>(id)] = static_cast<int32_t>(i);
            }
        }
        sSdrEntryByIdBuilt = true;
        sSdrEntryByIdCount = sSdrPackCache.entries.size();
        if (sSdrPackCache.sourcePath[0] != '\0')
        {
            ::strncpy(sSdrEntryByIdSource, sSdrPackCache.sourcePath, sizeof(sSdrEntryByIdSource) - 1);
            sSdrEntryByIdSource[sizeof(sSdrEntryByIdSource) - 1] = '\0';
        }
        else
        {
            sSdrEntryByIdSource[0] = '\0';
        }
    };

    auto tryLoadSdrBySegmentId = [&]() -> bool
    {
        if (segmentId <= 0) return false;
        if (static_cast<size_t>(segmentId) >= sSdrEntryById.size()) return false;
        const bool sourceChanged = (::strcmp(sSdrEntryByIdSource, sSdrPackCache.sourcePath) != 0);
        if (!sSdrEntryByIdBuilt || sSdrEntryByIdCount != sSdrPackCache.entries.size() || sourceChanged)
        {
            rebuildSdrEntryLookup();
        }

        const int32_t idx = sSdrEntryById[static_cast<size_t>(segmentId)];
        if (idx < 0) return false;
        const size_t uidx = static_cast<size_t>(idx);
        if (uidx >= sSdrPackCache.entries.size()) return false;
        const auto& e = sSdrPackCache.entries[uidx];
        if (e.size == 0) return false;
        if (static_cast<uint64_t>(e.offset) + static_cast<uint64_t>(e.size) > static_cast<uint64_t>(sSdrPackCache.size))
        {
            return false;
        }

        const uint8_t* src = static_cast<const uint8_t*>(sSdrPackCache.cartPtr) + e.offset;
        outBlob.bytes.resize(e.size);
        ::memcpy(outBlob.bytes.data(), src, e.size);
        if (outBlob.bytes.size() < sizeof(SegmentDrawReady::HeaderV1)) return false;
        if (ReadLe32(outBlob.bytes.data()) != SegmentDrawReady::kMagicSdr1) return false;
        if (ReadLe16(outBlob.bytes.data() + 4) != SegmentDrawReady::kVersion1) return false;
        outBlob.loaded = true;
        outBlob.size = outBlob.bytes.size();
        return true;
    };

    auto rememberTrustedSource = [&]()
    {
        const size_t entryCount = sSdrPackCache.entries.size();
        bool shouldPin = false;
        if (entryCount > sSdrTrustedEntries)
        {
            sSdrTrustedEntries = entryCount;
            shouldPin = true;
        }
        else if (sSdrPinnedPath[0] == '\0' && entryCount > 0)
        {
            // Bootstrap pin when no trusted source has been recorded yet.
            shouldPin = true;
        }
        else if (entryCount == sSdrTrustedEntries && entryCount > 0)
        {
            // Keep pin synchronized only for equal-capacity trusted indexes.
            shouldPin = true;
        }

        if (shouldPin && sSdrPackCache.sourcePath[0] != '\0')
        {
            ::strncpy(sSdrPinnedPath, sSdrPackCache.sourcePath, sizeof(sSdrPinnedPath) - 1);
            sSdrPinnedPath[sizeof(sSdrPinnedPath) - 1] = '\0';
        }
        if (entryCount >= sSdrBestEntries && sSdrPackCache.sourcePath[0] != '\0')
        {
            sSdrBestEntries = entryCount;
            ::strncpy(sSdrBestPath, sSdrPackCache.sourcePath, sizeof(sSdrBestPath) - 1);
            sSdrBestPath[sizeof(sSdrBestPath) - 1] = '\0';
        }
    };

    auto probePackedEntryCount = [&](const char* path, size_t& outEntryCount) -> bool
    {
        outEntryCount = 0;
        if (!path || path[0] == '\0') return false;
        SRL::Cd::File f(path);
        if (!f.Exists() || f.Size.Bytes < 12) return false;
        if (!f.Open()) return false;

        uint8_t hdr[12]{};
        const int32_t got = f.Read(12, hdr);
        if (got < 12) return false;

        const uint32_t magic = ReadLe32(hdr + 0);
        const uint32_t version = ReadLe32(hdr + 4);
        const uint32_t entryCount = ReadLe32(hdr + 8);
        if (magic != 0x314B4150 || version != 1u || entryCount == 0u) return false;
        outEntryCount = static_cast<size_t>(entryCount);
        return true;
    };

    auto loadBestCandidate = [&](const char* const* candidates, size_t count) -> bool
    {
        if (sSdrPackCache.cartPtr && sSdrPackCache.size > 0 &&
            indexAcceptable(sSdrPackCache.entries.size()))
        {
            return true;
        }

        if (sSdrBestPath[0] != '\0')
        {
            const char* bestOnly[] = { sSdrBestPath };
            if (LoadPackedAssetIndexToCart(bestOnly, 1, sSdrPackCache) &&
                indexAcceptable(sSdrPackCache.entries.size()))
            {
                return true;
            }
            InvalidatePackedAssetCache(sSdrPackCache);
        }

        size_t bestEntries = 0;
        bool found = false;
        for (size_t i = 0; i < count; ++i)
        {
            if (!candidates[i] || candidates[i][0] == '\0') continue;
            size_t entryCount = 0;
            if (!probePackedEntryCount(candidates[i], entryCount))
            {
                continue;
            }
            if (!indexAcceptable(entryCount))
            {
                continue;
            }

            if (!found || entryCount > bestEntries)
            {
                bestEntries = entryCount;
                found = true;
                ::strncpy(sSdrBestPath, candidates[i], sizeof(sSdrBestPath) - 1);
                sSdrBestPath[sizeof(sSdrBestPath) - 1] = '\0';
            }
        }

        if (!found) return false;
        sSdrBestEntries = bestEntries;
        const char* bestOnly[] = { sSdrBestPath };
        if (!LoadPackedAssetIndexToCart(bestOnly, 1, sSdrPackCache))
        {
            InvalidatePackedAssetCache(sSdrPackCache);
            return false;
        }
        if (!indexAcceptable(sSdrPackCache.entries.size()))
        {
            InvalidatePackedAssetCache(sSdrPackCache);
            return false;
        }
        return true;
    };

    auto tryLoadFromCandidates = [&](const char* const* candidates, size_t count) -> bool
    {
        if (!loadBestCandidate(candidates, count))
        {
            return false;
        }
        if (!tryLoadSdrBySegmentId() &&
            !LoadPackedAssetEntryToBlob(sSdrPackCache, packedName, outBlob))
        {
            return false;
        }
        rememberTrustedSource();
        return true;
    };

    auto tryLoad = [&]() -> bool
    {
        if (sSdrPinnedPath[0] != '\0')
        {
            const char* pinnedCandidates[] = { sSdrPinnedPath };
            if (tryLoadFromCandidates(pinnedCandidates, 1)) return true;
        }
        return tryLoadFromCandidates(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]));
    };

    auto tryParseLoaded = [&]() -> bool
    {
        if (SegmentDrawReady::Loader::Parse(outBlob, outView)) return true;
        const uint32_t m = (outBlob.bytes.size() >= 4) ? ReadLe32(outBlob.bytes.data()) : 0u;
        const uint16_t v = (outBlob.bytes.size() >= 6) ? ReadLe16(outBlob.bytes.data() + 4) : 0u;
        SRL::Debug::Print(1, 15, "SDR parse fail %03d m:%lx v:%u s:%u",
                          segmentId,
                          static_cast<unsigned long>(m),
                          static_cast<unsigned>(v),
                          static_cast<unsigned>(outBlob.bytes.size()));
        outView = {};
        return false;
    };

    if (tryLoad())
    {
        if (tryParseLoaded()) return true;
    }

    // Recovery: force one full pack reload when cached index/entry lookup fails.
    // SDR runtime stays cart-only (4MB) for performance.
    InvalidatePackedAssetCache(sSdrPackCache);
    if (tryLoad())
    {
        if (tryParseLoaded()) return true;
    }

    const auto cart = SRL::Memory::CartRam::GetReport();
    SRL::Debug::Print(1, 15, "SDR miss id:%d cache:%u entries:%u tr:%u cfree:%u",
                      segmentId,
                      static_cast<unsigned>(sSdrPackCache.size),
                      static_cast<unsigned>(sSdrPackCache.entries.size()),
                      static_cast<unsigned>(sSdrTrustedEntries),
                      static_cast<unsigned>(cart.FreeSize));
    if (sSdrPackCache.sourcePath[0] != '\0')
    {
        SRL::Debug::Print(1, 16, "SDR src:%s", sSdrPackCache.sourcePath);
    }
    (void)segmentId;
    outBlob.loaded = false;
    outBlob.size = 0;
    outBlob.bytes.clear();
    outView = {};
    return false;
}

// Read only SDR header counts used for memory planning.
static bool LoadSdrHeaderForSegment(int segmentId, SegmentDrawReady::HeaderV1& outHeader)
{
    SegmentDrawReady::Blob blob{};
    SegmentDrawReady::Loader::View view{};
    if (!LoadSdrForSegment(segmentId, blob, view)) return false;
    outHeader = view.header;
    return true;
}

static bool LoadRdrForSegment(int segmentId, SegmentRuntimeDraw::Blob& outBlob, SegmentRuntimeDraw::Loader::View& outView)
{
    static PackedAssetCache sRdrPackCache{};
    static char sRdrPinnedPath[96]{};
    static size_t sRdrTrustedEntries = 0;
    static char sRdrBestPath[96]{};
    static size_t sRdrBestEntries = 0;
    static std::array<int32_t, 4097> sRdrEntryById{};
    static bool sRdrEntryByIdBuilt = false;
    static size_t sRdrEntryByIdCount = 0;
    static char sRdrEntryByIdSource[96]{};
    char packedName[16]{};
    std::snprintf(packedName, sizeof(packedName), "S%03d.RDR", segmentId);
    const char* packCandidates[] = {
        "/CD/DATA/RDR.BIN",
        "/CD/DATA/RDR.BIN;1",
        "/DATA/RDR.BIN",
        "/DATA/RDR.BIN;1",
        "CD/DATA/RDR.BIN",
        "CD/DATA/RDR.BIN;1",
        "DATA/RDR.BIN",
        "DATA/RDR.BIN;1",
        "cd/data/RDR.BIN",
        "cd/data/RDR.BIN;1",
        "cd/data/rdr.bin",
        "cd/data/rdr.bin;1",
        "data/RDR.BIN",
        "data/RDR.BIN;1",
        "data/rdr.bin",
        "data/rdr.bin;1",
        "RDR.BIN",
        "RDR.BIN;1",
        "rdr.bin",
        "rdr.bin;1"
    };

    auto indexAcceptable = [&](size_t entryCount) -> bool
    {
        if (entryCount == 0) return false;
        if (segmentId > 0 && entryCount < static_cast<size_t>(segmentId)) return false;
        if (sRdrTrustedEntries > 0 && entryCount < sRdrTrustedEntries) return false;
        return true;
    };

    auto parseRdrNameToId = [&](const char* name, int32_t& outId) -> bool
    {
        outId = -1;
        if (!name) return false;
        if (name[0] != 'S' && name[0] != 's') return false;
        if (name[1] < '0' || name[1] > '9') return false;
        if (name[2] < '0' || name[2] > '9') return false;
        if (name[3] < '0' || name[3] > '9') return false;
        if (name[4] != '.') return false;
        outId = (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');
        return outId > 0;
    };

    auto rebuildRdrEntryLookup = [&]()
    {
        for (size_t i = 0; i < sRdrEntryById.size(); ++i) sRdrEntryById[i] = -1;
        for (size_t i = 0; i < sRdrPackCache.entries.size(); ++i)
        {
            int32_t id = -1;
            if (!parseRdrNameToId(sRdrPackCache.entries[i].name, id)) continue;
            if (id <= 0 || static_cast<size_t>(id) >= sRdrEntryById.size()) continue;
            if (sRdrEntryById[static_cast<size_t>(id)] < 0)
            {
                sRdrEntryById[static_cast<size_t>(id)] = static_cast<int32_t>(i);
            }
        }
        sRdrEntryByIdBuilt = true;
        sRdrEntryByIdCount = sRdrPackCache.entries.size();
        if (sRdrPackCache.sourcePath[0] != '\0')
        {
            ::strncpy(sRdrEntryByIdSource, sRdrPackCache.sourcePath, sizeof(sRdrEntryByIdSource) - 1);
            sRdrEntryByIdSource[sizeof(sRdrEntryByIdSource) - 1] = '\0';
        }
        else
        {
            sRdrEntryByIdSource[0] = '\0';
        }
    };

    auto tryLoadRdrBySegmentId = [&]() -> bool
    {
        if (segmentId <= 0) return false;
        if (static_cast<size_t>(segmentId) >= sRdrEntryById.size()) return false;
        const bool sourceChanged = (::strcmp(sRdrEntryByIdSource, sRdrPackCache.sourcePath) != 0);
        if (!sRdrEntryByIdBuilt || sRdrEntryByIdCount != sRdrPackCache.entries.size() || sourceChanged)
        {
            rebuildRdrEntryLookup();
        }

        const int32_t idx = sRdrEntryById[static_cast<size_t>(segmentId)];
        if (idx < 0) return false;
        const size_t uidx = static_cast<size_t>(idx);
        if (uidx >= sRdrPackCache.entries.size()) return false;
        const auto& e = sRdrPackCache.entries[uidx];
        if (e.size == 0) return false;
        if (static_cast<uint64_t>(e.offset) + static_cast<uint64_t>(e.size) > static_cast<uint64_t>(sRdrPackCache.size))
        {
            return false;
        }

        const uint8_t* src = static_cast<const uint8_t*>(sRdrPackCache.cartPtr) + e.offset;
        outBlob.bytes.resize(e.size);
        ::memcpy(outBlob.bytes.data(), src, e.size);
        if (outBlob.bytes.size() < sizeof(SegmentRuntimeDraw::HeaderV1)) return false;
        if (ReadLe32(outBlob.bytes.data()) != SegmentRuntimeDraw::kMagicRdr1) return false;
        if (ReadLe16(outBlob.bytes.data() + 4) != SegmentRuntimeDraw::kVersion1) return false;
        outBlob.loaded = true;
        outBlob.size = outBlob.bytes.size();
        return true;
    };

    auto rememberTrustedSource = [&]()
    {
        const size_t entryCount = sRdrPackCache.entries.size();
        bool shouldPin = false;
        if (entryCount > sRdrTrustedEntries)
        {
            sRdrTrustedEntries = entryCount;
            shouldPin = true;
        }
        else if (sRdrPinnedPath[0] == '\0' && entryCount > 0)
        {
            shouldPin = true;
        }
        else if (entryCount == sRdrTrustedEntries && entryCount > 0)
        {
            shouldPin = true;
        }

        if (shouldPin && sRdrPackCache.sourcePath[0] != '\0')
        {
            ::strncpy(sRdrPinnedPath, sRdrPackCache.sourcePath, sizeof(sRdrPinnedPath) - 1);
            sRdrPinnedPath[sizeof(sRdrPinnedPath) - 1] = '\0';
        }
        if (entryCount >= sRdrBestEntries && sRdrPackCache.sourcePath[0] != '\0')
        {
            sRdrBestEntries = entryCount;
            ::strncpy(sRdrBestPath, sRdrPackCache.sourcePath, sizeof(sRdrBestPath) - 1);
            sRdrBestPath[sizeof(sRdrBestPath) - 1] = '\0';
        }
    };

    auto probePackedEntryCount = [&](const char* path, size_t& outEntryCount) -> bool
    {
        outEntryCount = 0;
        if (!path || path[0] == '\0') return false;
        SRL::Cd::File f(path);
        if (!f.Exists() || f.Size.Bytes < 12) return false;
        if (!f.Open()) return false;

        uint8_t hdr[12]{};
        const int32_t got = f.Read(12, hdr);
        if (got < 12) return false;

        const uint32_t magic = ReadLe32(hdr + 0);
        const uint32_t version = ReadLe32(hdr + 4);
        const uint32_t entryCount = ReadLe32(hdr + 8);
        if (magic != 0x314B4150 || version != 1u || entryCount == 0u) return false;
        outEntryCount = static_cast<size_t>(entryCount);
        return true;
    };

    auto loadBestCandidate = [&](const char* const* candidates, size_t count) -> bool
    {
        if (sRdrPackCache.cartPtr && sRdrPackCache.size > 0 &&
            indexAcceptable(sRdrPackCache.entries.size()))
        {
            return true;
        }

        if (sRdrBestPath[0] != '\0')
        {
            const char* bestOnly[] = { sRdrBestPath };
            if (LoadPackedAssetIndexToCart(bestOnly, 1, sRdrPackCache) &&
                indexAcceptable(sRdrPackCache.entries.size()))
            {
                return true;
            }
            InvalidatePackedAssetCache(sRdrPackCache);
        }

        size_t bestEntries = 0;
        bool found = false;
        for (size_t i = 0; i < count; ++i)
        {
            if (!candidates[i] || candidates[i][0] == '\0') continue;
            size_t entryCount = 0;
            if (!probePackedEntryCount(candidates[i], entryCount)) continue;
            if (!indexAcceptable(entryCount)) continue;

            if (!found || entryCount > bestEntries)
            {
                bestEntries = entryCount;
                found = true;
                ::strncpy(sRdrBestPath, candidates[i], sizeof(sRdrBestPath) - 1);
                sRdrBestPath[sizeof(sRdrBestPath) - 1] = '\0';
            }
        }

        if (!found) return false;
        sRdrBestEntries = bestEntries;
        const char* bestOnly[] = { sRdrBestPath };
        if (!LoadPackedAssetIndexToCart(bestOnly, 1, sRdrPackCache))
        {
            InvalidatePackedAssetCache(sRdrPackCache);
            return false;
        }
        if (!indexAcceptable(sRdrPackCache.entries.size()))
        {
            InvalidatePackedAssetCache(sRdrPackCache);
            return false;
        }
        return true;
    };

    auto tryLoadFromCandidates = [&](const char* const* candidates, size_t count) -> bool
    {
        if (!loadBestCandidate(candidates, count))
        {
            return false;
        }
        if (!tryLoadRdrBySegmentId() &&
            !LoadPackedAssetEntryToBlob(sRdrPackCache, packedName, outBlob))
        {
            return false;
        }
        rememberTrustedSource();
        return true;
    };

    auto tryLoad = [&]() -> bool
    {
        if (sRdrPinnedPath[0] != '\0')
        {
            const char* pinnedCandidates[] = { sRdrPinnedPath };
            if (tryLoadFromCandidates(pinnedCandidates, 1)) return true;
        }
        return tryLoadFromCandidates(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]));
    };

    auto tryParseLoaded = [&]() -> bool
    {
        if (SegmentRuntimeDraw::Loader::Parse(outBlob, outView)) return true;
        const uint32_t m = (outBlob.bytes.size() >= 4) ? ReadLe32(outBlob.bytes.data()) : 0u;
        const uint16_t v = (outBlob.bytes.size() >= 6) ? ReadLe16(outBlob.bytes.data() + 4) : 0u;
        SRL::Debug::Print(1, 15, "RDR parse fail %03d m:%lx v:%u s:%u",
                          segmentId,
                          static_cast<unsigned long>(m),
                          static_cast<unsigned>(v),
                          static_cast<unsigned>(outBlob.bytes.size()));
        outView = {};
        return false;
    };

    if (tryLoad())
    {
        if (tryParseLoaded()) return true;
    }

    InvalidatePackedAssetCache(sRdrPackCache);
    if (tryLoad())
    {
        if (tryParseLoaded()) return true;
    }

    const auto cart = SRL::Memory::CartRam::GetReport();
    SRL::Debug::Print(1, 15, "RDR miss id:%d cache:%u entries:%u tr:%u cfree:%u",
                      segmentId,
                      static_cast<unsigned>(sRdrPackCache.size),
                      static_cast<unsigned>(sRdrPackCache.entries.size()),
                      static_cast<unsigned>(sRdrTrustedEntries),
                      static_cast<unsigned>(cart.FreeSize));
    if (sRdrPackCache.sourcePath[0] != '\0')
    {
        SRL::Debug::Print(1, 16, "RDR src:%s", sRdrPackCache.sourcePath);
    }

    outBlob.loaded = false;
    outBlob.size = 0;
    outBlob.bytes.clear();
    outView = {};
    return false;
}

static bool LoadRdrMappedForSegment(int segmentId,
                                    SegmentRuntimeDraw::MappedBlob& outBlob,
                                    SegmentRuntimeDraw::Loader::View& outView,
                                    SegmentRuntimeDraw::Blob* fallbackBlob = nullptr)
{
    static TrackRuntimePackCache sTrackRdrPackCache{};
    const char* packCandidates[] = {
        "/CD/DATA/TRKRDR.BIN",
        "/CD/DATA/TRKRDR.BIN;1",
        "/DATA/TRKRDR.BIN",
        "/DATA/TRKRDR.BIN;1",
        "CD/DATA/TRKRDR.BIN",
        "CD/DATA/TRKRDR.BIN;1",
        "DATA/TRKRDR.BIN",
        "DATA/TRKRDR.BIN;1",
        "cd/data/TRKRDR.BIN",
        "cd/data/TRKRDR.BIN;1",
        "cd/data/trkrdr.bin",
        "cd/data/trkrdr.bin;1",
        "data/TRKRDR.BIN",
        "data/TRKRDR.BIN;1",
        "data/trkrdr.bin",
        "data/trkrdr.bin;1",
        "TRKRDR.BIN",
        "TRKRDR.BIN;1",
        "trkrdr.bin",
        "trkrdr.bin;1"
    };

    outBlob = {};
    outView = {};
    bool directPackLoaded = false;

    if (LoadTrackRuntimePackToCart(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]), sTrackRdrPackCache))
    {
        directPackLoaded = true;
        const uint8_t* blobData = nullptr;
        size_t blobSize = 0;
        TrackRuntimePack::DirectoryEntryV1 entry{};
        if (TrackRuntimePack::Loader::ResolveEntryRange(sTrackRdrPackCache.view, segmentId, blobData, blobSize, &entry))
        {
            outBlob.data = blobData;
            outBlob.size = blobSize;
            outBlob.loaded = true;
            if (SegmentRuntimeDraw::Loader::Parse(outBlob, outView))
            {
                if (g_rdrBuildScratch.verts.capacity() < sTrackRdrPackCache.view.header.maxVertexCount)
                {
                    g_rdrBuildScratch.verts.reserve(sTrackRdrPackCache.view.header.maxVertexCount);
                }
                if (g_rdrBuildScratch.faces.capacity() < sTrackRdrPackCache.view.header.maxFaceCount)
                {
                    g_rdrBuildScratch.faces.reserve(sTrackRdrPackCache.view.header.maxFaceCount);
                }
                if (g_rdrBuildScratch.attrs.capacity() < sTrackRdrPackCache.view.header.maxFaceCount)
                {
                    g_rdrBuildScratch.attrs.reserve(sTrackRdrPackCache.view.header.maxFaceCount);
                }
                return true;
            }

            const uint32_t m = (blobSize >= 4) ? ReadLe32(blobData + 0) : 0u;
            const uint16_t v = (blobSize >= 6) ? ReadLe16(blobData + 4) : 0u;
            SRL::Debug::Print(1, 15, "TRKRDR parse fail %03d m:%lx v:%u s:%u",
                              segmentId,
                              static_cast<unsigned long>(m),
                              static_cast<unsigned>(v),
                              static_cast<unsigned>(blobSize));
        }
    }

    if (!directPackLoaded && fallbackBlob)
    {
        fallbackBlob->loaded = false;
        fallbackBlob->size = 0;
        fallbackBlob->bytes.clear();
        if (LoadRdrForSegment(segmentId, *fallbackBlob, outView))
        {
            outBlob.data = fallbackBlob->bytes.data();
            outBlob.size = fallbackBlob->bytes.size();
            outBlob.loaded = true;
            return true;
        }
    }

    const auto cart = SRL::Memory::CartRam::GetReport();
    SRL::Debug::Print(1, 15, "RDR miss id:%d cache:%u entries:%u tr:%u cfree:%u",
                      segmentId,
                      static_cast<unsigned>(sTrackRdrPackCache.size),
                      directPackLoaded
                          ? static_cast<unsigned>(sTrackRdrPackCache.view.header.segmentCount)
                          : 0u,
                      directPackLoaded
                          ? static_cast<unsigned>(sTrackRdrPackCache.view.header.maxSegmentId)
                          : 0u,
                      static_cast<unsigned>(cart.FreeSize));
    if (sTrackRdrPackCache.sourcePath[0] != '\0')
    {
        SRL::Debug::Print(1, 16, "RDR src:%s", sTrackRdrPackCache.sourcePath);
    }
    return false;
}

static bool LoadRdrHeaderForSegment(int segmentId, SegmentRuntimeDraw::HeaderV1& outHeader)
{
    SegmentRuntimeDraw::MappedBlob blob{};
    SegmentRuntimeDraw::Loader::View view{};
    SegmentRuntimeDraw::Blob fallbackBlob{};
    if (!LoadRdrMappedForSegment(segmentId, blob, view, &fallbackBlob)) return false;
    outHeader = view.header;
    return true;
}

// Estimate a safe package size for runtime batch assembly using current High Work RAM.
static size_t ComputeSafeSegmentsPerPackage(size_t requestedSegments)
{
    if (requestedSegments == 0) return 1;

    SegmentDrawReady::HeaderV1 ref{};
    if (!LoadSdrHeaderForSegment(1, ref) || ref.vertexCount == 0 || ref.faceCount == 0)
    {
        return requestedSegments;
    }

    const size_t bytesPerSeg =
        static_cast<size_t>(ref.vertexCount) * sizeof(SRL::Math::Types::Vector3D) +
        static_cast<size_t>(ref.faceCount) * (sizeof(SRL::Types::Polygon) + sizeof(SRL::Types::Attribute)) +
        static_cast<size_t>(ref.faceCount) * (sizeof(uint16_t) + sizeof(uint8_t));

    // Keep a fixed reserve for frame runtime systems and transient data.
    const size_t hwrFree = GetHighWorkRamFreeBytesSafe();
    const size_t usable = (hwrFree > kWorkRamPlanningHeadroomBytes) ? (hwrFree - kWorkRamPlanningHeadroomBytes) : 0;

    const size_t byMem = (bytesPerSeg > 0 && usable > 0) ? (usable / bytesPerSeg) : 1;
    const size_t byIndex = (ref.vertexCount > 0) ? (65000u / static_cast<size_t>(ref.vertexCount)) : requestedSegments;

    size_t safe = std::min(requestedSegments, std::max<size_t>(1, std::min(byMem, byIndex)));

    // In test mode we prefer preserving the requested package size.
    // A too-conservative clamp here collapses the view to a single segment package.
    if (safe < requestedSegments)
    {
        SRL::Debug::Print(1, 12, "PKG guard bypass req:%u est:%u hwr:%u",
                          static_cast<unsigned>(requestedSegments),
                          static_cast<unsigned>(safe),
                          static_cast<unsigned>(hwrFree));
        safe = requestedSegments;
    }

    SRL::Debug::Print(1, 12, "PKG safe req:%u got:%u hwr:%u",
                      static_cast<unsigned>(requestedSegments),
                      static_cast<unsigned>(safe),
                      static_cast<unsigned>(hwrFree));
    return safe;
}

// Estimate bytes needed in runtime containers for one SDR segment.
// This is a conservative estimate used only for memory admission checks.
static size_t EstimateSdrSegmentRuntimeBytes(const SegmentDrawReady::HeaderV1& hdr)
{
    const size_t v = static_cast<size_t>(hdr.vertexCount);
    const size_t f = static_cast<size_t>(hdr.faceCount);
    const size_t vertsBytes = v * sizeof(SRL::Math::Types::Vector3D);
    const size_t facesBytes = f * sizeof(SRL::Types::Polygon);
    const size_t attrsBytes = f * sizeof(SRL::Types::Attribute);
    const size_t lodBytes = f * (sizeof(uint16_t) + sizeof(uint8_t) + sizeof(int32_t));
    // Keep a small fixed overhead for vector metadata and allocator alignment.
    const size_t overhead = 2u * 1024u;
    return vertsBytes + facesBytes + attrsBytes + lodBytes + overhead;
}

// Check if a contiguous segment batch can be admitted with current High Work RAM.
// Keeps a fixed reserve to avoid starving other systems in the same frame.
static bool CanAdmitSdrBatchInHighWorkRam(size_t firstSegmentId,
                                          size_t lastSegmentId,
                                          size_t reserveBytes,
                                          size_t& outEstimatedBytes,
                                          size_t& outFreeBytes)
{
    outEstimatedBytes = 0;
    bool freeValid = false;
    outFreeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    if (!freeValid)
    {
        // Keep planner permissive when report telemetry is temporarily invalid.
        outFreeBytes = std::numeric_limits<size_t>::max();
    }
    if (lastSegmentId < firstSegmentId) return false;

    for (size_t sid = firstSegmentId; sid <= lastSegmentId; ++sid)
    {
        SegmentDrawReady::HeaderV1 hdr{};
        if (!LoadSdrHeaderForSegment(static_cast<int>(sid), hdr))
        {
            return false;
        }
        outEstimatedBytes += EstimateSdrSegmentRuntimeBytes(hdr);
    }

    if (outFreeBytes <= reserveBytes + kWorkRamHardFloorBytes) return false;
    const size_t usable = outFreeBytes - reserveBytes;
    // Safety margin on top of estimate to absorb per-allocation metadata and
    // temporary vectors while a segment renderer is being built.
    const size_t guardedEstimate = outEstimatedBytes + (outEstimatedBytes / 5u); // +20%
    if (guardedEstimate <= usable) return true;

    // Fallback: allow exact estimate when free memory after admission still
    // keeps a hard floor for runtime systems.
    if (outEstimatedBytes <= usable)
    {
        const size_t freeAfter = outFreeBytes - outEstimatedBytes;
        return freeAfter >= kWorkRamHardFloorBytes;
    }
    return false;
}

static bool LoadSdrFamilyIdsForSegment(int segmentId, TrackLowWorkU16Vector& outFamilyIds)
{
    outFamilyIds.clear();

    auto trimStaticBlobIfLow = [&]()
    {
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        if (!freeValid) return;
        if (freeBytes > (kWorkRamHardFloorBytes + (64u * 1024u))) return;
        ResetBlobBytes(g_sdrFamilyIdsScratch);
    };
    trimStaticBlobIfLow();
    g_sdrFamilyIdsScratch.loaded = false;
    g_sdrFamilyIdsScratch.size = 0;
    g_sdrFamilyIdsScratch.bytes.clear();
    SegmentDrawReady::Loader::View sdrView{};
    if (!LoadSdrForSegment(segmentId, g_sdrFamilyIdsScratch, sdrView))
    {
        return false;
    }

    if (sdrView.header.faceCount == 0) return false;
    outFamilyIds.assign(static_cast<size_t>(sdrView.header.faceCount), 0);
    for (uint32_t fi = 0; fi < sdrView.header.faceCount; ++fi)
    {
        uint16_t familyId = 0;
        const size_t off = sdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
        if (!SegmentDrawReady::Loader::ReadFamilyIdLeAt(g_sdrFamilyIdsScratch.bytes, off, familyId)) return false;
        outFamilyIds[static_cast<size_t>(fi)] = familyId;
    }

    trimStaticBlobIfLow();
    return true;
}

static bool LoadRuntimeFamilyIdsForSegment(int segmentId, TrackLowWorkU16Vector& outFamilyIds)
{
    outFamilyIds.clear();

    auto trimStaticBlobIfLow = [&]()
    {
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        if (!freeValid) return;
        if (freeBytes > (kWorkRamHardFloorBytes + (64u * 1024u))) return;
        ResetBlobBytes(g_rdrFamilyIdsScratch);
    };
    trimStaticBlobIfLow();

    SegmentRuntimeDraw::MappedBlob rdrBlob{};
    SegmentRuntimeDraw::Loader::View rdrView{};
    if (LoadRdrMappedForSegment(segmentId, rdrBlob, rdrView, &g_rdrFamilyIdsScratch))
    {
        outFamilyIds.resize(rdrView.header.faceCount);
        for (uint32_t fi = 0; fi < rdrView.header.faceCount; ++fi)
        {
            uint16_t familyId = 0;
            const size_t off = rdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
            if (!SegmentRuntimeDraw::Loader::ReadFamilyIdLeAt(rdrView.data, rdrView.blobSize, off, familyId))
            {
                SRL::Debug::Print(1, 15, "RDR fam read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
                outFamilyIds.clear();
                break;
            }
            outFamilyIds[static_cast<size_t>(fi)] = familyId;
        }
        trimStaticBlobIfLow();
        if (!outFamilyIds.empty()) return true;
    }

    if (kEnableTrackRuntimeStabilization)
    {
        return false;
    }

    return LoadSdrFamilyIdsForSegment(segmentId, outFamilyIds);
}

static size_t ApplyMatFamiliesToRenderer(TrackRenderer& renderer,
                                         const SegmentComponent::Blob& matBlob,
                                         const SegmentComponent::Loader::MatView& matView,
                                         const uint16_t* familyIds,
                                         const int32_t* familySlots,
                                         size_t familyCount)
{
    const size_t rendererFaces = static_cast<size_t>(renderer.FaceCount());
    if (rendererFaces == 0) return 0;
    const size_t nFaces = std::min(rendererFaces, static_cast<size_t>(matView.header.faceCount));
    if (nFaces == 0) return 0;

    std::vector<int16_t> remap(rendererFaces, -1);
    size_t changed = 0;
    size_t missingFamilies = 0;
    uint16_t lastMissingFamily = 0;

    for (size_t fi = 0; fi < nFaces; ++fi)
    {
        SegmentComponent::MatFaceBinding mb{};
        const size_t moff = matView.bindingOffset + fi * sizeof(SegmentComponent::MatFaceBinding);
        if (!SegmentComponent::Loader::ReadMatFaceBindingLeAt(matBlob.bytes, moff, mb)) continue;
        const uint16_t fam = static_cast<uint16_t>(mb.materialId);
        if (fam == 0) continue;

        int32_t slot = -1;
        for (size_t i = 0; i < familyCount; ++i)
        {
            if (familyIds[i] == fam)
            {
                slot = familySlots[i];
                break;
            }
        }

        if (slot > 0)
        {
            remap[fi] = slot;
            ++changed;
        }
        else
        {
            ++missingFamilies;
            lastMissingFamily = fam;
        }
    }

    if (missingFamilies > 0)
    {
        SRL::Debug::Print(1, 19, "MAT8 miss face:%u lastFam:%u", (unsigned)missingFamilies, (unsigned)lastMissingFamily);
    }
    (void)renderer.ApplyFaceTextureSlotsGlobal(remap);
    return changed;
}

static bool LoadGeoForSegment(int segmentId, SegmentComponent::Blob& outBlob, SegmentComponent::Loader::GeoView& outView)
{
    static PackedAssetCache sGeoPackCache{};
    char packedName[16]{};
    std::snprintf(packedName, sizeof(packedName), "S%03d.GEO", segmentId);
    const char* packCandidates[] = {
        "CD/DATA/GEO.BIN",
        "CD/DATA/GEO.BIN;1",
        "DATA/GEO.BIN",
        "DATA/GEO.BIN;1",
        "GEO.BIN",
        "GEO.BIN;1"
    };
    if (LoadPackedAssetIndexToCart(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]), sGeoPackCache) &&
        LoadPackedAssetEntryToBlob(sGeoPackCache, packedName, outBlob))
    {
        return SegmentComponent::Loader::ParseGeo(outBlob, outView);
    }
    (void)segmentId;
    outBlob = {};
    outView = {};
    return false;
}

static bool BuildRendererFromRdr(int segmentId,
                                 TrackRenderer& renderer,
                                 Vector3D* outCenter,
                                 TrackLowWorkU16Vector* outFamilyIds = nullptr)
{
    auto trimStaticScratchIfLow = [&]()
    {
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        if (!freeValid) return;
        if (freeBytes > (kWorkRamHardFloorBytes + (64u * 1024u))) return;
        TrimRuntimeBlobScratchCaches(true);
    };
    trimStaticScratchIfLow();

    SegmentRuntimeDraw::MappedBlob rdrBlob{};
    SegmentRuntimeDraw::Loader::View rdrView{};
    if (!LoadRdrMappedForSegment(segmentId, rdrBlob, rdrView, &g_rdrBuildScratch.blob))
    {
        return false;
    }

    auto& verts = g_rdrBuildScratch.verts;
    auto& faces = g_rdrBuildScratch.faces;
    auto& attrs = g_rdrBuildScratch.attrs;
    verts.clear();
    faces.clear();
    attrs.clear();
    (void)CompactEmptyVectorForTarget(verts, rdrView.header.vertexCount);
    (void)CompactEmptyVectorForTarget(faces, rdrView.header.faceCount);
    (void)CompactEmptyVectorForTarget(attrs, rdrView.header.faceCount);
    if (verts.capacity() < rdrView.header.vertexCount) verts.reserve(rdrView.header.vertexCount);
    if (faces.capacity() < rdrView.header.faceCount) faces.reserve(rdrView.header.faceCount);
    if (attrs.capacity() < rdrView.header.faceCount) attrs.reserve(rdrView.header.faceCount);
    if (outFamilyIds)
    {
        outFamilyIds->clear();
        outFamilyIds->reserve(rdrView.header.faceCount);
        for (uint32_t fi = 0; fi < rdrView.header.faceCount; ++fi)
        {
            uint16_t familyId = 0;
            const size_t off = rdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
            if (!SegmentRuntimeDraw::Loader::ReadFamilyIdLeAt(rdrView.data, rdrView.blobSize, off, familyId))
            {
                SRL::Debug::Print(1, 15, "RDR fam read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
                return false;
            }
            outFamilyIds->push_back(familyId);
        }
    }

    for (uint32_t vi = 0; vi < rdrView.header.vertexCount; ++vi)
    {
        SegmentRuntimeDraw::Vertex sv{};
        const size_t off = rdrView.verticesOffset + static_cast<size_t>(vi) * sizeof(SegmentRuntimeDraw::Vertex);
        if (!SegmentRuntimeDraw::Loader::ReadVertexLeAt(rdrView.data, rdrView.blobSize, off, sv))
        {
            SRL::Debug::Print(1, 15, "RDR vtx read fail %03d v:%u", segmentId, static_cast<unsigned>(vi));
            return false;
        }
        verts.push_back(Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sv.x),
            SRL::Math::Types::Fxp::BuildRaw(sv.y),
            SRL::Math::Types::Fxp::BuildRaw(sv.z)));
    }

    for (uint32_t fi = 0; fi < rdrView.header.faceCount; ++fi)
    {
        SegmentRuntimeDraw::Face sf{};
        SegmentRuntimeDraw::Attr sa{};
        const size_t foff = rdrView.facesOffset + static_cast<size_t>(fi) * sizeof(SegmentRuntimeDraw::Face);
        const size_t aoff = rdrView.attrsOffset + static_cast<size_t>(fi) * sizeof(SegmentRuntimeDraw::Attr);
        if (!SegmentRuntimeDraw::Loader::ReadFaceLeAt(rdrView.data, rdrView.blobSize, foff, sf))
        {
            SRL::Debug::Print(1, 15, "RDR face read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
            return false;
        }
        if (!SegmentRuntimeDraw::Loader::ReadAttrLeAt(rdrView.data, rdrView.blobSize, aoff, sa))
        {
            SRL::Debug::Print(1, 15, "RDR attr read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
            return false;
        }

        const uint16_t indices[4] = { sf.v0, sf.v1, sf.v2, sf.v3 };
        for (size_t i = 0; i < 4; ++i)
        {
            if (static_cast<size_t>(indices[i]) >= verts.size())
            {
                return false;
            }
        }

        SRL::Types::Polygon p{};
        p.Vertices[0] = sf.v0;
        p.Vertices[1] = sf.v1;
        p.Vertices[2] = sf.v2;
        p.Vertices[3] = sf.v3;
        p.Normal = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sf.normalX),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalY),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalZ));
        faces.push_back(p);

        SRL::Types::Attribute attr{};
        attr.Visibility = (sa.visibility != 0)
            ? SRL::Types::Attribute::FaceVisibility::DoubleSided
            : SRL::Types::Attribute::FaceVisibility::SingleSided;
        attr.Sort = sa.sort;
        attr.Texture = sa.texture;
        attr.Display = sa.display;
        attr.ColorMode = sa.colorMode;
        attr.Gouraud = sa.gouraud;
        attr.Direction = sa.direction;
        attrs.push_back(attr);
    }

    const bool ok = renderer.InitializeFromComponentDataRecycled(verts, faces, attrs);
    if (!ok)
    {
        SRL::Debug::Print(1, 15, "RDR init cmp fail %03d", segmentId);
    }
    if (ok && outCenter)
    {
        *outCenter = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(rdrView.header.centerX),
            SRL::Math::Types::Fxp::BuildRaw(rdrView.header.centerY),
            SRL::Math::Types::Fxp::BuildRaw(rdrView.header.centerZ));
    }
    trimStaticScratchIfLow();
    return ok;
}

static bool BuildRendererFromSdr(int segmentId,
                                 TrackRenderer& renderer,
                                 Vector3D* outCenter,
                                 TrackLowWorkU16Vector* outFamilyIds = nullptr)
{
    auto trimStaticScratchIfLow = [&]()
    {
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        if (!freeValid) return;
        if (freeBytes > (kWorkRamHardFloorBytes + (64u * 1024u))) return;
        TrimRuntimeBlobScratchCaches(true);
    };
    trimStaticScratchIfLow();
    g_sdrBuildScratch.blob.loaded = false;
    g_sdrBuildScratch.blob.size = 0;
    g_sdrBuildScratch.blob.bytes.clear();
    SegmentDrawReady::Loader::View sdrView{};
    if (!LoadSdrForSegment(segmentId, g_sdrBuildScratch.blob, sdrView))
    {
        SRL::Debug::Print(1, 15, "SDR load fail %03d", segmentId);
        return false;
    }

    auto& verts = g_sdrBuildScratch.verts;
    auto& faces = g_sdrBuildScratch.faces;
    auto& attrs = g_sdrBuildScratch.attrs;
    verts.clear();
    faces.clear();
    attrs.clear();
    (void)CompactEmptyVectorForTarget(verts, sdrView.header.vertexCount);
    (void)CompactEmptyVectorForTarget(faces, sdrView.header.faceCount);
    (void)CompactEmptyVectorForTarget(attrs, sdrView.header.faceCount);
    if (verts.capacity() < sdrView.header.vertexCount) verts.reserve(sdrView.header.vertexCount);
    if (faces.capacity() < sdrView.header.faceCount) faces.reserve(sdrView.header.faceCount);
    if (attrs.capacity() < sdrView.header.faceCount) attrs.reserve(sdrView.header.faceCount);
    if (outFamilyIds)
    {
        outFamilyIds->clear();
        outFamilyIds->reserve(sdrView.header.faceCount);
        for (uint32_t fi = 0; fi < sdrView.header.faceCount; ++fi)
        {
            uint16_t familyId = 0;
            const size_t off = sdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
            if (!SegmentDrawReady::Loader::ReadFamilyIdLeAt(g_sdrBuildScratch.blob.bytes, off, familyId))
            {
                SRL::Debug::Print(1, 15, "SDR fam read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
                return false;
            }
            outFamilyIds->push_back(familyId);
        }
    }

    for (uint32_t vi = 0; vi < sdrView.header.vertexCount; ++vi)
    {
        SegmentDrawReady::Vertex sv{};
        const size_t off = sdrView.verticesOffset + static_cast<size_t>(vi) * sizeof(SegmentDrawReady::Vertex);
        if (!SegmentDrawReady::Loader::ReadVertexLeAt(g_sdrBuildScratch.blob.bytes, off, sv))
        {
            SRL::Debug::Print(1, 15, "SDR vtx read fail %03d v:%u", segmentId, static_cast<unsigned>(vi));
            return false;
        }

        verts.push_back(Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sv.x),
            SRL::Math::Types::Fxp::BuildRaw(sv.y),
            SRL::Math::Types::Fxp::BuildRaw(sv.z)));
    }

    auto DecodeSortMode = [](uint16_t raw) -> SRL::Types::Attribute::SortMode
    {
        const uint16_t clamped = (raw > 3u) ? 0u : raw;
        return static_cast<SRL::Types::Attribute::SortMode>(SRL::Types::Attribute::SortMode::Center - clamped);
    };

    auto BuildSdrBaseAttr = [&](const SegmentDrawReady::AttrBase& sa) -> SRL::Types::Attribute
    {
        const auto visibility =
            (sa.visibility == static_cast<uint16_t>(SegmentDrawReady::VisibilityMode::SingleSided))
                ? SRL::Types::Attribute::FaceVisibility::SingleSided
                : SRL::Types::Attribute::FaceVisibility::DoubleSided;
        const auto sortMode = DecodeSortMode(sa.sortMode);
        const uint16_t gouraud = sa.gouraudMode ? sa.gouraudMode : CL32KRGB;
        const uint16_t keepFlags = static_cast<uint16_t>(sa.flags & (CL_Trans | CL_Half | MESHon | MESHoff));
        const uint16_t display = static_cast<uint16_t>((sa.colorMode ? sa.colorMode : CL32KRGB) | keepFlags);
        const uint16_t spriteMode = sa.spriteMode ? sa.spriteMode : sprPolygon;
        const uint16_t direction = sa.useLight ? UseLight : UseGouraud;
        return SRL::Types::Attribute(
            visibility,
            sortMode,
            No_Texture,
            sa.baseColor,
            gouraud,
            display,
            spriteMode,
            direction);
    };

    for (uint32_t fi = 0; fi < sdrView.header.faceCount; ++fi)
    {
        SegmentDrawReady::Face sf{};
        SegmentDrawReady::AttrBase sa{};
        const size_t foff = sdrView.facesOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::Face);
        const size_t aoff = sdrView.attrsOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::AttrBase);
        if (!SegmentDrawReady::Loader::ReadFaceLeAt(g_sdrBuildScratch.blob.bytes, foff, sf))
        {
            SRL::Debug::Print(1, 15, "SDR face read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
            return false;
        }
        if (!SegmentDrawReady::Loader::ReadAttrBaseLeAt(g_sdrBuildScratch.blob.bytes, aoff, sa))
        {
            SRL::Debug::Print(1, 15, "SDR attr read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
            return false;
        }

        const uint16_t indices[4] = { sf.v0, sf.v1, sf.v2, sf.v3 };
        for (size_t i = 0; i < 4; ++i)
        {
            if (static_cast<size_t>(indices[i]) >= verts.size())
            {
                SRL::Debug::Print(1, 15, "SEG%03d SDR bad idx f:%u i:%u v:%u max:%u",
                                  segmentId,
                                  static_cast<unsigned>(fi),
                                  static_cast<unsigned>(i),
                                  static_cast<unsigned>(indices[i]),
                                  static_cast<unsigned>(verts.size()));
                return false;
            }
        }

        SRL::Types::Polygon p{};
        p.Vertices[0] = sf.v0;
        p.Vertices[1] = sf.v1;
        p.Vertices[2] = sf.v2;
        p.Vertices[3] = sf.v3;
        p.Normal = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sf.normalX),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalY),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalZ));
        faces.push_back(p);

        attrs.push_back(BuildSdrBaseAttr(sa));
    }

    const bool ok = renderer.InitializeFromComponentDataRecycled(verts, faces, attrs);
    if (!ok)
    {
        SRL::Debug::Print(1, 15, "SDR init cmp fail %03d", segmentId);
    }
    if (ok && outCenter)
    {
        *outCenter = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sdrView.header.centerX),
            SRL::Math::Types::Fxp::BuildRaw(sdrView.header.centerY),
            SRL::Math::Types::Fxp::BuildRaw(sdrView.header.centerZ));
    }
    trimStaticScratchIfLow();
    return ok;
}

static bool BuildRendererFromRuntimeBlob(int segmentId,
                                         TrackRenderer& renderer,
                                         Vector3D* outCenter,
                                         TrackLowWorkU16Vector* outFamilyIds,
                                         bool* outUsedRdr)
{
    if (outUsedRdr) *outUsedRdr = false;
    if (BuildRendererFromRdr(segmentId, renderer, outCenter, outFamilyIds))
    {
        if (outUsedRdr) *outUsedRdr = true;
        return true;
    }
    if (kEnableTrackRuntimeStabilization)
    {
        return false;
    }
    return BuildRendererFromSdr(segmentId, renderer, outCenter, outFamilyIds);
}

// Load one precompiled draw-ready batch from BDR.BIN.
static bool LoadBdrForBatch(int firstSegmentId,
                            int lastSegmentId,
                            BatchDrawReady::Blob& outBlob,
                            BatchDrawReady::Loader::View& outView)
{
    static PackedAssetCache sBdrPackCache{};
    char packedName[24]{};
    std::snprintf(packedName, sizeof(packedName), "B%03d_%03d.BDR", firstSegmentId, lastSegmentId);
    const char* packCandidates[] = {
        "CD/DATA/BDR.BIN",
        "CD/DATA/BDR.BIN;1",
        "DATA/BDR.BIN",
        "DATA/BDR.BIN;1",
        "BDR.BIN",
        "BDR.BIN;1"
    };
    if (!LoadPackedAssetIndexToCart(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]), sBdrPackCache))
    {
        return false;
    }
    if (!LoadPackedAssetEntryToBlob(sBdrPackCache, packedName, outBlob))
    {
        return false;
    }
    return BatchDrawReady::Loader::Parse(outBlob, outView);
}

// Build one renderer directly from a precompiled BDR batch.
static bool BuildRendererFromBdrBatch(int firstId,
                                      int lastId,
                                      uint8_t logicalSegmentCount,
                                      BdrBatchBuildResult& outBatch)
{
    if (firstId <= 0 || lastId < firstId || logicalSegmentCount == 0) return false;

    BatchDrawReady::Blob bdrBlob{};
    BatchDrawReady::Loader::View bdrView{};
    if (!LoadBdrForBatch(firstId, lastId, bdrBlob, bdrView))
    {
        return false;
    }

    std::vector<SRL::Math::Types::Vector3D> verts{};
    std::vector<SRL::Types::Polygon> faces{};
    std::vector<SRL::Types::Attribute> attrs{};
    TrackLowWorkU16Vector familyIds{};
    TrackLowWorkU8Vector faceRankOffsets{};

    verts.reserve(bdrView.header.vertexCount);
    faces.reserve(bdrView.header.faceCount);
    attrs.reserve(bdrView.header.faceCount);
    familyIds.reserve(bdrView.header.faceCount);
    faceRankOffsets.reserve(bdrView.header.faceCount);

    for (uint32_t vi = 0; vi < bdrView.header.vertexCount; ++vi)
    {
        SegmentDrawReady::Vertex sv{};
        const size_t off = bdrView.verticesOffset + static_cast<size_t>(vi) * sizeof(SegmentDrawReady::Vertex);
        if (!SegmentDrawReady::Loader::ReadVertexLeAt(bdrBlob.bytes, off, sv)) return false;

        verts.push_back(Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sv.x),
            SRL::Math::Types::Fxp::BuildRaw(sv.y),
            SRL::Math::Types::Fxp::BuildRaw(sv.z)));
    }

    auto DecodeSortMode = [](uint16_t raw) -> SRL::Types::Attribute::SortMode
    {
        const uint16_t clamped = (raw > 3u) ? 0u : raw;
        return static_cast<SRL::Types::Attribute::SortMode>(SRL::Types::Attribute::SortMode::Center - clamped);
    };

    auto BuildSdrBaseAttr = [&](const SegmentDrawReady::AttrBase& sa) -> SRL::Types::Attribute
    {
        const auto visibility =
            (sa.visibility == static_cast<uint16_t>(SegmentDrawReady::VisibilityMode::SingleSided))
                ? SRL::Types::Attribute::FaceVisibility::SingleSided
                : SRL::Types::Attribute::FaceVisibility::DoubleSided;
        const auto sortMode = DecodeSortMode(sa.sortMode);
        const uint16_t gouraud = sa.gouraudMode ? sa.gouraudMode : CL32KRGB;
        const uint16_t keepFlags = static_cast<uint16_t>(sa.flags & (CL_Trans | CL_Half | MESHon | MESHoff));
        const uint16_t display = static_cast<uint16_t>((sa.colorMode ? sa.colorMode : CL32KRGB) | keepFlags);
        const uint16_t spriteMode = sa.spriteMode ? sa.spriteMode : sprPolygon;
        const uint16_t direction = sa.useLight ? UseLight : UseGouraud;
        return SRL::Types::Attribute(
            visibility,
            sortMode,
            No_Texture,
            sa.baseColor,
            gouraud,
            display,
            spriteMode,
            direction);
    };

    for (uint32_t fi = 0; fi < bdrView.header.faceCount; ++fi)
    {
        SegmentDrawReady::Face sf{};
        SegmentDrawReady::AttrBase sa{};
        const size_t foff = bdrView.facesOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::Face);
        const size_t aoff = bdrView.attrsOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::AttrBase);
        const size_t ioff = bdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
        const size_t roff = bdrView.faceRankOffsetsOffset + static_cast<size_t>(fi);
        if (!SegmentDrawReady::Loader::ReadFaceLeAt(bdrBlob.bytes, foff, sf)) return false;
        if (!SegmentDrawReady::Loader::ReadAttrBaseLeAt(bdrBlob.bytes, aoff, sa)) return false;

        uint16_t familyId = 0;
        if (!SegmentDrawReady::Loader::ReadFamilyIdLeAt(bdrBlob.bytes, ioff, familyId)) return false;
        if (roff >= bdrBlob.bytes.size()) return false;
        const uint8_t rankOffset = bdrBlob.bytes[roff];

        const uint16_t indices[4] = { sf.v0, sf.v1, sf.v2, sf.v3 };
        for (size_t i = 0; i < 4; ++i)
        {
            if (static_cast<size_t>(indices[i]) >= verts.size()) return false;
        }

        SRL::Types::Polygon p{};
        p.Vertices[0] = sf.v0;
        p.Vertices[1] = sf.v1;
        p.Vertices[2] = sf.v2;
        p.Vertices[3] = sf.v3;
        p.Normal = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sf.normalX),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalY),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalZ));
        faces.push_back(p);

        attrs.push_back(BuildSdrBaseAttr(sa));

        familyIds.push_back(familyId);
        faceRankOffsets.push_back(rankOffset);
    }

    auto renderer = std::make_unique<TrackRenderer>();
    if (!renderer->InitializeFromComponentData(std::move(verts),
                                               std::move(faces),
                                               std::move(attrs)))
    {
        return false;
    }

    renderer->SetUseOriginal(false);
    renderer->SetSglDirect(true);
    renderer->SetVdp1Commands(false);
    renderer->SetDirect2D(false);
    renderer->SetForceDoubleSided(false);
    renderer->SetScale(SRL::Math::Types::Fxp::BuildRaw(1 << 16));
    renderer->SetDrawLimit(renderer->MeshCount());

    outBatch = {};
    outBatch.center = Vector3D(
        SRL::Math::Types::Fxp::BuildRaw(bdrView.header.centerX),
        SRL::Math::Types::Fxp::BuildRaw(bdrView.header.centerY),
        SRL::Math::Types::Fxp::BuildRaw(bdrView.header.centerZ));
    outBatch.renderer = std::move(renderer);
    outBatch.familyIds = std::move(familyIds);
    outBatch.faceRankOffsets = std::move(faceRankOffsets);
    (void)logicalSegmentCount;
    return true;
}

static bool AppendSdrSegmentToBatch(int segmentId,
                                    uint8_t rankOffset,
                                    std::vector<SRL::Math::Types::Vector3D>& ioVerts,
                                    std::vector<SRL::Types::Polygon>& ioFaces,
                                    std::vector<SRL::Types::Attribute>& ioAttrs,
                                    TrackLowWorkU16Vector& ioFamilyIds,
                                    TrackLowWorkU8Vector& ioFaceRankOffsets,
                                    Vector3D& ioMin,
                                    Vector3D& ioMax)
{
    SegmentDrawReady::Blob sdrBlob{};
    SegmentDrawReady::Loader::View sdrView{};
    if (!LoadSdrForSegment(segmentId, sdrBlob, sdrView))
    {
        return false;
    }

    const size_t vertexBase = ioVerts.size();

    for (uint32_t vi = 0; vi < sdrView.header.vertexCount; ++vi)
    {
        SegmentDrawReady::Vertex sv{};
        const size_t off = sdrView.verticesOffset + static_cast<size_t>(vi) * sizeof(SegmentDrawReady::Vertex);
        if (!SegmentDrawReady::Loader::ReadVertexLeAt(sdrBlob.bytes, off, sv)) return false;

        const Vector3D v(
            SRL::Math::Types::Fxp::BuildRaw(sv.x),
            SRL::Math::Types::Fxp::BuildRaw(sv.y),
            SRL::Math::Types::Fxp::BuildRaw(sv.z));
        ioVerts.push_back(v);
        ioMin.X = SRL::Math::Min(ioMin.X, v.X);
        ioMin.Y = SRL::Math::Min(ioMin.Y, v.Y);
        ioMin.Z = SRL::Math::Min(ioMin.Z, v.Z);
        ioMax.X = SRL::Math::Max(ioMax.X, v.X);
        ioMax.Y = SRL::Math::Max(ioMax.Y, v.Y);
        ioMax.Z = SRL::Math::Max(ioMax.Z, v.Z);
    }

    auto DecodeSortMode = [](uint16_t raw) -> SRL::Types::Attribute::SortMode
    {
        const uint16_t clamped = (raw > 3u) ? 0u : raw;
        return static_cast<SRL::Types::Attribute::SortMode>(SRL::Types::Attribute::SortMode::Center - clamped);
    };

    auto BuildSdrBaseAttr = [&](const SegmentDrawReady::AttrBase& sa) -> SRL::Types::Attribute
    {
        const auto visibility =
            (sa.visibility == static_cast<uint16_t>(SegmentDrawReady::VisibilityMode::SingleSided))
                ? SRL::Types::Attribute::FaceVisibility::SingleSided
                : SRL::Types::Attribute::FaceVisibility::DoubleSided;
        const auto sortMode = DecodeSortMode(sa.sortMode);
        const uint16_t gouraud = sa.gouraudMode ? sa.gouraudMode : CL32KRGB;
        const uint16_t keepFlags = static_cast<uint16_t>(sa.flags & (CL_Trans | CL_Half | MESHon | MESHoff));
        const uint16_t display = static_cast<uint16_t>((sa.colorMode ? sa.colorMode : CL32KRGB) | keepFlags);
        const uint16_t spriteMode = sa.spriteMode ? sa.spriteMode : sprPolygon;
        const uint16_t direction = sa.useLight ? UseLight : UseGouraud;
        return SRL::Types::Attribute(
            visibility,
            sortMode,
            No_Texture,
            sa.baseColor,
            gouraud,
            display,
            spriteMode,
            direction);
    };

    for (uint32_t fi = 0; fi < sdrView.header.faceCount; ++fi)
    {
        SegmentDrawReady::Face sf{};
        SegmentDrawReady::AttrBase sa{};
        uint16_t familyId = 0;
        const size_t foff = sdrView.facesOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::Face);
        const size_t aoff = sdrView.attrsOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::AttrBase);
        const size_t ioff = sdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
        if (!SegmentDrawReady::Loader::ReadFaceLeAt(sdrBlob.bytes, foff, sf)) return false;
        if (!SegmentDrawReady::Loader::ReadAttrBaseLeAt(sdrBlob.bytes, aoff, sa)) return false;
        if (!SegmentDrawReady::Loader::ReadFamilyIdLeAt(sdrBlob.bytes, ioff, familyId)) return false;

        const uint16_t srcIdx[4] = { sf.v0, sf.v1, sf.v2, sf.v3 };
        for (size_t i = 0; i < 4; ++i)
        {
            if (static_cast<uint32_t>(srcIdx[i]) >= sdrView.header.vertexCount)
            {
                SRL::Debug::Print(1, 15, "SDR idx bad seg:%03d f:%u i:%u v:%u max:%u",
                                  segmentId,
                                  static_cast<unsigned>(fi),
                                  static_cast<unsigned>(i),
                                  static_cast<unsigned>(srcIdx[i]),
                                  static_cast<unsigned>(sdrView.header.vertexCount));
                return false;
            }
        }
        SRL::Types::Polygon p{};
        for (size_t i = 0; i < 4; ++i)
        {
            const size_t mapped = vertexBase + static_cast<size_t>(srcIdx[i]);
            if (mapped >= static_cast<size_t>(0xFFFF)) return false;
            p.Vertices[i] = static_cast<uint16_t>(mapped);
        }
        p.Normal = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sf.normalX),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalY),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalZ));
        ioFaces.push_back(p);

        ioAttrs.push_back(BuildSdrBaseAttr(sa));

        ioFamilyIds.push_back(familyId);
        ioFaceRankOffsets.push_back(rankOffset);
    }

    return true;
}

// Reorder quad corners from GEO UVs so the SGL textured polygon path sees a stable corner order.
static bool ReorderQuadVerticesFromUv(const SegmentComponent::GeoFace& face, uint16_t outVertices[4])
{
    if (!outVertices) return false;

    int16_t minU = face.u[0];
    int16_t maxU = face.u[0];
    int16_t minV = face.v[0];
    int16_t maxV = face.v[0];
    for (size_t i = 1; i < 4; ++i)
    {
        minU = std::min(minU, face.u[i]);
        maxU = std::max(maxU, face.u[i]);
        minV = std::min(minV, face.v[i]);
        maxV = std::max(maxV, face.v[i]);
    }

    if (minU == maxU || minV == maxV) return false;

    size_t uEdgeCount = 0;
    size_t vEdgeCount = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        const bool onUEdge = (face.u[i] == minU) || (face.u[i] == maxU);
        const bool onVEdge = (face.v[i] == minV) || (face.v[i] == maxV);
        if (!onUEdge || !onVEdge) return false;
        if (face.u[i] == minU || face.u[i] == maxU) ++uEdgeCount;
        if (face.v[i] == minV || face.v[i] == maxV) ++vEdgeCount;
    }
    if (uEdgeCount != 4 || vEdgeCount != 4) return false;

    const int16_t targetU[4] = { minU, maxU, maxU, minU };
    const int16_t targetV[4] = { minV, minV, maxV, maxV };
    bool used[4] = { false, false, false, false };

    for (size_t corner = 0; corner < 4; ++corner)
    {
        int best = -1;
        int32_t bestScore = 0x7FFFFFFF;
        for (size_t src = 0; src < 4; ++src)
        {
            if (used[src]) continue;
            const int32_t du = static_cast<int32_t>(face.u[src]) - static_cast<int32_t>(targetU[corner]);
            const int32_t dv = static_cast<int32_t>(face.v[src]) - static_cast<int32_t>(targetV[corner]);
            const int32_t score = (du < 0 ? -du : du) + (dv < 0 ? -dv : dv);
            if (score < bestScore)
            {
                bestScore = score;
                best = static_cast<int>(src);
            }
        }
        if (best < 0) return false;
        used[best] = true;
        outVertices[corner] = face.vertex[best];
    }

    return true;
}

// Build a stable face normal from the first three corners of the polygon.
static Vector3D BuildFaceNormalFromVerts(const std::vector<SRL::Math::Types::Vector3D>& verts,
                                         const uint16_t indices[4])
{
    if (verts.empty()) return Vector3D(0.0, 0.0, 0.0);
    const size_t ia = static_cast<size_t>(indices[0]);
    const size_t ib = static_cast<size_t>(indices[1]);
    const size_t ic = static_cast<size_t>(indices[2]);
    if (ia >= verts.size() || ib >= verts.size() || ic >= verts.size())
    {
        return Vector3D(0.0, 0.0, 0.0);
    }

    const auto& a = verts[ia];
    const auto& b = verts[ib];
    const auto& c = verts[ic];

    const int64_t abx = static_cast<int64_t>(b.X.RawValue()) - static_cast<int64_t>(a.X.RawValue());
    const int64_t aby = static_cast<int64_t>(b.Y.RawValue()) - static_cast<int64_t>(a.Y.RawValue());
    const int64_t abz = static_cast<int64_t>(b.Z.RawValue()) - static_cast<int64_t>(a.Z.RawValue());
    const int64_t acx = static_cast<int64_t>(c.X.RawValue()) - static_cast<int64_t>(a.X.RawValue());
    const int64_t acy = static_cast<int64_t>(c.Y.RawValue()) - static_cast<int64_t>(a.Y.RawValue());
    const int64_t acz = static_cast<int64_t>(c.Z.RawValue()) - static_cast<int64_t>(a.Z.RawValue());

    const int32_t nx = static_cast<int32_t>(((aby * acz) - (abz * acy)) >> 16);
    const int32_t ny = static_cast<int32_t>(((abz * acx) - (abx * acz)) >> 16);
    const int32_t nz = static_cast<int32_t>(((abx * acy) - (aby * acx)) >> 16);

    return Vector3D(SRL::Math::Types::Fxp::BuildRaw(nx),
                    SRL::Math::Types::Fxp::BuildRaw(ny),
                    SRL::Math::Types::Fxp::BuildRaw(nz));
}

// Build one component renderer and expose its center directly from GEO vertices.
static bool BuildRendererFromGeoMat8(int segmentId, TrackRenderer& renderer, Vector3D* outCenter)
{
    if (BuildRendererFromRdr(segmentId, renderer, outCenter) ||
        BuildRendererFromSdr(segmentId, renderer, outCenter))
    {
        return true;
    }

    SRL::Debug::Print(1, 15, "SEG%03d SDR load fail", segmentId);
    return false;
}
} // namespace

SRL::Math::Types::Vector3D TrackSystem::ComputeRendererCenter(const TrackRenderer& renderer)
{
    return renderer.StartMeshCenter() + renderer.Offset();
}

void TrackSystem::ReleaseRawSegmentCatalog()
{
    for (auto& e : rawSegmentCatalog_)
    {
        if (e.copy.cartPtr)
        {
            SRL::Memory::CartRam::Free(e.copy.cartPtr);
            e.copy.cartPtr = nullptr;
            e.copy.size = 0;
        }
    }
    rawSegmentCatalog_.clear();
}

void TrackSystem::ReleaseSeg1Texbanks()
{
    for (auto& b : seg1Texbanks_)
    {
        b.entries.clear();
        if (b.cartPtr)
        {
            SRL::Memory::CartRam::Free(b.cartPtr);
            b.cartPtr = nullptr;
        }
        b.size = 0;
    }
}

void TrackSystem::ReleaseSeg1TgaCatalog()
{
    for (auto& e : seg1TgaCatalog_)
    {
        if (e.cartPtr)
        {
            SRL::Memory::CartRam::Free(e.cartPtr);
            e.cartPtr = nullptr;
        }
        e.size = 0;
        e.name[0] = '\0';
    }
    seg1TgaCatalog_.clear();
}

static bool IsTgaNameChar(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '-' || c == '.';
}

static bool NameEqualsIgnoreCase(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b)
    {
        char ca = *a;
        char cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
        if (ca != cb) return false;
        ++a; ++b;
    }
    return (*a == '\0' && *b == '\0');
}

namespace
{
static void UpdatePackedAssetCacheTrackedBytes(PackedAssetCache& cache)
{
    const uint32_t current = VectorCapacityBytesSafe(cache.entries);
    if (current == cache.trackedEntryBytes) return;
    if (g_packedAssetCacheEntryBytesLwr >= cache.trackedEntryBytes)
    {
        g_packedAssetCacheEntryBytesLwr -= cache.trackedEntryBytes;
    }
    else
    {
        g_packedAssetCacheEntryBytesLwr = 0;
    }
    g_packedAssetCacheEntryBytesLwr += current;
    cache.trackedEntryBytes = current;
}

static void InvalidateTrackRuntimePackCache(TrackRuntimePackCache& cache)
{
    if (cache.cartPtr)
    {
        SRL::Memory::CartRam::Free(cache.cartPtr);
    }
    cache = {};
}

static void InvalidatePackedAssetCache(PackedAssetCache& cache)
{
    if (cache.cartPtr)
    {
        SRL::Memory::CartRam::Free(cache.cartPtr);
    }
    if (g_packedAssetCacheEntryBytesLwr >= cache.trackedEntryBytes)
    {
        g_packedAssetCacheEntryBytesLwr -= cache.trackedEntryBytes;
    }
    else
    {
        g_packedAssetCacheEntryBytesLwr = 0;
    }
    cache = {};
}

static bool RebuildPackedAssetEntries(PackedAssetCache& cache)
{
    cache.entries.clear();
    UpdatePackedAssetCacheTrackedBytes(cache);
    if (!cache.cartPtr || cache.size < 12) return false;

    const uint8_t* p = static_cast<const uint8_t*>(cache.cartPtr);
    const uint32_t magic = ReadLe32(p + 0);
    const uint32_t version = ReadLe32(p + 4);
    const uint32_t entryCount = ReadLe32(p + 8);
    if (magic != 0x314B4150 || version != 1) return false; // "PAK1"
    if (entryCount == 0) return false;

    const size_t entrySize = 64 + 4 + 4;
    const size_t tableBytes = static_cast<size_t>(entryCount) * entrySize;
    const size_t dataOffset = 12 + tableBytes;
    if (dataOffset > cache.size) return false;

    cache.entries.reserve(entryCount);
    uint32_t minOffset = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < entryCount; ++i)
    {
        const size_t off = 12 + static_cast<size_t>(i) * entrySize;
        if (off + entrySize > cache.size) return false;

        PackedAssetEntryMeta e{};
        ::memcpy(e.name, p + off, 64);
        e.name[64] = '\0';
        e.offset = ReadLe32(p + off + 64);
        e.size = ReadLe32(p + off + 68);
        if (e.size == 0) continue;
        if (e.offset < dataOffset) continue;
        if (static_cast<uint64_t>(e.offset) + static_cast<uint64_t>(e.size) > static_cast<uint64_t>(cache.size)) continue;
        if (e.offset < minOffset) minOffset = e.offset;
        cache.entries.push_back(e);
    }

    if (cache.entries.empty()) return false;
    // Header corruption guard: valid packs should start data immediately after table.
    // Allow a small slack for potential packer alignment/padding.
    if (minOffset > dataOffset)
    {
        const size_t gap = static_cast<size_t>(minOffset - static_cast<uint32_t>(dataOffset));
        if (gap > entrySize)
        {
            cache.entries.clear();
            UpdatePackedAssetCacheTrackedBytes(cache);
            return false;
        }
    }

    UpdatePackedAssetCacheTrackedBytes(cache);
    return !cache.entries.empty();
}

static bool ReadCdFileFully(SRL::Cd::File& file, uint32_t totalBytes, uint8_t* dst, uint32_t& outReadBytes)
{
    outReadBytes = 0;
    if (!dst || totalBytes == 0) return false;

    while (outReadBytes < totalBytes)
    {
        const int32_t toRead = static_cast<int32_t>(totalBytes - outReadBytes);
        if (toRead <= 0) break;
        int32_t got = file.Read(toRead, dst + outReadBytes);
        if (got <= 0) break;
        outReadBytes += static_cast<uint32_t>(got);
    }

    return outReadBytes == totalBytes;
}

static bool LoadTrackRuntimePackToCart(const char* const* candidates, size_t count, TrackRuntimePackCache& cache)
{
    char rememberedPath[96]{};
    if (cache.sourcePath[0] != '\0')
    {
        ::strncpy(rememberedPath, cache.sourcePath, sizeof(rememberedPath) - 1);
        rememberedPath[sizeof(rememberedPath) - 1] = '\0';
    }

    if (cache.cartPtr && cache.size > 0)
    {
        if (TrackRuntimePack::Loader::Parse(cache.cartPtr, cache.size, cache.view))
        {
            return true;
        }
        InvalidateTrackRuntimePackCache(cache);
    }

    const char* foundPath = nullptr;
    SRL::Cd::ChangeDir((const char*)0);
    if (rememberedPath[0] != '\0')
    {
        SRL::Cd::File remembered(rememberedPath);
        if (remembered.Exists() && remembered.Size.Bytes > 0)
        {
            foundPath = rememberedPath;
        }
    }
    for (size_t i = 0; i < count && !foundPath; ++i)
    {
        SRL::Cd::File probe(candidates[i]);
        if (probe.Exists() && probe.Size.Bytes > 0)
        {
            foundPath = candidates[i];
        }
    }
    if (!foundPath) return false;

    SRL::Cd::File file(foundPath);
    if (file.Size.Bytes <= 0) return false;
    if (!file.Open()) return false;

    const uint32_t bytes = static_cast<uint32_t>(file.Size.Bytes);
    void* mem = SRL::Memory::CartRam::Malloc(bytes);
    if (!mem) return false;

    uint32_t readBytes = 0;
    if (!ReadCdFileFully(file, bytes, static_cast<uint8_t*>(mem), readBytes))
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    cache.cartPtr = mem;
    cache.size = readBytes;
    if (!TrackRuntimePack::Loader::Parse(cache.cartPtr, cache.size, cache.view))
    {
        InvalidateTrackRuntimePackCache(cache);
        return false;
    }

    ::strncpy(cache.sourcePath, foundPath, sizeof(cache.sourcePath) - 1);
    cache.sourcePath[sizeof(cache.sourcePath) - 1] = '\0';
    return true;
}

static bool LoadPackedAssetIndexToCart(const char* const* candidates, size_t count, PackedAssetCache& cache)
{
    char rememberedPath[96]{};
    if (cache.sourcePath[0] != '\0')
    {
        ::strncpy(rememberedPath, cache.sourcePath, sizeof(rememberedPath) - 1);
        rememberedPath[sizeof(rememberedPath) - 1] = '\0';
    }

    if (cache.cartPtr && cache.size > 0)
    {
        // Keep previously validated in-memory index for runtime stability.
        if (!cache.entries.empty()) return true;
        if (RebuildPackedAssetEntries(cache)) return true;
        const uint8_t* p = static_cast<const uint8_t*>(cache.cartPtr);
        if (cache.size >= 12 &&
            ReadLe32(p + 0) == 0x314B4150 &&
            ReadLe32(p + 4) == 1u)
        {
            // Keep raw buffer alive even when vector index rebuild failed.
            // Callers with raw-table fallback can still resolve entries.
            return true;
        }

        // Cart-only recovery path: try to refresh the existing Cart RAM block
        // before freeing/reallocating, minimizing fragmentation and stalls.
        SRL::Cd::ChangeDir((const char*)0);
        if (cache.sourcePath[0] != '\0')
        {
            SRL::Cd::File rf(cache.sourcePath);
            if (rf.Exists() && rf.Size.Bytes > 0 &&
                static_cast<uint64_t>(rf.Size.Bytes) <= static_cast<uint64_t>(cache.size) &&
                rf.Open())
            {
                const uint32_t bytes = static_cast<uint32_t>(rf.Size.Bytes);
                uint32_t readBytes = 0;
                if (ReadCdFileFully(rf, bytes, static_cast<uint8_t*>(cache.cartPtr), readBytes))
                {
                    cache.size = readBytes;
                    if (RebuildPackedAssetEntries(cache)) return true;
                    const uint8_t* rp = static_cast<const uint8_t*>(cache.cartPtr);
                    if (cache.size >= 12 &&
                        ReadLe32(rp + 0) == 0x314B4150 &&
                        ReadLe32(rp + 4) == 1u)
                    {
                        return true;
                    }
                }
            }
        }
        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::File rf(candidates[i]);
            if (!rf.Exists() || rf.Size.Bytes <= 0) continue;
            if (static_cast<uint64_t>(rf.Size.Bytes) > static_cast<uint64_t>(cache.size)) continue;
            if (!rf.Open()) continue;

            const uint32_t bytes = static_cast<uint32_t>(rf.Size.Bytes);
            uint32_t readBytes = 0;
            if (!ReadCdFileFully(rf, bytes, static_cast<uint8_t*>(cache.cartPtr), readBytes)) continue;
            cache.size = readBytes;
            if (RebuildPackedAssetEntries(cache))
            {
                ::strncpy(cache.sourcePath, candidates[i], sizeof(cache.sourcePath) - 1);
                cache.sourcePath[sizeof(cache.sourcePath) - 1] = '\0';
                return true;
            }

            const uint8_t* rp = static_cast<const uint8_t*>(cache.cartPtr);
            if (cache.size >= 12 &&
                ReadLe32(rp + 0) == 0x314B4150 &&
                ReadLe32(rp + 4) == 1u)
            {
                return true;
            }
        }
        InvalidatePackedAssetCache(cache);
    }

    char pinnedPath[96]{};
    if (rememberedPath[0] != '\0')
    {
        ::strncpy(pinnedPath, rememberedPath, sizeof(pinnedPath) - 1);
        pinnedPath[sizeof(pinnedPath) - 1] = '\0';
    }
    cache = {};
    if (pinnedPath[0] != '\0')
    {
        ::strncpy(cache.sourcePath, pinnedPath, sizeof(cache.sourcePath) - 1);
        cache.sourcePath[sizeof(cache.sourcePath) - 1] = '\0';
    }
    SRL::Cd::ChangeDir((const char*)0);
    const char* foundPath = nullptr;
    if (cache.sourcePath[0] != '\0')
    {
        SRL::Cd::File pinnedProbe(cache.sourcePath);
        if (pinnedProbe.Exists() && pinnedProbe.Size.Bytes > 0)
        {
            foundPath = cache.sourcePath;
        }
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (foundPath) break;
        SRL::Cd::File probe(candidates[i]);
        if (probe.Exists() && probe.Size.Bytes > 0)
        {
            foundPath = candidates[i];
            break;
        }
    }
    if (!foundPath) return false;

    SRL::Cd::File f(foundPath);
    if (f.Size.Bytes <= 0) return false;
    if (!f.Open()) return false;

    const uint32_t bytes = static_cast<uint32_t>(f.Size.Bytes);
    void* mem = SRL::Memory::CartRam::Malloc(bytes);
    if (!mem) return false;

    uint32_t readBytes = 0;
    if (!ReadCdFileFully(f, bytes, static_cast<uint8_t*>(mem), readBytes))
    {
        SRL::Debug::Print(1, 15, "PAK read short %s got:%u exp:%u",
                          foundPath ? foundPath : "?",
                          static_cast<unsigned>(readBytes),
                          static_cast<unsigned>(bytes));
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    cache.cartPtr = mem;
    cache.size = readBytes;
    if (RebuildPackedAssetEntries(cache))
    {
        if (foundPath && foundPath[0] != '\0')
        {
            ::strncpy(cache.sourcePath, foundPath, sizeof(cache.sourcePath) - 1);
            cache.sourcePath[sizeof(cache.sourcePath) - 1] = '\0';
        }
        return true;
    }

    const uint8_t* p = static_cast<const uint8_t*>(cache.cartPtr);
    if (cache.size >= 12 &&
        ReadLe32(p + 0) == 0x314B4150 &&
        ReadLe32(p + 4) == 1u)
    {
        if (foundPath && foundPath[0] != '\0')
        {
            ::strncpy(cache.sourcePath, foundPath, sizeof(cache.sourcePath) - 1);
            cache.sourcePath[sizeof(cache.sourcePath) - 1] = '\0';
        }
        return true;
    }

    InvalidatePackedAssetCache(cache);
    return false;
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentComponent::Blob& out)
{
    out.loaded = false;
    out.size = 0;
    out.bytes.clear();
    if (!cache.cartPtr || cache.size == 0 || cache.entries.empty() || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        out.bytes.resize(e.size);
        ::memcpy(out.bytes.data(), src, e.size);
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    return false;
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentDrawReady::Blob& out)
{
    out.loaded = false;
    out.size = 0;
    out.bytes.clear();
    if (!cache.cartPtr || cache.size == 0 || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        out.bytes.resize(e.size);
        ::memcpy(out.bytes.data(), src, e.size);
        if (out.bytes.size() < sizeof(SegmentDrawReady::HeaderV1)) return false;
        if (ReadLe32(out.bytes.data()) != SegmentDrawReady::kMagicSdr1) return false;
        if (ReadLe16(out.bytes.data() + 4) != SegmentDrawReady::kVersion1) return false;
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    // Fallback path: scan raw PAK table directly.
    // This avoids runtime dependence on the in-memory vector index.
    const uint8_t* p = static_cast<const uint8_t*>(cache.cartPtr);
    if (cache.size < 12) return false;
    const uint32_t magic = ReadLe32(p + 0);
    const uint32_t version = ReadLe32(p + 4);
    const uint32_t entryCount = ReadLe32(p + 8);
    if (magic != 0x314B4150 || version != 1) return false; // "PAK1"

    const size_t entrySize = 64 + 4 + 4;
    const size_t tableBytes = static_cast<size_t>(entryCount) * entrySize;
    const size_t dataOffset = 12 + tableBytes;
    if (dataOffset > cache.size) return false;

    for (uint32_t i = 0; i < entryCount; ++i)
    {
        const size_t off = 12 + static_cast<size_t>(i) * entrySize;
        if (off + entrySize > cache.size) return false;

        char name[65]{};
        ::memcpy(name, p + off, 64);
        name[64] = '\0';
        if (!NameEqualsIgnoreCase(name, entryName)) continue;

        const uint32_t entryOffset = ReadLe32(p + off + 64);
        const uint32_t entrySizeBytes = ReadLe32(p + off + 68);
        if (entrySizeBytes == 0) return false;
        if (entryOffset < dataOffset) return false;
        if (static_cast<uint64_t>(entryOffset) + static_cast<uint64_t>(entrySizeBytes) > static_cast<uint64_t>(cache.size))
        {
            return false;
        }

        const uint8_t* src = p + entryOffset;
        out.bytes.resize(entrySizeBytes);
        ::memcpy(out.bytes.data(), src, entrySizeBytes);
        if (out.bytes.size() < sizeof(SegmentDrawReady::HeaderV1)) return false;
        if (ReadLe32(out.bytes.data()) != SegmentDrawReady::kMagicSdr1) return false;
        if (ReadLe16(out.bytes.data() + 4) != SegmentDrawReady::kVersion1) return false;
        out.loaded = true;
        out.size = out.bytes.size();
        SRL::Debug::Print(1, 15, "SDR raw idx hit %s", entryName);
        return true;
    }

    // Last-resort cart-only fallback: scan the table area by fixed record stride
    // and match the entry name directly, even when header entryCount is corrupted.
    const size_t bruteLimit = std::min<size_t>(cache.size, 256u * 1024u);
    for (size_t off = 12; (off + entrySize) <= bruteLimit; off += entrySize)
    {
        char name[65]{};
        ::memcpy(name, p + off, 64);
        name[64] = '\0';
        if (!NameEqualsIgnoreCase(name, entryName)) continue;

        const uint32_t entryOffset = ReadLe32(p + off + 64);
        const uint32_t entrySizeBytes = ReadLe32(p + off + 68);
        if (entrySizeBytes == 0) return false;
        if (entryOffset >= cache.size) return false;
        if (static_cast<uint64_t>(entryOffset) + static_cast<uint64_t>(entrySizeBytes) > static_cast<uint64_t>(cache.size))
        {
            return false;
        }

        const uint8_t* src = p + entryOffset;
        out.bytes.resize(entrySizeBytes);
        ::memcpy(out.bytes.data(), src, entrySizeBytes);
        if (out.bytes.size() < sizeof(SegmentDrawReady::HeaderV1)) return false;
        if (ReadLe32(out.bytes.data()) != SegmentDrawReady::kMagicSdr1) return false;
        if (ReadLe16(out.bytes.data() + 4) != SegmentDrawReady::kVersion1) return false;
        out.loaded = true;
        out.size = out.bytes.size();
        SRL::Debug::Print(1, 15, "SDR brute idx hit %s", entryName);
        return true;
    }

    return false;
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentRuntimeDraw::Blob& out)
{
    out.loaded = false;
    out.size = 0;
    out.bytes.clear();
    if (!cache.cartPtr || cache.size == 0 || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        out.bytes.resize(e.size);
        ::memcpy(out.bytes.data(), src, e.size);
        if (out.bytes.size() < sizeof(SegmentRuntimeDraw::HeaderV1)) return false;
        if (ReadLe32(out.bytes.data()) != SegmentRuntimeDraw::kMagicRdr1) return false;
        if (ReadLe16(out.bytes.data() + 4) != SegmentRuntimeDraw::kVersion1) return false;
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    const uint8_t* p = static_cast<const uint8_t*>(cache.cartPtr);
    if (cache.size < 12) return false;
    const uint32_t magic = ReadLe32(p + 0);
    const uint32_t version = ReadLe32(p + 4);
    const uint32_t entryCount = ReadLe32(p + 8);
    if (magic != 0x314B4150 || version != 1u || entryCount == 0u) return false;

    const size_t entrySize = 64 + 4 + 4;
    const size_t tableBytes = static_cast<size_t>(entryCount) * entrySize;
    const size_t dataOffset = 12 + tableBytes;
    if (dataOffset > cache.size) return false;

    for (uint32_t i = 0; i < entryCount; ++i)
    {
        const size_t off = 12 + static_cast<size_t>(i) * entrySize;
        if (off + entrySize > cache.size) return false;

        char name[65]{};
        ::memcpy(name, p + off, 64);
        name[64] = '\0';
        if (!NameEqualsIgnoreCase(name, entryName)) continue;

        const uint32_t entryOffset = ReadLe32(p + off + 64);
        const uint32_t entrySizeBytes = ReadLe32(p + off + 68);
        if (entrySizeBytes == 0) return false;
        if (entryOffset < dataOffset) return false;
        if (static_cast<uint64_t>(entryOffset) + static_cast<uint64_t>(entrySizeBytes) > static_cast<uint64_t>(cache.size))
        {
            return false;
        }

        const uint8_t* src = p + entryOffset;
        out.bytes.resize(entrySizeBytes);
        ::memcpy(out.bytes.data(), src, entrySizeBytes);
        if (out.bytes.size() < sizeof(SegmentRuntimeDraw::HeaderV1)) return false;
        if (ReadLe32(out.bytes.data()) != SegmentRuntimeDraw::kMagicRdr1) return false;
        if (ReadLe16(out.bytes.data() + 4) != SegmentRuntimeDraw::kVersion1) return false;
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    return false;
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, BatchDrawReady::Blob& out)
{
    out.loaded = false;
    out.size = 0;
    out.bytes.clear();
    if (!cache.cartPtr || cache.size == 0 || cache.entries.empty() || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        out.bytes.resize(e.size);
        ::memcpy(out.bytes.data(), src, e.size);
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    return false;
}
} // namespace

static void BuildIso83UpperName(const char* inName, char* outName, size_t outSize)
{
    if (!outName || outSize == 0) return;
    outName[0] = '\0';
    if (!inName || inName[0] == '\0') return;

    char upper[80]{};
    ::strncpy(upper, inName, sizeof(upper) - 1);
    for (size_t i = 0; upper[i] != '\0'; ++i)
    {
        if (upper[i] >= 'a' && upper[i] <= 'z') upper[i] = static_cast<char>(upper[i] - 'a' + 'A');
    }
    const char* dot = ::strrchr(upper, '.');
    if (!dot)
    {
        ::strncpy(outName, upper, outSize - 1);
        return;
    }

    char base[16]{};
    size_t baseLen = static_cast<size_t>(dot - upper);
    if (baseLen > 8) baseLen = 8;
    for (size_t i = 0; i < baseLen; ++i) base[i] = upper[i];
    base[baseLen] = '\0';

    const char* ext = dot + 1;
    char ext3[8]{};
    size_t e = 0;
    while (ext[e] != '\0' && e < 3) { ext3[e] = ext[e]; ++e; }
    ext3[e] = '\0';

    if (base[0] != '\0' && ext3[0] != '\0')
    {
        std::snprintf(outName, outSize, "%s.%s", base, ext3);
    }
}

bool TrackSystem::PreloadTgaCatalogFromSegmentsMap()
{
    ReleaseSeg1TgaCatalog();
    g_seg1MapCache = {};
    g_seg1MapCacheValid = false;
    seg1TgaPreloadCount_ = 0;
    seg1TgaAttemptCount_ = 0;
    seg1TgaFailCount_ = 0;
    seg1TgaJsonOk_ = 0;
    ::strncpy(g_tgaLastTry, "none", sizeof(g_tgaLastTry) - 1);
    g_tgaLastTry[sizeof(g_tgaLastTry) - 1] = '\0';
    ::strncpy(g_tgaLastResult, "preload_start", sizeof(g_tgaLastResult) - 1);
    g_tgaLastResult[sizeof(g_tgaLastResult) - 1] = '\0';
    ::strncpy(g_tgaLastName, "none", sizeof(g_tgaLastName) - 1);
    g_tgaLastName[sizeof(g_tgaLastName) - 1] = '\0';
    g_smapBytes = 0;
    ::strncpy(g_smapSig, "none", sizeof(g_smapSig) - 1);
    g_smapSig[sizeof(g_smapSig) - 1] = '\0';
    ::strncpy(g_smapHead, "none", sizeof(g_smapHead) - 1);
    g_smapHead[sizeof(g_smapHead) - 1] = '\0';

    auto loadNameToCart = [&](const char* inName) -> bool
    {
        if (!inName || inName[0] == '\0') return false;

        char name[64]{};
        NormalizeTextureFileName(inName, name, sizeof(name));
        if (name[0] == '\0') return false;
        ::strncpy(g_tgaLastName, name, sizeof(g_tgaLastName) - 1);
        g_tgaLastName[sizeof(g_tgaLastName) - 1] = '\0';

        bool already = false;
        for (const auto& ex : seg1TgaCatalog_)
        {
            if (NameEqualsIgnoreCase(ex.name, name)) { already = true; break; }
        }
        if (already) return true;

        ++seg1TgaAttemptCount_;
        char upper[64]{};
        ::strncpy(upper, name, sizeof(upper) - 1);
        for (size_t i = 0; upper[i] != '\0'; ++i)
        {
            if (upper[i] >= 'a' && upper[i] <= 'z') upper[i] = static_cast<char>(upper[i] - 'a' + 'A');
        }

        char iso83[80]{};
        BuildIso83UpperName(upper, iso83, sizeof(iso83));

        char c0[96]{}, c1[96]{}, c2[96]{}, c3[96]{}, c4[80]{}, c5[80]{};
        char u0[96]{}, u1[96]{}, u2[96]{}, u3[96]{}, u4[80]{}, u5[80]{};
        char i0[96]{}, i1[96]{}, i2[96]{}, i3[96]{}, i4[80]{}, i5[80]{};
        std::snprintf(c0, sizeof(c0), "CD/DATA/%s", name);
        std::snprintf(c1, sizeof(c1), "CD/DATA/%s;1", name);
        std::snprintf(c2, sizeof(c2), "DATA/%s", name);
        std::snprintf(c3, sizeof(c3), "DATA/%s;1", name);
        std::snprintf(c4, sizeof(c4), "%s", name);
        std::snprintf(c5, sizeof(c5), "%s;1", name);
        std::snprintf(u0, sizeof(u0), "CD/DATA/%s", upper);
        std::snprintf(u1, sizeof(u1), "CD/DATA/%s;1", upper);
        std::snprintf(u2, sizeof(u2), "DATA/%s", upper);
        std::snprintf(u3, sizeof(u3), "DATA/%s;1", upper);
        std::snprintf(u4, sizeof(u4), "%s", upper);
        std::snprintf(u5, sizeof(u5), "%s;1", upper);
        if (iso83[0] != '\0')
        {
            std::snprintf(i0, sizeof(i0), "CD/DATA/%s", iso83);
            std::snprintf(i1, sizeof(i1), "CD/DATA/%s;1", iso83);
            std::snprintf(i2, sizeof(i2), "DATA/%s", iso83);
            std::snprintf(i3, sizeof(i3), "DATA/%s;1", iso83);
            std::snprintf(i4, sizeof(i4), "%s", iso83);
            std::snprintf(i5, sizeof(i5), "%s;1", iso83);
        }
        const char* paths[] = { c0, c1, c2, c3, c4, c5, u0, u1, u2, u3, u4, u5, i0, i1, i2, i3, i4, i5 };
        const char* loadedPath = nullptr;
        for (size_t p = 0; p < sizeof(paths) / sizeof(paths[0]); ++p)
        {
            ::strncpy(g_tgaLastTry, paths[p], sizeof(g_tgaLastTry) - 1);
            g_tgaLastTry[sizeof(g_tgaLastTry) - 1] = '\0';
            SRL::Debug::Print(1, 26, "TGA cart try:%s", paths[p]);
            SRL::Cd::File f(paths[p]);
            if (!f.Exists() || f.Size.Bytes <= 0 || !f.Open()) continue;
            const uint32_t bytes = static_cast<uint32_t>(f.Size.Bytes);
            void* mem = SRL::Memory::CartRam::Malloc(bytes);
            if (!mem) continue;
            const int32_t read = f.Read(static_cast<int32_t>(bytes), mem);
            if (read <= 0 || static_cast<uint32_t>(read) > bytes)
            {
                SRL::Memory::CartRam::Free(mem);
                continue;
            }
            Seg1TgaCartEntry e{};
            ::strncpy(e.name, name, sizeof(e.name) - 1);
            e.cartPtr = mem;
            e.size = static_cast<uint32_t>(read);
            seg1TgaCatalog_.push_back(e);
            seg1TgaPreloadCount_ = static_cast<uint16_t>(seg1TgaCatalog_.size());
            loadedPath = paths[p];
            ::strncpy(g_tgaLastResult, "ok", sizeof(g_tgaLastResult) - 1);
            g_tgaLastResult[sizeof(g_tgaLastResult) - 1] = '\0';
            SRL::Debug::Print(1, 27, "TGA cart ok:%s path:%s size:%u", name, loadedPath, e.size);
            return true;
        }
        ++seg1TgaFailCount_;
        std::snprintf(g_tgaLastResult, sizeof(g_tgaLastResult), "fail:%s", name);
        SRL::Debug::Print(1, 27, "TGA cart fail:%s", name);
        return false;
    };

    // Preferred source: deterministic rename map used by runtime lookup.
    const char* renCandidates[] = {
        "CD/DATA/RTMAP.TXT",
        "CD/DATA/RTMAP.TXT;1",
        "DATA/RTMAP.TXT",
        "DATA/RTMAP.TXT;1",
        "RTMAP.TXT",
        "RTMAP.TXT;1",
        "CD/DATA/ren_textures_copy_map.json",
        "CD/DATA/ren_textures_copy_map.json;1",
        "DATA/ren_textures_copy_map.json",
        "DATA/ren_textures_copy_map.json;1",
        "CD/DATA/REN_TEXTURES_COPY_MAP.JSON",
        "CD/DATA/REN_TEXTURES_COPY_MAP.JSON;1",
        "DATA/REN_TEXTURES_COPY_MAP.JSON",
        "DATA/REN_TEXTURES_COPY_MAP.JSON;1",
        "ren_textures_copy_map.json",
        "ren_textures_copy_map.json;1"
    };
    std::vector<char> renText{};
    RenTextureMap renMap{};
    if (ReadCdFileText(renCandidates, sizeof(renCandidates) / sizeof(renCandidates[0]), renText) &&
        ParseRenTextureCopyMap(renText.data(), renMap))
    {
        seg1TgaJsonOk_ = 2;
        for (size_t i = 0; i < renMap.entries.size(); ++i)
        {
            (void)loadNameToCart(renMap.entries[i].targetName);
        }
        std::vector<char> smapText{};
        const char* smapCandidates[] = {
            "SMAP.TXT",
            "SMAP.TXT;1",
            "CD/DATA/SMAP.TXT",
            "CD/DATA/SMAP.TXT;1",
            "DATA/SMAP.TXT",
            "DATA/SMAP.TXT;1"
        };
        if (ReadCdFileText(smapCandidates, sizeof(smapCandidates) / sizeof(smapCandidates[0]), smapText))
        {
            g_seg1MapCache = {};
            g_seg1MapCacheValid = ParseSegment1TextureJson(smapText.data(), g_seg1MapCache);
            if (g_seg1MapCacheValid)
            {
                g_smapBytes = static_cast<uint32_t>(smapText.size());
                const uint8_t b0 = (smapText.size() > 0) ? static_cast<uint8_t>(smapText[0]) : 0;
                const uint8_t b1 = (smapText.size() > 1) ? static_cast<uint8_t>(smapText[1]) : 0;
                const uint8_t b2 = (smapText.size() > 2) ? static_cast<uint8_t>(smapText[2]) : 0;
                const uint8_t b3 = (smapText.size() > 3) ? static_cast<uint8_t>(smapText[3]) : 0;
                std::snprintf(g_smapSig, sizeof(g_smapSig), "%02X%02X%02X%02X", b0, b1, b2, b3);
                const size_t hn = (smapText.size() > 40) ? 40 : smapText.size();
                char head[48]{};
                if (hn > 0)
                {
                    memcpy(head, smapText.data(), hn);
                    head[hn] = '\0';
                    for (size_t hi = 0; head[hi] != '\0'; ++hi)
                    {
                        if (head[hi] < 32 || head[hi] > 126) head[hi] = '.';
                    }
                }
                ::strncpy(g_smapHead, head, sizeof(g_smapHead) - 1);
                g_smapHead[sizeof(g_smapHead) - 1] = '\0';
            }
        }
        return !seg1TgaCatalog_.empty();
    }

    const char* jsonCandidates[] = {
     //   "CD/DATA/segments_map.json",
     //   "CD/DATA/segments_map.json;1",
     //   "DATA/segments_map.json",
       // "DATA/segments_map.json;1",
     //   "CD/DATA/SEGMENTS_MAP.JSON",
     //   "CD/DATA/SEGMENTS_MAP.JSON;1",
    //    "DATA/SEGMENTS_MAP.JSON",
     //   "DATA/SEGMENTS_MAP.JSON;1",
     //   "SEGMENTS_MAP.JSON",
      //  "SEGMENTS_MAP.JSON;1",
      //  "segments_map.json",
      //  "segments_map.json;1",
      //  "SEG_MAP.TXT",
      // "SEGMAP.TXT",
        "SMAP.TXT",
        "CD/DATA/SMAP.TXT",
        "CD/DATA/SMAP.TXT;1",
        "DATA/SMAP.TXT",
        "DATA/SMAP.TXT;1",
        "SMAP.TXT;1"
       // "CD/DATA/SEGMAP.TXT",
       // "CD/DATA/SEGMAP.TXT;1",
      //  "DATA/SEGMAP.TXT",
      //  "DATA/SEGMAP.TXT;1",
      //  "CD/DATA/SEG_MAP.TXT",
      //  "CD/DATA/SEG_MAP.TXT;1",
      //  "DATA/SEG_MAP.TXT",
      //  "DATA/SEG_MAP.TXT;1"
    };

    std::vector<char> jsonText{};
    const bool cdLoaded = ReadCdFileText(jsonCandidates, sizeof(jsonCandidates) / sizeof(jsonCandidates[0]), jsonText);
    if (!cdLoaded && !ReadLocalSegmentsMap(jsonText))
    {
        ::strncpy(g_tgaLastResult, "map_open_fail", sizeof(g_tgaLastResult) - 1);
        g_tgaLastResult[sizeof(g_tgaLastResult) - 1] = '\0';
        SRL::Debug::Print(1, 6, "TGA map json fail");
        return false;
    }
    if (!cdLoaded)
    {
        SRL::Debug::Print(1, 6, "TGA map json load fallback local");
    }
    seg1TgaJsonOk_ = cdLoaded ? 1 : 3;
    std::snprintf(g_tgaLastResult, sizeof(g_tgaLastResult), "map_ok bytes:%u", (unsigned)jsonText.size());
    g_smapBytes = static_cast<uint32_t>(jsonText.size());
    {
        const uint8_t b0 = (jsonText.size() > 0) ? static_cast<uint8_t>(jsonText[0]) : 0;
        const uint8_t b1 = (jsonText.size() > 1) ? static_cast<uint8_t>(jsonText[1]) : 0;
        const uint8_t b2 = (jsonText.size() > 2) ? static_cast<uint8_t>(jsonText[2]) : 0;
        const uint8_t b3 = (jsonText.size() > 3) ? static_cast<uint8_t>(jsonText[3]) : 0;
        std::snprintf(g_smapSig, sizeof(g_smapSig), "%02X%02X%02X%02X", b0, b1, b2, b3);
    }
    {
        char head[48]{};
        const size_t hn = (jsonText.size() > 40) ? 40 : jsonText.size();
        if (hn > 0)
        {
            memcpy(head, jsonText.data(), hn);
            head[hn] = '\0';
            for (size_t i = 0; head[i] != '\0'; ++i)
            {
                if (head[i] < 32 || head[i] > 126) head[i] = '.';
            }
        }
        ::strncpy(g_smapHead, head, sizeof(g_smapHead) - 1);
        g_smapHead[sizeof(g_smapHead) - 1] = '\0';
        SRL::Debug::Print(1, 24, "SMAP head:%s", g_smapHead);
    }

    char variantNames[1024][64]{};
    const size_t variantCount = CollectAllVariantTextureNames(jsonText.data(), variantNames, 1024);
    g_seg1MapCacheValid = ParseSegment1TextureJson(jsonText.data(), g_seg1MapCache);
    size_t extracted = 0;
    if (variantCount > 0)
    {
        for (size_t i = 0; i < variantCount; ++i)
        {
            ::strncpy(g_tgaLastName, variantNames[i], sizeof(g_tgaLastName) - 1);
            g_tgaLastName[sizeof(g_tgaLastName) - 1] = '\0';
            if (loadNameToCart(variantNames[i])) ++extracted;
        }
        std::snprintf(g_tgaLastResult, sizeof(g_tgaLastResult), "variants:%u ok:%u",
                      (unsigned)variantCount, (unsigned)extracted);
        SRL::Debug::Print(1, 8, "TGA pre ok:%u", (unsigned)extracted);
        seg1TgaPreloadCount_ = static_cast<uint16_t>(seg1TgaCatalog_.size());
        return !seg1TgaCatalog_.empty();
    }

    const char* s = jsonText.data();
    size_t tokenHits = 0;
    char firstToken[64]{};
    while (s && *s)
    {
        const char* p = strstr(s, ".tga");
        if (!p) p = strstr(s, ".TGA");
        if (!p) break;

        const char* b = p;
        while (b > jsonText.data() && IsTgaNameChar(*(b - 1))) --b;

        char name[64]{};
        size_t n = static_cast<size_t>((p - b) + 4);
        if (n >= sizeof(name)) n = sizeof(name) - 1;
        memcpy(name, b, n);
        name[n] = '\0';
        ::strncpy(g_tgaLastName, name, sizeof(g_tgaLastName) - 1);
        g_tgaLastName[sizeof(g_tgaLastName) - 1] = '\0';
        ++tokenHits;
        if (firstToken[0] == '\0')
        {
            ::strncpy(firstToken, name, sizeof(firstToken) - 1);
            firstToken[sizeof(firstToken) - 1] = '\0';
        }

        bool already = false;
        for (const auto& ex : seg1TgaCatalog_)
        {
            if (NameEqualsIgnoreCase(ex.name, name)) { already = true; break; }
        }
        if (name[0] != '\0' && !already)
        {
            if (loadNameToCart(name))
            {
                ++extracted;
            }
            else
            {
                SRL::Debug::Print(1, 7, "TGA miss:%s", name);
            }
        }
        s = p + 4;
    }

    if (tokenHits == 0)
    {
        ::strncpy(g_tgaLastResult, "no_tga_tokens", sizeof(g_tgaLastResult) - 1);
        g_tgaLastResult[sizeof(g_tgaLastResult) - 1] = '\0';
    }
    else
    {
        std::snprintf(g_tgaLastResult, sizeof(g_tgaLastResult), "tokens:%u first:%s ok:%u",
                      (unsigned)tokenHits, firstToken, (unsigned)extracted);
    }
    SRL::Debug::Print(1, 8, "TGA pre ok:%u", (unsigned)extracted);
    seg1TgaPreloadCount_ = static_cast<uint16_t>(seg1TgaCatalog_.size());
    return !seg1TgaCatalog_.empty();
}

bool TrackSystem::BuildSeg1TexbankCandidatePaths(int lodValue,
                                                 std::array<std::array<char, 40>, 16>& storage,
                                                 const char** outCandidates,
                                                 size_t& outCount)
{
    if (!outCandidates) return false;

    outCount = 0;
    auto addCandidate = [&](const char* fmt)
    {
        if (outCount >= storage.size()) return;
        std::snprintf(storage[outCount].data(), storage[outCount].size(), fmt, lodValue);
        outCandidates[outCount] = storage[outCount].data();
        ++outCount;
    };

    addCandidate("CD/DATA/TEXBANK_%d.BIN");
    addCandidate("CD/DATA/TEXBANK_%d.BIN;1");
    addCandidate("DATA/TEXBANK_%d.BIN");
    addCandidate("DATA/TEXBANK_%d.BIN;1");
    addCandidate("TEXBANK_%d.BIN");
    addCandidate("TEXBANK_%d.BIN;1");
    addCandidate("texbank_%d.bin");
    addCandidate("texbank_%d.bin;1");
    addCandidate("CD/DATA/TBK%d.BIN");
    addCandidate("CD/DATA/TBK%d.BIN;1");
    addCandidate("DATA/TBK%d.BIN");
    addCandidate("DATA/TBK%d.BIN;1");
    addCandidate("TBK%d.BIN");
    addCandidate("TBK%d.BIN;1");
    addCandidate("tbk%d.bin");
    addCandidate("tbk%d.bin;1");
    return outCount > 0;
}

bool TrackSystem::LoadSeg1TexbankIndexToCart(size_t lodIndex, int lodValue)
{
    if (lodIndex >= seg1Texbanks_.size()) return false;
    auto& bank = seg1Texbanks_[lodIndex];
    if (bank.lod == lodValue && bank.cartPtr && bank.size > 0 && !bank.entries.empty()) return true;

    if (bank.cartPtr)
    {
        SRL::Memory::CartRam::Free(bank.cartPtr);
    }
    bank = {};
    bank.lod = lodValue;

    std::array<std::array<char, 40>, 16> candidateStorage{};
    const char* cands[16]{};
    size_t candCount = 0;
    if (!BuildSeg1TexbankCandidatePaths(bank.lod, candidateStorage, cands, candCount)) return false;

    const char* foundPath = nullptr;
    for (size_t i = 0; i < candCount; ++i)
    {
        SRL::Cd::File probe(cands[i]);
        if (probe.Exists() && probe.Size.Bytes > 0)
        {
            foundPath = cands[i];
            break;
        }
    }
    if (!foundPath) return false;
    SRL::Cd::File f(foundPath);
    if (f.Size.Bytes <= 0) return false;
    if (!f.Open()) return false;

    const uint32_t bytes = static_cast<uint32_t>(f.Size.Bytes);
    void* mem = SRL::Memory::CartRam::Malloc(bytes);
    if (!mem) return false;
    const int32_t read = f.Read(static_cast<int32_t>(bytes), mem);
    if (read <= 0 || static_cast<uint32_t>(read) > bytes)
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    const uint32_t readBytes = static_cast<uint32_t>(read);
    const uint8_t* p = static_cast<const uint8_t*>(mem);
    if (readBytes < 20)
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }
    const uint32_t magic = ReadLe32(p + 0);
    const uint16_t ver = ReadLe16(p + 4);
    const uint16_t lod = ReadLe16(p + 6);
    const uint32_t count = ReadLe32(p + 8);
    const uint32_t dataOff = ReadLe32(p + 12);
    (void)ver;
    if (magic != 0x314B4254 || lod != static_cast<uint16_t>(bank.lod))
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }
    const uint32_t entryBase = 20;
    const uint32_t entrySize = 16;
    if (readBytes < entryBase)
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }
    const uint32_t entrySpan = readBytes - entryBase;
    if (count > (entrySpan / entrySize))
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }
    if (dataOff > readBytes)
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    TrackLowWorkVector<Seg1TexbankEntry> parsedEntries{};
    parsedEntries.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t o = entryBase + (i * entrySize);
        Seg1TexbankEntry e{};
        e.familyId = static_cast<uint16_t>(ReadLe32(p + o + 0));
        e.offset = ReadLe32(p + o + 4);
        e.size = ReadLe32(p + o + 8);
        if (e.offset <= readBytes && e.size <= (readBytes - e.offset))
        {
            parsedEntries.push_back(e);
        }
    }
    if (parsedEntries.empty())
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    bank.cartPtr = mem;
    bank.size = readBytes;
    bank.entries = std::move(parsedEntries);
    return true;
}

const TrackSegmentCopy* TrackSystem::FindRawSegmentCopyById(int id) const
{
    for (const auto& e : rawSegmentCatalog_)
    {
        if (e.id == id && e.copy.cartPtr && e.copy.size > 0) return &e.copy;
    }
    return nullptr;
}

const char* TrackSystem::FindExistingPath(const char* const* paths, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        SRL::Cd::File f(paths[i]);
        const bool exists = f.Exists() && f.Size.Bytes > 0;
        ::strncpy(lastSegmentPath_, paths[i], sizeof(lastSegmentPath_));
        lastSegmentPath_[sizeof(lastSegmentPath_) - 1] = '\0';
        SRL::Debug::Print(1, 6, "Check cd path: %s -> %d", paths[i], exists ? 1 : 0);
        if (exists) return paths[i];
    }
    return nullptr;
}

const char* TrackSystem::ResolveSegmentPath(size_t id)
{
    constexpr size_t variantCount = kSegmentPathTemplates_.size();
    std::array<std::array<char, 64>, variantCount> buffers{};
    const char* candidates[variantCount]{};

    for (size_t i = 0; i < variantCount; ++i)
    {
        std::snprintf(buffers[i].data(), buffers[i].size(), kSegmentPathTemplates_[i], unsigned(id));
        candidates[i] = buffers[i].data();
    }
    return FindExistingPath(candidates, variantCount);
}

TrackSegmentCopy TrackSystem::CopySegmentById(size_t id)
{
    TrackSegmentCopy copy{};

    char upperName[32]{};
    char upperNameV[32]{};
    char lowerName[32]{};
    char lowerNameV[32]{};
    std::snprintf(upperName, sizeof(upperName), "SEG_%03u.NYA", unsigned(id));
    std::snprintf(upperNameV, sizeof(upperNameV), "SEG_%03u.NYA;1", unsigned(id));
    std::snprintf(lowerName, sizeof(lowerName), "seg_%03u.nya", unsigned(id));
    std::snprintf(lowerNameV, sizeof(lowerNameV), "seg_%03u.nya;1", unsigned(id));
    const char* names[] = { upperName, upperNameV, lowerName, lowerNameV };
    const size_t namesCount = sizeof(names) / sizeof(names[0]);
    struct DirChain { const char* a; const char* b; };
    const DirChain dirChains[] = {
        { "DATA", nullptr },
        { "data", nullptr },
        { nullptr, nullptr },
        { "DATA", "SETORES" },
        { "data", "setores" },
        { "SETORES", nullptr },
        { "setores", nullptr },
        { nullptr, nullptr }
    };

    for (const auto& chain : dirChains)
    {
        SRL::Cd::ChangeDir((const char*)0);
        if (chain.a) SRL::Cd::ChangeDir(chain.a);
        if (chain.b) SRL::Cd::ChangeDir(chain.b);

        for (size_t ni = 0; ni < namesCount; ++ni)
        {
            const char* name = names[ni];
            SRL::Cd::File f(name);
            const bool exists = f.Exists() && f.Size.Bytes > 0;
            if (!exists) continue;

            if (chain.a && chain.b)
            {
                std::snprintf(lastSegmentPath_, sizeof(lastSegmentPath_), "%s/%s/%s", chain.a, chain.b, name);
            }
            else if (chain.a)
            {
                std::snprintf(lastSegmentPath_, sizeof(lastSegmentPath_), "%s/%s", chain.a, name);
            }
            else
            {
                std::snprintf(lastSegmentPath_, sizeof(lastSegmentPath_), "%s", name);
            }
            copy = CopyTrackSegmentToCart(name);
            SRL::Cd::ChangeDir((const char*)0);
            return copy;
        }
    }

    SRL::Cd::ChangeDir((const char*)0);
    std::snprintf(lastSegmentPath_, sizeof(lastSegmentPath_), "SEG_%03u.NYA (not found via ChangeDir)", unsigned(id));
    return copy;
}

TrackSystem::SegmentEntryVector TrackSystem::CopyAllTrackSegments(size_t maxSegments)
{
    SegmentEntryVector segments;
    const size_t loadLimit = (maxSegments == 0) ? kTrackSegmentLimit : std::min(maxSegments, kTrackSegmentLimit);
    segments.reserve(loadLimit);
    for (size_t i = 1; i <= loadLimit; ++i)
    {
        TrackSegmentCopy copy = CopySegmentById(i);
        if (!copy.cartPtr || copy.size == 0)
        {
            SRL::Debug::Print(1, 12, "Segment %03u path missing (%u variants)", unsigned(i), unsigned(kSegmentPathTemplates_.size()));
            break;
        }
        segments.push_back({ static_cast<int16_t>(i), copy });
        if (copy.cartPtr)
        {
            SRL::Debug::Print(1, 11, "Segment %03u copied (%u bytes)", unsigned(i), unsigned(copy.size));
        }
        else
        {
            // log removido
            break;
        }
    }

    size_t valid = 0;
    for (const auto& segment : segments)
    {
        if (segment.copy.cartPtr && segment.copy.size > 0) ++valid;
    }
    SRL::Debug::Print(1, 13, "Track segments copied %u/%u", unsigned(valid), unsigned(segments.size()));
    return segments;
}

std::vector<TrackSystem::SegmentRenderEntry> TrackSystem::BuildSegmentRenderers(SegmentEntryVector& entries)
{
    std::vector<SegmentRenderEntry> renderers;
    if (entries.empty()) return renderers;
    renderers.reserve(1);

    // Single-segment package: use the runtime blob path first and keep SDR as
    // compatibility fallback during migration.
    if (entries.size() == 1)
    {
        const int segmentId = entries[0].id;
        auto renderer = std::make_unique<TrackRenderer>();
        Vector3D center(0.0, 0.0, 0.0);
        TrackLowWorkU16Vector familyIds{};
        bool usedRdr = false;
        if (!BuildRendererFromRuntimeBlob(segmentId, *renderer, &center, &familyIds, &usedRdr))
        {
            SRL::Debug::Print(1, 15, "SDR init fail %03d", segmentId);
            return {};
        }
        if (usedRdr) ++runtimeRdrBuildsThisFrame_;
        else ++runtimeSdrBuildsThisFrame_;

        if (familyIds.empty())
        {
            SRL::Debug::Print(1, 15, "SDR fam fail %03d", segmentId);
            return {};
        }

        ConfigureStreamedRendererDefaults(*renderer);

        SegmentRenderEntry item{};
        item.id = segmentId;
        item.logicalSegmentCount = 1;
        item.center = center;
        item.renderer = std::move(renderer);
        item.lodState.ready = true;
        item.lodState.hasPerFaceRankOffsets = false;
        item.lodState.currentLodIndex = 0xFF;
        item.lodState.currentBaseRank = -1;
        item.lodState.faceFamilyIds = std::move(familyIds);
        item.lodState.faceRankOffsets.assign(item.lodState.faceFamilyIds.size(), 0);
        item.lodState.currentFaceSlots.assign(item.lodState.faceFamilyIds.size(), -1);
        renderers.push_back(std::move(item));
        return renderers;
    }

    // Pre-size batch buffers using exact SDR counts for this package.
    // This avoids repeated growth and lowers Work RAM fragmentation.
    size_t totalVerts = 0;
    size_t totalFaces = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        SegmentDrawReady::HeaderV1 hdr{};
        if (!LoadSdrHeaderForSegment(entries[i].id, hdr))
        {
            SRL::Debug::Print(1, 15, "SDR head fail %03d", entries[i].id);
            return {};
        }
        totalVerts += static_cast<size_t>(hdr.vertexCount);
        totalFaces += static_cast<size_t>(hdr.faceCount);
    }
    if (totalVerts >= static_cast<size_t>(0xFFFF))
    {
        SRL::Debug::Print(1, 15, "SDR pkg vtx ovf:%u", static_cast<unsigned>(totalVerts));
        return {};
    }
    SRL::Debug::Print(1, 12, "SDR pkg vf v:%u f:%u", (unsigned)totalVerts, (unsigned)totalFaces);

    std::vector<SRL::Math::Types::Vector3D> batchVerts{};
    std::vector<SRL::Types::Polygon> batchFaces{};
    std::vector<SRL::Types::Attribute> batchAttrs{};
    TrackLowWorkU16Vector batchFamilyIds{};
    TrackLowWorkU8Vector batchFaceRankOffsets{};
    batchVerts.reserve(totalVerts);
    batchFaces.reserve(totalFaces);
    batchAttrs.reserve(totalFaces);
    batchFamilyIds.reserve(totalFaces);
    batchFaceRankOffsets.reserve(totalFaces);

    Vector3D minv(SRL::Math::Types::Fxp::BuildRaw(32767 << 16),
                  SRL::Math::Types::Fxp::BuildRaw(32767 << 16),
                  SRL::Math::Types::Fxp::BuildRaw(32767 << 16));
    Vector3D maxv(SRL::Math::Types::Fxp::BuildRaw(-32768 << 16),
                  SRL::Math::Types::Fxp::BuildRaw(-32768 << 16),
                  SRL::Math::Types::Fxp::BuildRaw(-32768 << 16));

    // Build one runtime draw package from N contiguous SDR segments.
    // This keeps asset flexibility (segment-level SDR) while reducing draw count.
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (!AppendSdrSegmentToBatch(entries[i].id,
                                     static_cast<uint8_t>(i),
                                     batchVerts,
                                     batchFaces,
                                     batchAttrs,
                                     batchFamilyIds,
                                     batchFaceRankOffsets,
                                     minv,
                                     maxv))
        {
            SRL::Debug::Print(1, 15, "SDR batch fail %03d", entries[i].id);
            return {};
        }
    }

    auto renderer = std::make_unique<TrackRenderer>();
    if (!renderer->InitializeFromComponentData(std::move(batchVerts),
                                               std::move(batchFaces),
                                               std::move(batchAttrs)))
    {
        SRL::Debug::Print(1, 15, "SDR pkg init fail %03d", entries.front().id);
        return {};
    }
    ConfigureStreamedRendererDefaults(*renderer);

    SegmentRenderEntry item{};
    item.id = entries.front().id;
    item.logicalSegmentCount = static_cast<uint8_t>(std::min<size_t>(entries.size(), 255));
    item.center = (minv + maxv) / SRL::Math::Types::Fxp::BuildRaw(2 << 16);
    item.renderer = std::move(renderer);
    item.lodState.ready = true;
    item.lodState.currentLodIndex = 0xFF;
    item.lodState.currentBaseRank = -1;
    item.lodState.faceFamilyIds = std::move(batchFamilyIds);
    item.lodState.faceRankOffsets = std::move(batchFaceRankOffsets);
    item.lodState.hasPerFaceRankOffsets = false;
    for (size_t fi = 0; fi < item.lodState.faceRankOffsets.size(); ++fi)
    {
        if (item.lodState.faceRankOffsets[fi] != 0)
        {
            item.lodState.hasPerFaceRankOffsets = true;
            break;
        }
    }
    item.lodState.currentFaceSlots.assign(item.lodState.faceFamilyIds.size(), -1);
    renderers.push_back(std::move(item));
    return renderers;
}

std::vector<TrackSystem::SegmentHandle> TrackSystem::BuildSegmentHandleTable()
{
    segmentPool_.Reset();
    std::vector<SegmentHandle> handles;
    handles.reserve(segmentRenderers_.size());
    if (segmentRenderers_.empty()) return handles;
    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        auto& entry = segmentRenderers_[i];
        handles.push_back(segmentPool_.Add(&entry));
    }
    return handles;
}

void TrackSystem::InitializeFamilySlots(FamilySlotVector& outSlots,
                                        const int* familyIds,
                                        size_t count) const
{
    outSlots.clear();
    outSlots.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const int fam = familyIds ? familyIds[i] : 0;
        if (fam <= 0) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = static_cast<uint16_t>(fam);
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        outSlots.push_back(slotEntry);
    }
    if (&outSlots == &seg1FamilySlots_) InvalidateFamilySlotIndex();
}

void TrackSystem::InitializeFamilySlots(FamilySlotVector& outSlots,
                                        const FamilyIdCatalogVector& familyIds) const
{
    outSlots.clear();
    outSlots.reserve(familyIds.size());
    for (size_t i = 0; i < familyIds.size(); ++i)
    {
        const uint16_t fam = familyIds[i];
        if (fam == 0) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = fam;
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        outSlots.push_back(slotEntry);
    }
    if (&outSlots == &seg1FamilySlots_) InvalidateFamilySlotIndex();
}

void TrackSystem::InvalidateFamilySlotIndex() const
{
    familySlotIndexDirty_ = true;
}

void TrackSystem::RebuildFamilySlotIndex() const
{
    if (!familySlotIndexDirty_) return;
    for (size_t i = 0; i < familySlotIndex_.size(); ++i)
    {
        familySlotIndex_[i] = -1;
    }
    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        const uint16_t fam = seg1FamilySlots_[i].familyId;
        if (fam < familySlotIndex_.size() && familySlotIndex_[fam] < 0)
        {
            familySlotIndex_[fam] = static_cast<int16_t>(i);
        }
    }
    familySlotIndexDirty_ = false;
}

TrackSystem::Seg1FamilySlotEntry* TrackSystem::FindFamilySlot(FamilySlotVector& familySlots, uint16_t familyId)
{
    if (&familySlots == &seg1FamilySlots_ && familyId < familySlotIndex_.size())
    {
        RebuildFamilySlotIndex();
        const int16_t idx = familySlotIndex_[familyId];
        if (idx >= 0)
        {
            const size_t uidx = static_cast<size_t>(idx);
            if (uidx < familySlots.size() && familySlots[uidx].familyId == familyId)
            {
                return &familySlots[uidx];
            }
        }
    }
    for (size_t i = 0; i < familySlots.size(); ++i)
    {
        if (familySlots[i].familyId == familyId) return &familySlots[i];
    }
    return nullptr;
}

const TrackSystem::Seg1FamilySlotEntry* TrackSystem::FindFamilySlot(const FamilySlotVector& familySlots, uint16_t familyId) const
{
    if (&familySlots == &seg1FamilySlots_ && familyId < familySlotIndex_.size())
    {
        RebuildFamilySlotIndex();
        const int16_t idx = familySlotIndex_[familyId];
        if (idx >= 0)
        {
            const size_t uidx = static_cast<size_t>(idx);
            if (uidx < familySlots.size() && familySlots[uidx].familyId == familyId)
            {
                return &familySlots[uidx];
            }
        }
    }
    for (size_t i = 0; i < familySlots.size(); ++i)
    {
        if (familySlots[i].familyId == familyId) return &familySlots[i];
    }
    return nullptr;
}

bool TrackSystem::TryGetFamilyLodSlot(const FamilySlotVector& familySlots,
                                      uint16_t familyId,
                                      uint8_t lodIndex,
                                      uint16_t& outSlot) const
{
    outSlot = No_Texture;
    if (lodIndex > 3) return false;
    const auto* slotEntry = FindFamilySlot(familySlots, familyId);
    if (!slotEntry) return false;
    outSlot = slotEntry->lodSlots[lodIndex];
    return IsVdp1TextureSlotLive(outSlot);
}

const TrackSystem::Seg1TexbankEntry* TrackSystem::FindTexbankEntryByFamily(const Seg1TexbankCart& bank, uint16_t familyId) const
{
    for (size_t i = 0; i < bank.entries.size(); ++i)
    {
        if (bank.entries[i].familyId == familyId) return &bank.entries[i];
    }
    return nullptr;
}

bool TrackSystem::TryLoadFamilyLodSlot(Seg1FamilySlotEntry& slotEntry,
                                       uint8_t targetLodIndex,
                                       bool fallbackToLowerLods,
                                       bool fallbackToHigherLods,
                                       bool* outSawMissingFamily,
                                       bool* outSawDecodeFail,
                                       bool* outSawUploadFail,
                                       int* outLoadedFromLodValue)
{
    if (outSawMissingFamily) *outSawMissingFamily = false;
    if (outSawDecodeFail) *outSawDecodeFail = false;
    if (outSawUploadFail) *outSawUploadFail = false;
    if (outLoadedFromLodValue) *outLoadedFromLodValue = 0;

    if (targetLodIndex > 3) return false;
    if (slotEntry.familyId == 0) return false;
    if (slotEntry.lodSlots[targetLodIndex] != No_Texture)
    {
        const uint16_t existingSlot = slotEntry.lodSlots[targetLodIndex];
        if (IsVdp1TextureSlotLive(existingSlot))
        {
            if (outLoadedFromLodValue)
            {
                const int lodValues[4] = { 8, 16, 32, 64 };
                *outLoadedFromLodValue = lodValues[targetLodIndex];
            }
            return true;
        }
        // Slot was previously assigned but is no longer valid in VDP1 metadata.
        slotEntry.lodSlots[targetLodIndex] = No_Texture;
    }

    const int lodValues[4] = { 8, 16, 32, 64 };
    uint8_t searchOrder[4]{};
    size_t searchCount = 0;
    searchOrder[searchCount++] = targetLodIndex;
    if (fallbackToHigherLods)
    {
        // Prefer nearest higher lod first to reduce upload cost and memory spikes.
        for (int li = static_cast<int>(targetLodIndex) + 1; li <= 3; ++li)
        {
            searchOrder[searchCount++] = static_cast<uint8_t>(li);
        }
    }
    if (fallbackToLowerLods)
    {
        for (int li = static_cast<int>(targetLodIndex) - 1; li >= 0; --li)
        {
            searchOrder[searchCount++] = static_cast<uint8_t>(li);
        }
    }

    for (size_t si = 0; si < searchCount; ++si)
    {
        const uint8_t sourceLodIndex = searchOrder[si];
        if (!LoadSeg1TexbankIndexToCart(static_cast<size_t>(sourceLodIndex), lodValues[sourceLodIndex])) continue;
        const auto& bank = seg1Texbanks_[sourceLodIndex];
        const uint8_t* bankBytes = static_cast<const uint8_t*>(bank.cartPtr);
        if (!bankBytes) continue;

        const Seg1TexbankEntry* bankEntry = FindTexbankEntryByFamily(bank, slotEntry.familyId);
        if (!bankEntry)
        {
            if (outSawMissingFamily) *outSawMissingFamily = true;
            continue;
        }

        DecodedTgaTexture decoded{};
        if (!DecodePalettedTgaMemory(bankBytes + bankEntry->offset, bankEntry->size, decoded))
        {
            if (outSawDecodeFail) *outSawDecodeFail = true;
            continue;
        }

        const int32_t slot = UploadDecodedTextureToVdp1(decoded);
        if (slot < 0)
        {
            if (outSawUploadFail) *outSawUploadFail = true;
            continue;
        }

        slotEntry.lodSlots[targetLodIndex] = static_cast<uint16_t>(slot);
        if (outLoadedFromLodValue) *outLoadedFromLodValue = lodValues[sourceLodIndex];
        return true;
    }

    return false;
}

bool TrackSystem::PreloadFullTrackFamilyLodCache()
{
    const int lodValues[4] = { 8, 16, 32, 64 };
    const size_t loadBankCount = kEnableTrackRuntimeStabilization ? 1u : seg1Texbanks_.size();
    for (size_t li = 0; li < loadBankCount; ++li)
    {
        if (!LoadSeg1TexbankIndexToCart(li, lodValues[li]))
        {
            fullTrackFamilyCacheReady_ = false;
            return false;
        }
    }
    for (size_t li = loadBankCount; li < seg1Texbanks_.size(); ++li)
    {
        auto& bank = seg1Texbanks_[li];
        if (bank.cartPtr)
        {
            SRL::Memory::CartRam::Free(bank.cartPtr);
        }
        bank = {};
        bank.lod = lodValues[li];
    }

    FamilyIdCatalogVector familyIdsUsed{};
    familyIdsUsed.reserve(128);
    std::array<uint8_t, 4096> seenFamilies{};
    for (size_t i = 0; i < seenFamilies.size(); ++i) seenFamilies[i] = 0u;

    for (size_t li = 0; li < loadBankCount; ++li)
    {
        const auto& bank = seg1Texbanks_[li];
        for (size_t i = 0; i < bank.entries.size(); ++i)
        {
            const uint16_t fam = bank.entries[i].familyId;
            if (fam == 0) continue;
            if (fam < seenFamilies.size())
            {
                if (seenFamilies[fam] != 0u) continue;
                seenFamilies[fam] = 1u;
            }
            else
            {
                bool exists = false;
                for (size_t fi = 0; fi < familyIdsUsed.size(); ++fi)
                {
                    if (familyIdsUsed[fi] == fam)
                    {
                        exists = true;
                        break;
                    }
                }
                if (exists) continue;
            }
            familyIdsUsed.push_back(fam);
        }
    }

    if (familyIdsUsed.empty())
    {
        fullTrackFamilyCacheReady_ = false;
        return false;
    }

    InitializeFamilySlots(seg1FamilySlots_, familyIdsUsed);
    InvalidateFamilySlotIndex();
    fullTrackFamilyCacheReady_ = !seg1FamilySlots_.empty();

    unsigned loadedLod8 = 0;
    unsigned failedLod8 = 0;
    if (fullTrackFamilyCacheReady_ && kEnableTrackRuntimeStabilization)
    {
        const bool savedReady = ready_;
        const uint8_t savedTextureUploads = textureUploadsThisFrame_;
        ready_ = false;
        textureUploadsThisFrame_ = 0;
        for (auto& family : seg1FamilySlots_)
        {
            bool sawMissingFamily = false;
            bool sawDecodeFail = false;
            bool sawUploadFail = false;
            if (TryLoadFamilyLodSlot(family,
                                     0,
                                     /*fallbackToLowerLods*/false,
                                     /*fallbackToHigherLods*/false,
                                     &sawMissingFamily,
                                     &sawDecodeFail,
                                     &sawUploadFail,
                                     nullptr))
            {
                ++loadedLod8;
            }
            else
            {
                ++failedLod8;
            }
        }
        ready_ = savedReady;
        textureUploadsThisFrame_ = savedTextureUploads;
    }

    const auto cart = SRL::Memory::CartRam::GetReport();
    const auto hwr = SRL::Memory::HighWorkRam::GetReport();
    const auto lwr = SRL::Memory::LowWorkRam::GetReport();
    SRL::Debug::Print(1, 21, "TRK fam catalog fam:%u banks:%u l8:%u fail:%u",
                      static_cast<unsigned>(seg1FamilySlots_.size()),
                      static_cast<unsigned>(loadBankCount),
                      loadedLod8,
                      failedLod8);
    SRL::Debug::Print(1, 22, "TRK mem hb:%u lb:%u cf:%u",
                      static_cast<unsigned>(EstimateWorkRamRetainedBytes()),
                      static_cast<unsigned>(EstimateLowWorkRamRetainedBytes()),
                      static_cast<unsigned>(cart.FreeSize));
    SRL::Debug::Print(1, 23, "TRK ram hf:%u lf:%u     ",
                      static_cast<unsigned>(hwr.FreeSize),
                      static_cast<unsigned>(lwr.FreeSize));
    return fullTrackFamilyCacheReady_;
}

// Build shared family ids used by the track renderer set. Texture slots are loaded lazily.
bool TrackSystem::BuildTrackFamilyLodSlots(FamilySlotVector& outSlots)
{
    outSlots.clear();
    FamilyIdCatalogVector familyIdsUsed{};
    familyIdsUsed.reserve(256);
    std::array<uint8_t, 4096> seenSmallFamily{};
    for (size_t i = 0; i < seenSmallFamily.size(); ++i) seenSmallFamily[i] = 0;

    for (const auto& seg : segmentRenderers_)
    {
        if (!seg.renderer) continue;
        if (!seg.lodState.ready) continue;
        for (size_t fi = 0; fi < seg.lodState.faceFamilyIds.size(); ++fi)
        {
            const uint16_t fam = seg.lodState.faceFamilyIds[fi];
            if (fam == 0) continue;
            if (fam < seenSmallFamily.size())
            {
                if (seenSmallFamily[fam]) continue;
                seenSmallFamily[fam] = 1;
                familyIdsUsed.push_back(fam);
                continue;
            }
            bool exists = false;
            for (size_t i = 0; i < familyIdsUsed.size(); ++i)
            {
                if (familyIdsUsed[i] == fam)
                {
                    exists = true;
                    break;
                }
            }
            if (!exists) familyIdsUsed.push_back(fam);
        }
    }

    if (familyIdsUsed.empty()) return false;

    InitializeFamilySlots(outSlots, familyIdsUsed);

    return !outSlots.empty();
}

// Build the per face family table for one segment renderer from SDR1.
bool TrackSystem::BuildSegmentLodState(SegmentRenderEntry& entry,
                                       const SegmentComponent::Blob& matBlob,
                                       const SegmentComponent::Loader::MatView& matView,
                                       FamilySlotVector& familySlots)
{
    if (!entry.renderer) return false;
    if (!entry.lodState.ready) return false;
    if (entry.lodState.faceFamilyIds.empty()) return false;
    entry.lodState.currentFaceSlots.assign(entry.lodState.faceFamilyIds.size(), -1);
    entry.lodState.hasPerFaceRankOffsets = false;
    for (size_t fi = 0; fi < entry.lodState.faceRankOffsets.size(); ++fi)
    {
        if (entry.lodState.faceRankOffsets[fi] != 0)
        {
            entry.lodState.hasPerFaceRankOffsets = true;
            break;
        }
    }

    (void)matBlob;
    (void)matView;

    entry.lodState.currentLodIndex = 0xFF;
    entry.lodState.currentBaseRank = -1;
    return RebuildSegmentFaceSlotsForBaseRank(entry, 0, familySlots);
}

// Upload one family texture slot only when a lod band actually needs it.
bool TrackSystem::EnsureFamilyLodSlotLoaded(FamilySlotVector& familySlots,
                                            uint16_t familyId,
                                            uint8_t lodIndex)
{
    if (lodIndex > 3) return false;

    Seg1FamilySlotEntry* slotEntry = FindFamilySlot(familySlots, familyId);
    if (!slotEntry) return false;
    const uint16_t existing = slotEntry->lodSlots[lodIndex];
    if (existing != No_Texture && IsVdp1TextureSlotLive(existing)) return true;
    if (ready_ && textureUploadsThisFrame_ >= kTextureUploadsBudgetPerFrame) return false;
    const bool loaded = TryLoadFamilyLodSlot(*slotEntry,
                                             lodIndex,
                                             /*fallbackToLowerLods*/true,
                                             /*fallbackToHigherLods*/true,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr);
    if (loaded && ready_) ++textureUploadsThisFrame_;
    return loaded;
}

// Rebuild one segment face slot table on demand for the selected lod band.
bool TrackSystem::RebuildSegmentFaceSlotsForLod(SegmentRenderEntry& entry,
                                                uint8_t lodIndex,
                                                FamilySlotVector& familySlots)
{
    if (!entry.renderer) return false;
    if (lodIndex > 3) return false;
    if (entry.lodState.faceFamilyIds.empty()) return false;

    const size_t faceCount = entry.lodState.faceFamilyIds.size();
    entry.lodState.currentFaceSlots.assign(faceCount, -1);
    std::vector<uint16_t> seenFamilies{};
    std::vector<int16_t> seenSlots{};
    seenFamilies.reserve(32);
    seenSlots.reserve(32);

    for (size_t fi = 0; fi < faceCount; ++fi)
    {
        const uint16_t fam = entry.lodState.faceFamilyIds[fi];
        if (fam == 0) continue;

        size_t cached = static_cast<size_t>(-1);
        for (size_t i = 0; i < seenFamilies.size(); ++i)
        {
            if (seenFamilies[i] == fam)
            {
                cached = i;
                break;
            }
        }
        if (cached != static_cast<size_t>(-1))
        {
            entry.lodState.currentFaceSlots[fi] = seenSlots[cached];
            continue;
        }

        (void)EnsureFamilyLodSlotLoaded(familySlots, fam, lodIndex);
        int16_t resolved = -1;
        uint16_t slot = No_Texture;
        if (TryGetFamilyLodSlot(familySlots, fam, lodIndex, slot))
        {
            resolved = static_cast<int16_t>(slot);
            entry.lodState.currentFaceSlots[fi] = resolved;
        }
        seenFamilies.push_back(fam);
        seenSlots.push_back(resolved);
    }

    return true;
}

bool TrackSystem::RebuildSegmentFaceSlotsForBaseRank(SegmentRenderEntry& entry,
                                                     size_t baseRank,
                                                     FamilySlotVector& familySlots)
{
    if (!entry.renderer) return false;
    if (entry.lodState.faceFamilyIds.empty()) return false;

    const size_t faceCount = entry.lodState.faceFamilyIds.size();
    entry.lodState.currentFaceSlots.assign(faceCount, -1);
    std::vector<uint16_t> seenFamilies{};
    std::vector<int16_t> seenSlots{};
    seenFamilies.reserve(32);
    seenSlots.reserve(32);

    const bool hasRankOffsets = entry.lodState.faceRankOffsets.size() == faceCount;
    for (size_t fi = 0; fi < faceCount; ++fi)
    {
        const uint16_t fam = entry.lodState.faceFamilyIds[fi];
        if (fam == 0) continue;

        const size_t rank = baseRank + (hasRankOffsets ? static_cast<size_t>(entry.lodState.faceRankOffsets[fi]) : 0u);
        const uint8_t lodIndex = ResolveSegmentLodIndexByRank(rank);
        const uint16_t key = static_cast<uint16_t>((static_cast<uint16_t>(lodIndex) << 12) | (fam & 0x0FFFu));
        size_t cached = static_cast<size_t>(-1);
        for (size_t i = 0; i < seenFamilies.size(); ++i)
        {
            if (seenFamilies[i] == key)
            {
                cached = i;
                break;
            }
        }
        if (cached != static_cast<size_t>(-1))
        {
            entry.lodState.currentFaceSlots[fi] = seenSlots[cached];
            continue;
        }

        (void)EnsureFamilyLodSlotLoaded(familySlots, fam, lodIndex);
        int16_t resolved = -1;
        uint16_t slot = No_Texture;
        if (TryGetFamilyLodSlot(familySlots, fam, lodIndex, slot))
        {
            resolved = static_cast<int16_t>(slot);
            entry.lodState.currentFaceSlots[fi] = resolved;
        }
        seenFamilies.push_back(key);
        seenSlots.push_back(resolved);
    }

    return true;
}

bool TrackSystem::RebuildSafeSegmentEntry(SegmentRenderEntry& entry)
{
    if (!entry.renderer) return false;
    if (entry.id <= 0) return false;

    Vector3D rebuiltCenter(0.0, 0.0, 0.0);
    FamilyIdVector rebuiltFamilyIds{};
    if (!BuildSegmentIntoRenderer(entry.id, *entry.renderer, rebuiltCenter, rebuiltFamilyIds)) return false;
    if (rebuiltFamilyIds.empty()) return false;

    bool addedFamily = false;
    for (const uint16_t fam : rebuiltFamilyIds)
    {
        if (fam == 0) continue;
        if (FindFamilySlot(seg1FamilySlots_, fam)) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = fam;
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        seg1FamilySlots_.push_back(slotEntry);
        addedFamily = true;
    }
    if (addedFamily) InvalidateFamilySlotIndex();

    entry.center = rebuiltCenter;
    entry.logicalSegmentCount = 1;
    entry.lodState.ready = true;
    entry.lodState.hasPerFaceRankOffsets = false;
    entry.lodState.currentBaseRank = -1;
    entry.lodState.currentLodIndex = 0xFF;
    entry.lodState.faceFamilyIds = std::move(rebuiltFamilyIds);
    entry.lodState.faceRankOffsets.assign(entry.lodState.faceFamilyIds.size(), 0u);
    entry.lodState.currentFaceSlots.assign(entry.lodState.faceFamilyIds.size(), -1);
    if (!RebuildSegmentFaceSlotsForLod(entry, 0, seg1FamilySlots_)) return false;
    if (HasMissingRequiredFaceTextureSlots(entry.lodState.currentFaceSlots, &entry.lodState.faceFamilyIds))
    {
        return false;
    }
    (void)entry.renderer->ApplyFaceTextureSlotsGlobal(entry.lodState.currentFaceSlots);
    entry.lodState.currentLodIndex = 0;
    return IsRendererStateIntegral(*entry.renderer);
}

// Map a near to far rank into the current fixed lod bands for track rendering.
uint8_t TrackSystem::ResolveSegmentLodIndexByRank(size_t rank) const
{
    if (kEnableTrackRuntimeStabilization)
    {
        (void)rank;
        return 0; // correctness-first path: keep the active window in 8x8 only
    }
    const size_t lod64End = static_cast<size_t>(kLodBand64Count);
    const size_t lod32End = lod64End + static_cast<size_t>(kLodBand32Count);
    const size_t lod16End = lod32End + static_cast<size_t>(kLodBand16Count);
    const size_t lod8End = lod16End + static_cast<size_t>(kLodBand8Count);
    if (rank < lod64End) return 3; // 64x64 for [0..3]
    if (rank < lod32End) return 2; // 32x32 for [4..8]
    if (rank < lod16End) return 1; // 16x16 for [9..13]
    if (rank < lod8End) return 0;  // 8x8   for [14..19]
    return 0;                      // keep 8x8 beyond rank 19
}

// Apply lod changes only when a visible segment crosses a band boundary.
void TrackSystem::UpdateVisibleSegmentLods(const std::vector<SegmentHandle>& nearToFarHandles)
{
    if (kEnableTrackRuntimeStabilization)
    {
        (void)nearToFarHandles;
        return;
    }

    FamilySlotVector& familySlots = seg1FamilySlots_;
    size_t logicalRank = 0;
    for (size_t rank = 0; rank < nearToFarHandles.size(); ++rank)
    {
        auto* entry = segmentPool_.Resolve(nearToFarHandles[rank]);
        if (!entry || !entry->renderer) continue;
        if (!entry->lodState.ready) continue;

        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const int16_t desiredBaseRank = static_cast<int16_t>(logicalRank);
        if (!entry->lodState.hasPerFaceRankOffsets)
        {
            // Fast path for 1-segment packages: update only when band changes.
            if (entry->lodState.currentLodIndex == desiredLodIndex &&
                !HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                    &entry->lodState.faceFamilyIds))
            {
                logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
                continue;
            }
            if (ready_ && textureUploadsThisFrame_ >= kTextureUploadsBudgetPerFrame)
            {
                // Keep current mapping this frame and retry when upload budget resets.
                logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
                continue;
            }

            std::vector<int16_t> previousFaceSlots = entry->lodState.currentFaceSlots;
            const uint8_t previousLodIndex = entry->lodState.currentLodIndex;
            const int16_t previousBaseRank = entry->lodState.currentBaseRank;
            if (!RebuildSegmentFaceSlotsForLod(*entry, desiredLodIndex, familySlots))
            {
                entry->lodState.currentFaceSlots.swap(previousFaceSlots);
                entry->lodState.currentLodIndex = previousLodIndex;
                entry->lodState.currentBaseRank = previousBaseRank;
                logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
                continue;
            }
            const bool missing =
                HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                   &entry->lodState.faceFamilyIds);
            if (missing)
            {
                // Keep the previous mapping until the new lod is fully available.
                entry->lodState.currentFaceSlots.swap(previousFaceSlots);
                entry->lodState.currentLodIndex = previousLodIndex;
                entry->lodState.currentBaseRank = previousBaseRank;
                logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
                continue;
            }
            (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
            ++runtimeFaceRemapsThisFrame_;
            ++runtimeLodSegmentUpdatesThisFrame_;
            entry->lodState.currentLodIndex = desiredLodIndex;
            entry->lodState.currentBaseRank = -1;
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }

        // Fallback path for multi-segment batches with per-face rank offsets.
        if (entry->lodState.currentBaseRank == desiredBaseRank &&
            entry->lodState.currentLodIndex != 0xFF)
        {
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }
        if (ready_ && textureUploadsThisFrame_ >= kTextureUploadsBudgetPerFrame)
        {
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }
        std::vector<int16_t> previousFaceSlots = entry->lodState.currentFaceSlots;
        const uint8_t previousLodIndex = entry->lodState.currentLodIndex;
        const int16_t previousBaseRank = entry->lodState.currentBaseRank;
        if (!RebuildSegmentFaceSlotsForBaseRank(*entry, logicalRank, familySlots))
        {
            entry->lodState.currentFaceSlots.swap(previousFaceSlots);
            entry->lodState.currentLodIndex = previousLodIndex;
            entry->lodState.currentBaseRank = previousBaseRank;
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }
        const bool missing =
            HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                               &entry->lodState.faceFamilyIds);
        if (missing)
        {
            entry->lodState.currentFaceSlots.swap(previousFaceSlots);
            entry->lodState.currentLodIndex = previousLodIndex;
            entry->lodState.currentBaseRank = previousBaseRank;
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }
        (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
        ++runtimeFaceRemapsThisFrame_;
        ++runtimeLodSegmentUpdatesThisFrame_;
        entry->lodState.currentBaseRank = desiredBaseRank;
        entry->lodState.currentLodIndex = desiredLodIndex;
        logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
    }
}

bool TrackSystem::BuildSegmentCenterCatalog()
{
    segmentCenterCatalog_.clear();

    // Catalog source prefers the runtime blob pack and falls back to SDR during migration.
    // Stop on first missing id to keep the id space contiguous.
    constexpr int32_t kCatalogHardLimit = 4096;
    for (int32_t id = 1; id <= kCatalogHardLimit; ++id)
    {
        int32_t centerX = 0;
        int32_t centerY = 0;
        int32_t centerZ = 0;
        SegmentRuntimeDraw::HeaderV1 rdrHeader{};
        if (LoadRdrHeaderForSegment(id, rdrHeader))
        {
            centerX = rdrHeader.centerX;
            centerY = rdrHeader.centerY;
            centerZ = rdrHeader.centerZ;
        }
        else
        {
            SegmentDrawReady::HeaderV1 sdrHeader{};
            if (!LoadSdrHeaderForSegment(id, sdrHeader))
            {
                break;
            }
            centerX = sdrHeader.centerX;
            centerY = sdrHeader.centerY;
            centerZ = sdrHeader.centerZ;
        }

        segmentCenterCatalog_.push_back(Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(centerX),
            SRL::Math::Types::Fxp::BuildRaw(centerY),
            SRL::Math::Types::Fxp::BuildRaw(centerZ)));
    }

    totalSegmentCount_ = static_cast<uint16_t>(std::min<size_t>(
        segmentCenterCatalog_.size(),
        static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
    if (totalSegmentCount_ == 0)
    {
        activeWindowStartId_ = 1;
        return false;
    }

    activeWindowStartId_ = 1;
    return true;
}

bool TrackSystem::RebuildActiveSegmentWindow(int32_t startSegmentId, size_t loadLimit, int8_t direction)
{
    direction = (direction < 0) ? -1 : 1;
    if (totalSegmentCount_ == 0 || loadLimit == 0)
    {
        segmentEntries_.clear();
        segmentRenderers_.clear();
        segmentHandles_.clear();
        segmentPool_.Reset();
        activeWindowHead_ = 0;
        windowDirection_ = direction;
        prewarmCooldown_ = 0;
        slideScratchRenderer_.reset();
        ResetSlidePrefetchState();
        familyMergeCooldown_ = 0;
        segmentsReady_ = false;
        return false;
    }

    const int32_t wrappedStartId = WrapSegmentIdToRange(startSegmentId, totalSegmentCount_);
    if (wrappedStartId <= 0) return false;
    activeWindowStartId_ = wrappedStartId;
    windowDirection_ = direction;

    const size_t windowCount = std::min<size_t>(
        std::min<size_t>(loadLimit, kTrackSegmentLimit),
        static_cast<size_t>(totalSegmentCount_));

    segmentEntries_.clear();
    segmentRenderers_.clear();
    segmentHandles_.clear();
    segmentPool_.Reset();
    activeWindowHead_ = 0;
    prewarmCooldown_ = 0;
    slideScratchRenderer_.reset();
    ResetSlidePrefetchState();
    familyMergeCooldown_ = 0;
    segmentEntries_.reserve(windowCount);
    segmentRenderers_.reserve(windowCount);

    // Runtime streaming path: keep package fixed at one segment to minimize
    // transient allocations and avoid heavy planner probes every window shift.
    const size_t segmentsPerDrawPackage = 1;
    size_t builtCount = 0;

    for (size_t logicalSid = 0; logicalSid < windowCount; )
    {
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        if (freeValid && freeBytes <= kWorkRamHardFloorBytes)
        {
            SRL::Debug::Print(1, 11, "PKG low HWR sid:%u free:%u ok:%u",
                              static_cast<unsigned>(logicalSid + 1),
                              static_cast<unsigned>(freeBytes),
                              freeValid ? 1u : 0u);
        }

        const size_t chosenCount = std::min(segmentsPerDrawPackage, (windowCount - logicalSid));
        SegmentEntryVector batchEntries{};
        batchEntries.reserve(chosenCount);
        for (size_t i = 0; i < chosenCount; ++i)
        {
            const int32_t sid = WrapSegmentIdToRange(
                wrappedStartId + (static_cast<int32_t>(logicalSid + i) * static_cast<int32_t>(direction)),
                totalSegmentCount_);
            if (sid <= 0) continue;
            batchEntries.push_back({ static_cast<int16_t>(sid), {} });
        }

        if (batchEntries.empty())
        {
            logicalSid += chosenCount;
            continue;
        }

        auto built = BuildSegmentRenderers(batchEntries);
        if (built.empty())
        {
            SRL::Debug::Print(1, 11, "PKG build fail id:%d", batchEntries.front().id);
            break;
        }

        // Keep metadata aligned with successfully built renderers.
        segmentEntries_.push_back({ batchEntries.front().id, {} });
        segmentRenderers_.push_back(std::move(built[0]));
        ++builtCount;
        logicalSid += chosenCount;
    }

    const bool fullWindowBuilt = (builtCount == windowCount);
    segmentsReady_ = fullWindowBuilt;
    if (!segmentsReady_)
    {
        segmentEntries_.clear();
        segmentRenderers_.clear();
        segmentHandles_.clear();
        segmentPool_.Reset();
        activeWindowHead_ = 0;
        slideScratchRenderer_.reset();
        ResetSlidePrefetchState();
        SRL::Debug::Print(1, 11, "PKG window incomplete built:%u need:%u",
                          static_cast<unsigned>(builtCount),
                          static_cast<unsigned>(windowCount));
        return false;
    }
    activeWindowHead_ = 0;
    segmentHandles_ = BuildSegmentHandleTable();
    if (kEnableTrackRuntimeStabilization && !slideScratchRenderer_)
    {
        slideScratchRenderer_ = std::make_unique<TrackRenderer>();
        if (slideScratchRenderer_)
        {
            ConfigureStreamedRendererDefaults(*slideScratchRenderer_);
        }
    }

    SRL::Debug::Print(1, 13, "Track pkg built %u segs:%u start:%d tot:%u",
                      static_cast<unsigned>(builtCount),
                      static_cast<unsigned>(segmentEntries_.size()),
                      activeWindowStartId_,
                      static_cast<unsigned>(totalSegmentCount_));
    return segmentsReady_;
}

void TrackSystem::ResetSlidePrefetchState()
{
    slidePrefetchSegmentId_ = -1;
    slidePrefetchCenter_ = Vector3D(0.0, 0.0, 0.0);
    slidePrefetchFamilyIds_.clear();
    slidePrefetchFaceSlots_.clear();
    slidePrefetchRenderer_.reset();
    slidePrefetchLod8Ready_ = false;
}

bool TrackSystem::BuildSegmentIntoPrefetch(int32_t segmentId)
{
    if (segmentId <= 0) return false;

    if (kEnableTrackRuntimeStabilization)
    {
        const bool needsRendererBuild =
            (slidePrefetchSegmentId_ != segmentId) ||
            !slideScratchRenderer_ ||
            slidePrefetchFamilyIds_.empty();

        if (needsRendererBuild)
        {
            if (!slideScratchRenderer_)
            {
                slideScratchRenderer_ = std::make_unique<TrackRenderer>();
            }
            if (!slideScratchRenderer_) return false;

            Vector3D center(0.0, 0.0, 0.0);
            TrackLowWorkU16Vector familyIds{};
            bool usedRdr = false;
            if (!BuildRendererFromRuntimeBlob(segmentId, *slideScratchRenderer_, &center, &familyIds, &usedRdr) ||
                familyIds.empty())
            {
                ResetSlidePrefetchState();
                return false;
            }
            if (usedRdr) ++runtimeRdrBuildsThisFrame_;
            else ++runtimeSdrBuildsThisFrame_;

            ConfigureStreamedRendererDefaults(*slideScratchRenderer_);
            slidePrefetchSegmentId_ = segmentId;
            slidePrefetchCenter_ = center;
            slidePrefetchFamilyIds_ = std::move(familyIds);
            slidePrefetchFaceSlots_.assign(slidePrefetchFamilyIds_.size(), -1);
            slidePrefetchLod8Ready_ = false;
        }

        if (!slideScratchRenderer_ || slidePrefetchFamilyIds_.empty()) return false;

        bool addedFamily = false;
        for (size_t fi = 0; fi < slidePrefetchFamilyIds_.size(); ++fi)
        {
            const uint16_t fam = slidePrefetchFamilyIds_[fi];
            if (fam == 0) continue;
            if (FindFamilySlot(seg1FamilySlots_, fam)) continue;
            Seg1FamilySlotEntry slotEntry{};
            slotEntry.familyId = fam;
            slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
            seg1FamilySlots_.push_back(slotEntry);
            addedFamily = true;
        }
        if (addedFamily) InvalidateFamilySlotIndex();

        SegmentRenderEntry prefetched{};
        prefetched.id = segmentId;
        prefetched.logicalSegmentCount = 1;
        prefetched.renderer = std::move(slideScratchRenderer_);
        prefetched.center = slidePrefetchCenter_;
        prefetched.lodState.ready = true;
        prefetched.lodState.hasPerFaceRankOffsets = false;
        prefetched.lodState.currentLodIndex = 0xFF;
        prefetched.lodState.currentBaseRank = -1;
        prefetched.lodState.faceFamilyIds.swap(slidePrefetchFamilyIds_);
        prefetched.lodState.currentFaceSlots.swap(slidePrefetchFaceSlots_);

        const bool rebuilt = RebuildSegmentFaceSlotsForLod(prefetched, 0, seg1FamilySlots_);
        if (rebuilt)
        {
            (void)prefetched.renderer->ApplyFaceTextureSlotsGlobal(prefetched.lodState.currentFaceSlots);
            ++runtimeFaceRemapsThisFrame_;
        }

        slideScratchRenderer_ = std::move(prefetched.renderer);
        slidePrefetchFamilyIds_.swap(prefetched.lodState.faceFamilyIds);
        slidePrefetchFaceSlots_.swap(prefetched.lodState.currentFaceSlots);
        slidePrefetchLod8Ready_ =
            rebuilt &&
            !HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_);
        return rebuilt;
    }

    const bool needsRendererBuild =
        (slidePrefetchSegmentId_ != segmentId) ||
        !slidePrefetchRenderer_ ||
        slidePrefetchFamilyIds_.empty();

    if (needsRendererBuild)
    {
        if (!slidePrefetchRenderer_)
        {
            if (slideScratchRenderer_)
            {
                slidePrefetchRenderer_ = std::move(slideScratchRenderer_);
            }
            else
            {
                slidePrefetchRenderer_ = std::make_unique<TrackRenderer>();
            }
        }
        if (!slidePrefetchRenderer_) return false;

        Vector3D center(0.0, 0.0, 0.0);
        TrackLowWorkU16Vector familyIds{};
        bool usedRdr = false;
        if (!BuildRendererFromRuntimeBlob(segmentId, *slidePrefetchRenderer_, &center, &familyIds, &usedRdr) ||
            familyIds.empty())
        {
            ResetSlidePrefetchState();
            return false;
        }
        if (usedRdr) ++runtimeRdrBuildsThisFrame_;
        else ++runtimeSdrBuildsThisFrame_;

        ConfigureStreamedRendererDefaults(*slidePrefetchRenderer_);
        slidePrefetchSegmentId_ = segmentId;
        slidePrefetchCenter_ = center;
        slidePrefetchFamilyIds_ = std::move(familyIds);
        slidePrefetchFaceSlots_.assign(slidePrefetchFamilyIds_.size(), -1);
        slidePrefetchLod8Ready_ = false;
    }

    if (!slidePrefetchRenderer_ || slidePrefetchFamilyIds_.empty()) return false;

    bool addedFamily = false;
    for (size_t fi = 0; fi < slidePrefetchFamilyIds_.size(); ++fi)
    {
        const uint16_t fam = slidePrefetchFamilyIds_[fi];
        if (fam == 0) continue;
        if (FindFamilySlot(seg1FamilySlots_, fam)) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = fam;
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        seg1FamilySlots_.push_back(slotEntry);
        addedFamily = true;
    }
    if (addedFamily) InvalidateFamilySlotIndex();

    SegmentRenderEntry prefetched{};
    prefetched.id = segmentId;
    prefetched.logicalSegmentCount = 1;
    prefetched.renderer = std::move(slidePrefetchRenderer_);
    prefetched.center = slidePrefetchCenter_;
    prefetched.lodState.ready = true;
    prefetched.lodState.hasPerFaceRankOffsets = false;
    prefetched.lodState.currentLodIndex = 0xFF;
    prefetched.lodState.currentBaseRank = -1;
    prefetched.lodState.faceFamilyIds.swap(slidePrefetchFamilyIds_);
    prefetched.lodState.currentFaceSlots.swap(slidePrefetchFaceSlots_);

    const bool rebuilt = RebuildSegmentFaceSlotsForLod(prefetched, 0, seg1FamilySlots_);
    if (rebuilt)
    {
        (void)prefetched.renderer->ApplyFaceTextureSlotsGlobal(prefetched.lodState.currentFaceSlots);
        ++runtimeFaceRemapsThisFrame_;
    }

    slidePrefetchRenderer_ = std::move(prefetched.renderer);
    slidePrefetchFamilyIds_.swap(prefetched.lodState.faceFamilyIds);
    slidePrefetchFaceSlots_.swap(prefetched.lodState.currentFaceSlots);
    slidePrefetchLod8Ready_ =
        rebuilt &&
        !HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_);
    return rebuilt;
}

bool TrackSystem::BuildSegmentIntoRenderer(int32_t segmentId,
                                           TrackRenderer& renderer,
                                           Vector3D& outCenter,
                                           FamilyIdVector& outFamilyIds)
{
    if (segmentId <= 0) return false;

    outFamilyIds.clear();
    bool usedRdr = false;
    if (!BuildRendererFromRuntimeBlob(segmentId, renderer, &outCenter, &outFamilyIds, &usedRdr)) return false;
    if (outFamilyIds.empty()) return false;
    if (usedRdr) ++runtimeRdrBuildsThisFrame_;
    else ++runtimeSdrBuildsThisFrame_;
    ConfigureStreamedRendererDefaults(renderer);
    return true;
}

bool TrackSystem::BuildSegmentIntoSlideScratch(int32_t segmentId,
                                               Vector3D& outCenter,
                                               FamilyIdVector& outFamilyIds)
{
    if (segmentId <= 0) return false;
    if (!slideScratchRenderer_) slideScratchRenderer_ = std::make_unique<TrackRenderer>();
    if (!slideScratchRenderer_) return false;
    if (!BuildSegmentIntoRenderer(segmentId, *slideScratchRenderer_, outCenter, outFamilyIds)) return false;
    return true;
}

void TrackSystem::PrimeRuntimeScratchCapacities()
{
    static TrackRuntimePackCache sTrackScratchPackCache{};
    const char* packCandidates[] = {
        "/CD/DATA/TRKRDR.BIN",
        "/CD/DATA/TRKRDR.BIN;1",
        "/DATA/TRKRDR.BIN",
        "/DATA/TRKRDR.BIN;1",
        "CD/DATA/TRKRDR.BIN",
        "CD/DATA/TRKRDR.BIN;1",
        "DATA/TRKRDR.BIN",
        "DATA/TRKRDR.BIN;1",
        "cd/data/TRKRDR.BIN",
        "cd/data/TRKRDR.BIN;1",
        "cd/data/trkrdr.bin",
        "cd/data/trkrdr.bin;1",
        "data/TRKRDR.BIN",
        "data/TRKRDR.BIN;1",
        "data/trkrdr.bin",
        "data/trkrdr.bin;1",
        "TRKRDR.BIN",
        "TRKRDR.BIN;1",
        "trkrdr.bin",
        "trkrdr.bin;1"
    };
    if (!LoadTrackRuntimePackToCart(packCandidates,
                                    sizeof(packCandidates) / sizeof(packCandidates[0]),
                                    sTrackScratchPackCache))
    {
        return;
    }

    const size_t maxFaces = std::max<size_t>(1u, static_cast<size_t>(sTrackScratchPackCache.view.header.maxFaceCount));
    const size_t maxVerts = std::max<size_t>(1u, static_cast<size_t>(sTrackScratchPackCache.view.header.maxVertexCount));

    if (g_rdrBuildScratch.verts.capacity() < maxVerts) g_rdrBuildScratch.verts.reserve(maxVerts);
    if (g_rdrBuildScratch.faces.capacity() < maxFaces) g_rdrBuildScratch.faces.reserve(maxFaces);
    if (g_rdrBuildScratch.attrs.capacity() < maxFaces) g_rdrBuildScratch.attrs.reserve(maxFaces);
    if (slideIncomingFamilyIdsScratch_.capacity() < maxFaces) slideIncomingFamilyIdsScratch_.reserve(maxFaces);
    if (slideIncomingFaceRankOffsetsScratch_.capacity() < maxFaces) slideIncomingFaceRankOffsetsScratch_.reserve(maxFaces);
    if (slideIncomingFaceSlotsScratch_.capacity() < maxFaces) slideIncomingFaceSlotsScratch_.reserve(maxFaces);
    if (slidePrefetchFamilyIds_.capacity() < maxFaces) slidePrefetchFamilyIds_.reserve(maxFaces);
    if (slidePrefetchFaceSlots_.capacity() < maxFaces) slidePrefetchFaceSlots_.reserve(maxFaces);
}

void TrackSystem::TryPrefetchUpcomingSegment()
{
    if (!segmentsReady_ || segmentRenderers_.empty() || totalSegmentCount_ == 0) return;

    const size_t windowCount = segmentRenderers_.size();
    const int32_t nextId = (windowDirection_ > 0)
        ? WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(windowCount),
                               totalSegmentCount_)
        : WrapSegmentIdToRange(activeWindowStartId_ - static_cast<int32_t>(windowCount),
                               totalSegmentCount_);
    if (nextId <= 0) return;
    const bool prefetchRendererResident = kEnableTrackRuntimeStabilization
        ? static_cast<bool>(slideScratchRenderer_)
        : static_cast<bool>(slidePrefetchRenderer_);
    if (slidePrefetchSegmentId_ == nextId &&
        prefetchRendererResident &&
        !slidePrefetchFamilyIds_.empty())
    {
        (void)BuildSegmentIntoPrefetch(nextId);
        return;
    }

    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    // In safe mode the previous threshold effectively disabled prefetch for the
    // whole lap. The runtime now lives far below the conservative planning floor,
    // so waiting for +48 KB of HWR headroom meant the next tail was always built
    // at the slide boundary instead of ahead of time.
    const size_t floorBytes = kEnableTrackRuntimeStabilization
        ? (4u * 1024u)
        : (slideScratchRenderer_
            ? (kWorkRamHardFloorBytes + (12u * 1024u))
            : (kWorkRamHardFloorBytes + (32u * 1024u)));
    if (freeValid && freeBytes <= floorBytes)
    {
        if (!kEnableTrackRuntimeStabilization)
        {
            TrimRuntimeBlobScratchCaches(true);
        }
        bool freeValidAfter = false;
        const size_t freeAfter = GetHighWorkRamFreeBytesSafe(&freeValidAfter);
        if (freeValidAfter && freeAfter <= floorBytes) return;
    }

    if (!BuildSegmentIntoPrefetch(nextId))
    {
        TrackLowWorkU16Vector familyIds{};
        if (!LoadRuntimeFamilyIdsForSegment(nextId, familyIds) || familyIds.empty()) return;
        slidePrefetchSegmentId_ = nextId;
        slidePrefetchCenter_ = Vector3D(0.0, 0.0, 0.0);
        slidePrefetchFamilyIds_ = std::move(familyIds);
        slidePrefetchFaceSlots_.clear();
        slidePrefetchRenderer_.reset();
        slidePrefetchLod8Ready_ = false;
    }
}

void TrackSystem::CaptureTrackTextureHeapBase()
{
    trackTextureHeapBase_ = SRL::VDP1::GetTextureCount();
    trackTextureHeapBaseValid_ = true;
}

bool TrackSystem::RebuildTrackTextureResidencyForWindow()
{
    if (segmentRenderers_.empty()) return false;
    if (!trackTextureHeapBaseValid_)
    {
        CaptureTrackTextureHeapBase();
    }
    if (!trackTextureHeapBaseValid_) return false;
    if (seg1FamilySlots_.empty() && !BuildTrackFamilyLodSlots(seg1FamilySlots_))
    {
        return false;
    }

    const bool savedReady = ready_;
    const uint8_t savedTextureUploads = textureUploadsThisFrame_;
    ready_ = false;
    textureUploadsThisFrame_ = 0;

    if (!kEnableTrackRuntimeStabilization)
    {
        SRL::VDP1::ResetTextureHeap(trackTextureHeapBase_);
        ReleaseTrackedPaletteBanks();
        ResetReusableTrackTextureSlots();
        for (auto& family : seg1FamilySlots_)
        {
            family.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
            family.workingRefs = { 0, 0, 0, 0 };
            family.unusedFrames = { 0, 0, 0, 0 };
        }
    }

    bool success = true;
    size_t logicalRank = 0;
    for (size_t rank = 0; rank < segmentRenderers_.size(); ++rank)
    {
        const int32_t segmentId = WrapSegmentIdToRange(
            activeWindowStartId_ + (windowDirection_ >= 0
                ? static_cast<int32_t>(rank)
                : -static_cast<int32_t>(rank)),
            totalSegmentCount_);
        if (segmentId <= 0)
        {
            success = false;
            break;
        }

        SegmentRenderEntry* entry = nullptr;
        for (auto& candidate : segmentRenderers_)
        {
            if (candidate.id == segmentId)
            {
                entry = &candidate;
                break;
            }
        }
        if (!entry || !entry->renderer || !entry->lodState.ready || entry->lodState.faceFamilyIds.empty())
        {
            success = false;
            break;
        }

        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const bool remapOk = !entry->lodState.hasPerFaceRankOffsets
            ? RebuildSegmentFaceSlotsForLod(*entry, desiredLodIndex, seg1FamilySlots_)
            : RebuildSegmentFaceSlotsForBaseRank(*entry, logicalRank, seg1FamilySlots_);
        if (!remapOk ||
            HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                               &entry->lodState.faceFamilyIds))
        {
            success = false;
            break;
        }

        (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
        ++runtimeFaceRemapsThisFrame_;
        entry->lodState.currentLodIndex = desiredLodIndex;
        entry->lodState.currentBaseRank = entry->lodState.hasPerFaceRankOffsets
            ? static_cast<int16_t>(logicalRank)
            : -1;
        logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
    }

    ready_ = savedReady;
    textureUploadsThisFrame_ = savedTextureUploads;

    if (!success)
    {
        return false;
    }

    if (!kEnableTrackRuntimeStabilization)
    {
        ++trackTextureRecycleCount_;
    }
    RefreshFamilyWorkingSet(false);
    return true;
}

bool TrackSystem::ShouldRecycleTrackTextureHeap() const
{
    if (kEnableTrackRuntimeStabilization) return false;
    if (!trackTextureHeapBaseValid_) return false;
    const uint16_t texCount = SRL::VDP1::GetTextureCount();
    if (texCount <= trackTextureHeapBase_) return false;

    const uint16_t trackUsed = static_cast<uint16_t>(texCount - trackTextureHeapBase_);
    // Recycle only by VDP1 texture pressure. Tying this to HWR free bytes causes
    // unnecessary texture thrash and visible "red faces" while slots are repopulated.
    constexpr uint16_t kTrackTexBudgetBeforeRecycle = 920u;
    constexpr uint16_t kHeapGuardSlots = 24u;
    if (texCount >= static_cast<uint16_t>(SRL_MAX_TEXTURES - kHeapGuardSlots)) return true;
    return trackUsed >= kTrackTexBudgetBeforeRecycle;
}

void TrackSystem::RecycleTrackTextureHeap()
{
    if (!trackTextureHeapBaseValid_) return;
    const uint16_t texBefore = SRL::VDP1::GetTextureCount();
    const size_t freeBefore = GetHighWorkRamFreeBytesSafe();
    SRL::VDP1::ResetTextureHeap(trackTextureHeapBase_);
    ReleaseTrackedPaletteBanks();
    ResetReusableTrackTextureSlots();
    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        seg1FamilySlots_[i].lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        seg1FamilySlots_[i].unusedFrames = { 0, 0, 0, 0 };
    }
    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        auto& lod = segmentRenderers_[i].lodState;
        lod.currentLodIndex = 0xFF;
        lod.currentBaseRank = -1;
        if (!lod.currentFaceSlots.empty())
        {
            lod.currentFaceSlots.assign(lod.currentFaceSlots.size(), -1);
        }
    }
    ++trackTextureRecycleCount_;
    SRL::Debug::Print(1, 23, "TRK tex recycle:%u base:%u tb:%u ta:%u fb:%u fa:%u",
                      static_cast<unsigned>(trackTextureRecycleCount_),
                      static_cast<unsigned>(trackTextureHeapBase_),
                      static_cast<unsigned>(texBefore),
                      static_cast<unsigned>(SRL::VDP1::GetTextureCount()),
                      static_cast<unsigned>(freeBefore),
                      static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
}

uint32_t TrackSystem::EstimateWorkRamRetainedBytes() const
{
    uint64_t bytes = 0;
    bytes += EstimateRuntimeBlobScratchBytes();
    bytes += VectorCapacityBytesSafe(segmentRenderers_);
    bytes += VectorCapacityBytesSafe(segmentHandles_);
    bytes += VectorCapacityBytesSafe(slideIncomingFaceSlotsScratch_);
    bytes += VectorCapacityBytesSafe(slidePrefetchFaceSlots_);
    bytes += VectorCapacityBytesSafe(seg1ComponentVerts_);
    bytes += VectorCapacityBytesSafe(seg1ComponentFaces_);
    bytes += VectorCapacityBytesSafe(seg1ComponentAttrs_);
    bytes += VectorCapacityBytesSafe(seg1SingleFaceSlots_);
    for (size_t li = 0; li < seg1RendererFaceSlotsByLod_.size(); ++li)
    {
        bytes += VectorCapacityBytesSafe(seg1RendererFaceSlotsByLod_[li]);
    }

    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        const auto& entry = segmentRenderers_[i];
        bytes += VectorCapacityBytesSafe(entry.lodState.currentFaceSlots);
        if (entry.renderer) bytes += entry.renderer->RetainedBytes();
    }
    if (slideScratchRenderer_) bytes += slideScratchRenderer_->RetainedBytes();
    if (slidePrefetchRenderer_) bytes += slidePrefetchRenderer_->RetainedBytes();

    return (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(bytes);
}

uint32_t TrackSystem::EstimateLowWorkRamRetainedBytes() const
{
    uint64_t bytes = 0;
    bytes += VectorCapacityBytesSafe(segmentCenterCatalog_);
    bytes += VectorCapacityBytesSafe(segmentEntries_);
    bytes += VectorCapacityBytesSafe(rawSegmentCatalog_);
    bytes += VectorCapacityBytesSafe(seg1FaceFamilyIds_);
    bytes += VectorCapacityBytesSafe(seg1FamilySlots_);
    bytes += VectorCapacityBytesSafe(slideIncomingFamilyIdsScratch_);
    bytes += VectorCapacityBytesSafe(slideIncomingFaceRankOffsetsScratch_);
    bytes += VectorCapacityBytesSafe(slidePrefetchFamilyIds_);
    bytes += VectorCapacityBytesSafe(g_seg1MapCache.faceFamily);
    bytes += VectorCapacityBytesSafe(seg1TgaCatalog_);
    for (size_t li = 0; li < seg1Texbanks_.size(); ++li)
    {
        bytes += VectorCapacityBytesSafe(seg1Texbanks_[li].entries);
    }
    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        const auto& entry = segmentRenderers_[i];
        bytes += VectorCapacityBytesSafe(entry.lodState.faceFamilyIds);
        bytes += VectorCapacityBytesSafe(entry.lodState.faceRankOffsets);
    }
    bytes += g_packedAssetCacheEntryBytesLwr;

    return (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(bytes);
}

bool TrackSystem::TrimWorkRamRetainedCapacities(bool aggressive, int32_t* outFreeDelta)
{
    bool freeValidBefore = false;
    const size_t freeBefore = GetHighWorkRamFreeBytesSafe(&freeValidBefore);
    bool trimmed = false;

    const size_t windowTarget = std::max<size_t>(
        segmentRenderers_.size(),
        std::max<size_t>(1u, static_cast<size_t>(fixedVisibleSegmentCap_)));

    trimmed |= TrimVectorSlack(segmentHandles_, windowTarget + 2u, aggressive);
    trimmed |= TrimVectorSlack(slideIncomingFaceSlotsScratch_, aggressive ? 0u : 96u, aggressive);
    trimmed |= TrimVectorSlack(slidePrefetchFaceSlots_, aggressive ? 0u : 128u, aggressive);

    if (aggressive && slidePrefetchRenderer_)
    {
        ResetSlidePrefetchState();
        trimmed = true;
    }

    if (!seg1ComponentEnabled_)
    {
        trimmed |= TrimVectorSlack(seg1ComponentVerts_, 0u, aggressive);
        trimmed |= TrimVectorSlack(seg1ComponentFaces_, 0u, aggressive);
        trimmed |= TrimVectorSlack(seg1ComponentAttrs_, 0u, aggressive);
        trimmed |= TrimVectorSlack(seg1FaceFamilyIds_, 0u, aggressive);
    }

    for (size_t li = 0; li < seg1RendererFaceSlotsByLod_.size(); ++li)
    {
        const size_t keep = aggressive ? 0u : (seg1RendererFaceSlotsByLod_[li].size() + 8u);
        trimmed |= TrimVectorSlack(seg1RendererFaceSlotsByLod_[li], keep, aggressive);
    }

    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        auto& lod = segmentRenderers_[i].lodState;
        trimmed |= TrimVectorSlack(lod.currentFaceSlots,
                                   aggressive ? lod.currentFaceSlots.size() : (lod.currentFaceSlots.size() + 8u),
                                   aggressive);
    }

    const size_t desiredRendererCap = windowTarget + 2u;
    if (segmentRenderers_.capacity() > desiredRendererCap &&
        (aggressive || segmentRenderers_.capacity() > (desiredRendererCap + 8u)))
    {
        std::vector<SegmentRenderEntry> compact{};
        compact.reserve(desiredRendererCap);
        for (size_t i = 0; i < segmentRenderers_.size(); ++i)
        {
            compact.push_back(std::move(segmentRenderers_[i]));
        }
        segmentRenderers_.swap(compact);
        trimmed = true;
    }

    if (trimmed)
    {
        InvalidateFamilySlotIndex();
        segmentHandles_ = BuildSegmentHandleTable();
    }

    if (outFreeDelta)
    {
        bool freeValidAfter = false;
        const size_t freeAfter = GetHighWorkRamFreeBytesSafe(&freeValidAfter);
        if (freeValidBefore && freeValidAfter)
        {
            *outFreeDelta = static_cast<int32_t>(freeAfter) - static_cast<int32_t>(freeBefore);
        }
        else
        {
            *outFreeDelta = 0;
        }
    }
    return trimmed;
}

bool TrackSystem::ValidateAndRepairWindowState()
{
    if (segmentRenderers_.empty())
    {
        segmentHandles_.clear();
        segmentPool_.Reset();
        return false;
    }

    bool repaired = false;
    if (segmentEntries_.size() != segmentRenderers_.size())
    {
        const size_t n = std::min(segmentEntries_.size(), segmentRenderers_.size());
        segmentEntries_.resize(n);
        segmentRenderers_.resize(n);
        repaired = true;
    }

    if (segmentRenderers_.size() > kTrackSegmentLimit)
    {
        segmentRenderers_.resize(kTrackSegmentLimit);
        segmentEntries_.resize(std::min(segmentEntries_.size(), segmentRenderers_.size()));
        repaired = true;
    }

    if (activeWindowHead_ >= segmentRenderers_.size())
    {
        activeWindowHead_ = 0;
        repaired = true;
    }

    if (totalSegmentCount_ > 0 && !segmentRenderers_.empty())
    {
        static TrackLowWorkU8Vector seen{};
        static TrackLowWorkU8Vector expected{};
        const size_t markCount = static_cast<size_t>(totalSegmentCount_) + 1u;
        seen.assign(markCount, 0u);
        expected.assign(markCount, 0u);
        size_t duplicateCount = 0;
        size_t unexpectedCount = 0;
        size_t missingCount = 0;
        int32_t expectedId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
        for (size_t i = 0; i < segmentRenderers_.size(); ++i)
        {
            if (expectedId > 0) expected[static_cast<size_t>(expectedId)] = 1u;
            expectedId = WrapSegmentIdToRange(expectedId + (windowDirection_ >= 0 ? 1 : -1),
                                              totalSegmentCount_);
        }
        for (size_t i = 0; i < segmentRenderers_.size(); ++i)
        {
            const int32_t sid = WrapSegmentIdToRange(segmentRenderers_[i].id, totalSegmentCount_);
            if (sid <= 0) continue;
            uint8_t& mark = seen[static_cast<size_t>(sid)];
            if (mark != 0u) ++duplicateCount;
            mark = 1u;
            if (expected[static_cast<size_t>(sid)] == 0u) ++unexpectedCount;
        }
        expectedId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
        for (size_t i = 0; i < segmentRenderers_.size(); ++i)
        {
            if (expectedId > 0 && seen[static_cast<size_t>(expectedId)] == 0u) ++missingCount;
            expectedId = WrapSegmentIdToRange(expectedId + (windowDirection_ >= 0 ? 1 : -1),
                                              totalSegmentCount_);
        }
        if (duplicateCount > 0 || unexpectedCount > 0 || missingCount > 0)
        {
            if (RebuildActiveSegmentWindow(activeWindowStartId_, segmentRenderers_.size(), windowDirection_))
            {
                SRL::Debug::Print(1, 21, "TRK cov ms:%u dp:%u ux:%u",
                                  static_cast<unsigned>(missingCount),
                                  static_cast<unsigned>(duplicateCount),
                                  static_cast<unsigned>(unexpectedCount));
                repaired = true;
            }
        }
    }

    bool rebuildHandles = (segmentHandles_.size() != segmentRenderers_.size());
    if (!rebuildHandles)
    {
        for (size_t i = 0; i < segmentHandles_.size(); ++i)
        {
            if (segmentPool_.Resolve(segmentHandles_[i])) continue;
            rebuildHandles = true;
            break;
        }
    }
    if (rebuildHandles)
    {
        segmentHandles_ = BuildSegmentHandleTable();
        repaired = true;
    }

    if (repaired)
    {
        ++workRamRepairCount_;
        SRL::Debug::Print(1, 17, "WM repair:%u n:%u",
                          static_cast<unsigned>(workRamRepairCount_),
                          static_cast<unsigned>(segmentRenderers_.size()));
    }
    return repaired;
}

void TrackSystem::EmitWorkRamLivePointersTelemetry()
{
    if (workRamTelemetryCooldown_ > 0)
    {
        --workRamTelemetryCooldown_;
        return;
    }
    workRamTelemetryCooldown_ = 20;

    uint32_t liveRenderers = 0;
    uint32_t validHandles = 0;
    uint32_t lodCapBytes = 0;
    uint32_t slotCapElements = 0;
    uintptr_t ptrHash = 0;

    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        const auto& entry = segmentRenderers_[i];
        if (entry.renderer)
        {
            ++liveRenderers;
            ptrHash ^= (reinterpret_cast<uintptr_t>(entry.renderer.get()) >> 4);
        }
        lodCapBytes += VectorCapacityBytesSafe(entry.lodState.faceFamilyIds);
        lodCapBytes += VectorCapacityBytesSafe(entry.lodState.faceRankOffsets);
        lodCapBytes += VectorCapacityBytesSafe(entry.lodState.currentFaceSlots);
        slotCapElements += VectorCapacityElementsSafe(entry.lodState.currentFaceSlots);
        if (!entry.lodState.faceFamilyIds.empty())
        {
            ptrHash ^= (reinterpret_cast<uintptr_t>(entry.lodState.faceFamilyIds.data()) >> 4);
        }
        if (!entry.lodState.currentFaceSlots.empty())
        {
            ptrHash ^= (reinterpret_cast<uintptr_t>(entry.lodState.currentFaceSlots.data()) >> 4);
        }
    }
    for (size_t i = 0; i < segmentHandles_.size(); ++i)
    {
        if (!segmentPool_.Resolve(segmentHandles_[i])) continue;
        ++validHandles;
    }
    if (slideScratchRenderer_)
    {
        ptrHash ^= (reinterpret_cast<uintptr_t>(slideScratchRenderer_.get()) >> 4);
    }

    slotCapElements += VectorCapacityElementsSafe(slideIncomingFaceSlotsScratch_);
    slotCapElements += VectorCapacityElementsSafe(slidePrefetchFaceSlots_);
    slotCapElements += VectorCapacityElementsSafe(seg1SingleFaceSlots_);
    for (size_t li = 0; li < seg1RendererFaceSlotsByLod_.size(); ++li)
    {
        slotCapElements += VectorCapacityElementsSafe(seg1RendererFaceSlotsByLod_[li]);
    }

    const uint32_t slotBytesNow = slotCapElements * static_cast<uint32_t>(sizeof(int16_t));
    const uint32_t slotBytesLegacy32 = slotCapElements * static_cast<uint32_t>(sizeof(int32_t));
    const uint32_t slotBytesSaved = slotBytesLegacy32 - slotBytesNow;

    const auto hwr = SRL::Memory::HighWorkRam::GetReport();
    const auto lwr = SRL::Memory::LowWorkRam::GetReport();
    const uint32_t hwrUsed = (hwr.TotalSize >= hwr.FreeSize)
        ? static_cast<uint32_t>(hwr.TotalSize - hwr.FreeSize)
        : 0u;
    const uint32_t lwrUsed = (lwr.TotalSize >= lwr.FreeSize)
        ? static_cast<uint32_t>(lwr.TotalSize - lwr.FreeSize)
        : 0u;

    SRL::Debug::Print(1, 17, "WM mem hf:%u hu:%u lf:%u lu:%u",
                      static_cast<unsigned>(hwr.FreeSize),
                      static_cast<unsigned>(hwrUsed),
                      static_cast<unsigned>(lwr.FreeSize),
                      static_cast<unsigned>(lwrUsed));
    SRL::Debug::Print(1, 18, "WM cap hb:%u lb:%u lc:%u     ",
                      static_cast<unsigned>(EstimateWorkRamRetainedBytes()),
                      static_cast<unsigned>(EstimateLowWorkRamRetainedBytes()),
                      static_cast<unsigned>(lodCapBytes));
    SRL::Debug::Print(1, 16, "WM slot e:%u b:%u sv:%u     ",
                      static_cast<unsigned>(slotCapElements),
                      static_cast<unsigned>(slotBytesNow),
                      static_cast<unsigned>(slotBytesSaved));
}

void TrackSystem::RunWorkRamMaintenance(bool windowSlid)
{
    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    const bool softLowMemory = freeValid && freeBytes <= (kWorkRamHardFloorBytes + (48u * 1024u));
    const bool criticalLowMemory = freeValid && freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u));
    if (kEnableTrackRuntimeStabilization)
    {
        if ((criticalLowMemory || softLowMemory) && workRamTrimCooldown_ == 0)
        {
            TrimRuntimeBlobScratchCaches(true);
            int32_t freeDelta = 0;
            const bool trimmed = TrimWorkRamRetainedCapacities(true, &freeDelta);
            if (trimmed)
            {
                SRL::Debug::Print(1, 17, "WM trim a:%u df:%d f:%u",
                                  1u,
                                  static_cast<int>(freeDelta),
                                  static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
            }
            workRamTrimCooldown_ = criticalLowMemory ? 1u : 6u;
        }
        if (workRamTrimCooldown_ > 0)
        {
            --workRamTrimCooldown_;
        }
        return;
    }

    if (criticalLowMemory)
    {
        ResetSlidePrefetchState();
        if (slideScratchRenderer_)
        {
            slideScratchRenderer_->RecycleRuntimeState();
        }
        TrimRuntimeBlobScratchCaches(true);
    }

    bool trimmed = false;
    if (criticalLowMemory || (softLowMemory && workRamTrimCooldown_ == 0) || (windowSlid && workRamTrimCooldown_ == 0))
    {
        const bool aggressiveTrim = criticalLowMemory || softLowMemory;
        int32_t freeDelta = 0;
        trimmed = TrimWorkRamRetainedCapacities(aggressiveTrim, &freeDelta);
        TrimRuntimeBlobScratchCaches(aggressiveTrim);
        if (trimmed)
        {
            SRL::Debug::Print(1, 17, "WM trim a:%u df:%d f:%u",
                              aggressiveTrim ? 1u : 0u,
                              static_cast<int>(freeDelta),
                              static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
        }
        if (trimmed || criticalLowMemory)
        {
            (void)ValidateAndRepairWindowState();
            EmitWorkRamLivePointersTelemetry();
        }
        workRamTrimCooldown_ = aggressiveTrim ? 1u : 6u;
    }
    else if (workRamTrimCooldown_ > 0)
    {
        --workRamTrimCooldown_;
    }

    if ((windowSlid || criticalLowMemory) && ShouldRecycleTrackTextureHeap())
    {
        RecycleTrackTextureHeap();
    }
}

bool TrackSystem::SlideActiveSegmentWindow(size_t stepCount, int8_t direction)
{
    if (stepCount == 0) return true;
    if (!segmentsReady_ || segmentRenderers_.empty()) return false;
    if (totalSegmentCount_ == 0) return false;
    direction = (direction < 0) ? -1 : 1;

    const size_t windowCount = segmentRenderers_.size();
    if (windowCount == 0) return false;
    if (activeWindowHead_ >= windowCount) activeWindowHead_ = 0;

    if (windowDirection_ != direction)
    {
        const int32_t newStartId = WrapSegmentIdToRange(
            activeWindowStartId_ + static_cast<int32_t>(direction),
            totalSegmentCount_);
        if (newStartId <= 0) return false;
        if (!RebuildActiveSegmentWindow(newStartId, windowCount, direction))
        {
            return false;
        }
        TryPrefetchUpcomingSegment();
        if (stepCount <= 1) return true;
        return SlideActiveSegmentWindow(stepCount - 1, direction);
    }

    auto finalizeSlideWindow = [&]() -> bool
    {
        segmentsReady_ = !segmentRenderers_.empty();
        bool handlesOk = !kEnableTrackRuntimeStabilization &&
                         (segmentHandles_.size() == segmentRenderers_.size());
        if (handlesOk)
        {
            for (size_t i = 0; i < segmentHandles_.size(); ++i)
            {
                if (segmentPool_.Resolve(segmentHandles_[i])) continue;
                handlesOk = false;
                break;
            }
        }
        if (!handlesOk || kEnableTrackRuntimeStabilization)
        {
            segmentHandles_ = BuildSegmentHandleTable();
        }
        TryPrefetchUpcomingSegment();
        const int32_t endId = WrapSegmentIdToRange(
            activeWindowStartId_ + (windowDirection_ > 0
                ? static_cast<int32_t>(segmentRenderers_.size()) - 1
                : -(static_cast<int32_t>(segmentRenderers_.size()) - 1)),
            totalSegmentCount_);
        bool winFreeValid = false;
        const size_t winFree = GetHighWorkRamFreeBytesSafe(&winFreeValid);
        SRL::Debug::Print(1, 14, "WIN %d..%d n:%u d:%d free:%u ok:%u",
                          activeWindowStartId_,
                          endId,
                          static_cast<unsigned>(segmentRenderers_.size()),
                          static_cast<int>(windowDirection_),
                          static_cast<unsigned>(winFree),
                          winFreeValid ? 1u : 0u);
        if (winFreeValid)
        {
            int32_t dFree = 0;
            if (lastWindowFreeValid_)
            {
                dFree = static_cast<int32_t>(winFree) - static_cast<int32_t>(lastWindowFreeBytes_);
            }
            SRL::Debug::Print(1, 15, "WIN dFree:%d", static_cast<int>(dFree));
            lastWindowFreeBytes_ = winFree;
            lastWindowFreeValid_ = true;
        }
        else
        {
            lastWindowFreeValid_ = false;
            lastWindowFreeBytes_ = 0;
        }
        uint32_t trkBytes = 0;
        for (size_t i = 0; i < segmentRenderers_.size(); ++i)
        {
            if (!segmentRenderers_[i].renderer) continue;
            trkBytes += segmentRenderers_[i].renderer->RetainedBytes();
        }
        if (slideScratchRenderer_) trkBytes += slideScratchRenderer_->RetainedBytes();
        const uint16_t texCount = SRL::VDP1::GetTextureCount();
        const uint16_t trackTexUsed = (trackTextureHeapBaseValid_ && texCount > trackTextureHeapBase_)
            ? static_cast<uint16_t>(texCount - trackTextureHeapBase_)
            : 0u;
        uint32_t slotCapElements = 0;
        for (size_t i = 0; i < segmentRenderers_.size(); ++i)
        {
            slotCapElements += VectorCapacityElementsSafe(segmentRenderers_[i].lodState.currentFaceSlots);
        }
        slotCapElements += VectorCapacityElementsSafe(slideIncomingFaceSlotsScratch_);
        slotCapElements += VectorCapacityElementsSafe(slidePrefetchFaceSlots_);
        slotCapElements += VectorCapacityElementsSafe(seg1SingleFaceSlots_);
        for (size_t li = 0; li < seg1RendererFaceSlotsByLod_.size(); ++li)
        {
            slotCapElements += VectorCapacityElementsSafe(seg1RendererFaceSlotsByLod_[li]);
        }
        const uint32_t slotBytesNow = slotCapElements * static_cast<uint32_t>(sizeof(int16_t));
        const uint32_t slotBytesSaved = slotCapElements * static_cast<uint32_t>(sizeof(int16_t));
        const auto hwr = SRL::Memory::HighWorkRam::GetReport();
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        SRL::Debug::Print(1, 16, "WM trk:%u tx:%u hf:%u lf:%u",
                          static_cast<unsigned>(trkBytes),
                          static_cast<unsigned>(texCount),
                          static_cast<unsigned>(hwr.FreeSize),
                          static_cast<unsigned>(lwr.FreeSize));
        SRL::Debug::Print(1, 15, "WM tex tu:%u fs:%u sv:%u     ",
                          static_cast<unsigned>(trackTexUsed),
                          static_cast<unsigned>(slotBytesNow),
                          static_cast<unsigned>(slotBytesSaved));
        return segmentsReady_;
    };

    if (kEnableTrackRuntimeStabilization)
    {
        for (size_t step = 0; step < stepCount; ++step)
        {
            bool freeValid = false;
            const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
            if (freeValid && freeBytes <= kWorkRamHardFloorBytes)
            {
                SRL::Debug::Print(1, 11, "PKG slide low step:%u free:%u ok:%u",
                                  static_cast<unsigned>(step + 1),
                                  static_cast<unsigned>(freeBytes),
                                  freeValid ? 1u : 0u);
            }

            const int32_t nextId = (direction > 0)
                ? WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(windowCount),
                                       totalSegmentCount_)
                : WrapSegmentIdToRange(activeWindowStartId_ - static_cast<int32_t>(windowCount),
                                       totalSegmentCount_);
            if (nextId <= 0) return false;

            const int32_t outgoingId = (direction > 0)
                ? WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_)
                : WrapSegmentIdToRange(activeWindowStartId_ - (static_cast<int32_t>(windowCount) - 1),
                                       totalSegmentCount_);
            size_t dropIdx = activeWindowHead_ % windowCount;
            bool foundOutgoing = false;
            for (size_t i = 0; i < windowCount; ++i)
            {
                if (segmentRenderers_[i].id != outgoingId) continue;
                dropIdx = i;
                foundOutgoing = true;
                break;
            }
            if (!foundOutgoing)
            {
                dropIdx = (direction > 0)
                    ? (activeWindowHead_ % windowCount)
                    : ((activeWindowHead_ + windowCount - 1) % windowCount);
            }

            const int32_t nextStartId = WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(direction),
                                                             totalSegmentCount_);
            if (nextStartId <= 0) return false;

            Vector3D center(0.0, 0.0, 0.0);
            slideIncomingFamilyIdsScratch_.clear();
            slideIncomingFaceSlotsScratch_.clear();
            auto prefetchReady = [&]() -> bool
            {
        if (slidePrefetchSegmentId_ != nextId) return false;
        if (kEnableTrackRuntimeStabilization)
        {
            if (!slideScratchRenderer_) return false;
        }
        else
        {
            if (!slidePrefetchRenderer_) return false;
        }
        if (slidePrefetchFamilyIds_.empty()) return false;
        if (!slidePrefetchLod8Ready_) return false;
        if (slidePrefetchFaceSlots_.size() != slidePrefetchFamilyIds_.size()) return false;
        return !HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_);
            };
            const bool usePrefetchedRenderer = prefetchReady();
            if (usePrefetchedRenderer)
            {
                center = slidePrefetchCenter_;
                slideIncomingFamilyIdsScratch_.swap(slidePrefetchFamilyIds_);
                slideIncomingFaceSlotsScratch_.swap(slidePrefetchFaceSlots_);
                ++runtimePrefetchHitsThisFrame_;
            }

            SegmentRenderEntry& slot = segmentRenderers_[dropIdx];
            TrackSegmentEntry& meta = segmentEntries_[dropIdx];
            if (!usePrefetchedRenderer)
            {
                if (!slideScratchRenderer_) slideScratchRenderer_ = std::make_unique<TrackRenderer>();
                if (!slideScratchRenderer_ || !slot.renderer)
                {
                    ++runtimeSlideStallsThisFrame_;
                    SRL::Debug::Print(1, 11, "PKG slide slot fail id:%d free:%u",
                                      nextId,
                                      static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
                    return false;
                }
            }
            const int32_t previousSlotId = slot.id;
            const uint8_t previousLogicalSegmentCount = slot.logicalSegmentCount;
            const Vector3D previousCenter = slot.center;
            const bool previousLodReady = slot.lodState.ready;
            const bool previousHasPerFaceRankOffsets = slot.lodState.hasPerFaceRankOffsets;
            const uint8_t previousCurrentLodIndex = slot.lodState.currentLodIndex;
            const int16_t previousCurrentBaseRank = slot.lodState.currentBaseRank;
            TrackLowWorkU16Vector previousFaceFamilyIds{};
            TrackLowWorkU8Vector previousFaceRankOffsets{};
            std::vector<int16_t> previousFaceSlots{};
            previousFaceFamilyIds.swap(slot.lodState.faceFamilyIds);
            previousFaceRankOffsets.swap(slot.lodState.faceRankOffsets);
            previousFaceSlots.swap(slot.lodState.currentFaceSlots);
            const int32_t previousMetaId = meta.id;
            auto rollbackIncomingBuild = [&]()
            {
                if (usePrefetchedRenderer)
                {
                    std::unique_ptr<TrackRenderer> failedIncomingRenderer = std::move(slot.renderer);
                    slot.renderer = std::move(slideScratchRenderer_);
                    slideScratchRenderer_ = std::move(failedIncomingRenderer);
                    return;
                }
                if (slot.renderer) slot.renderer->RecycleRuntimeState();
                std::swap(slot.renderer, slideScratchRenderer_);
            };

            if (usePrefetchedRenderer)
            {
                std::unique_ptr<TrackRenderer> droppedRenderer = std::move(slot.renderer);
                if (kEnableTrackRuntimeStabilization)
                {
                    slot.renderer = std::move(slideScratchRenderer_);
                }
                else
                {
                    slot.renderer = std::move(slidePrefetchRenderer_);
                }
                slideScratchRenderer_ = std::move(droppedRenderer);
            }
            else
            {
                std::swap(slot.renderer, slideScratchRenderer_);
                auto buildIncomingDirect = [&]() -> bool
                {
                    if (BuildSegmentIntoRenderer(nextId, *slot.renderer, center, slideIncomingFamilyIdsScratch_) &&
                        !slideIncomingFamilyIdsScratch_.empty())
                    {
                        return true;
                    }
                    if (slot.renderer) slot.renderer->RecycleRuntimeState();
                    TrimRuntimeBlobScratchCaches(true);
                    int32_t freeDelta = 0;
                    (void)TrimWorkRamRetainedCapacities(true, &freeDelta);
                    return BuildSegmentIntoRenderer(nextId, *slot.renderer, center, slideIncomingFamilyIdsScratch_) &&
                           !slideIncomingFamilyIdsScratch_.empty();
                };

                if (!buildIncomingDirect())
                {
                    rollbackIncomingBuild();
                    ++runtimeSlideStallsThisFrame_;
                    ++runtimePrefetchMissesThisFrame_;
                    SRL::Debug::Print(1, 11, "PKG slide wait id:%d d:%d free:%u",
                                      nextId,
                                      static_cast<int>(direction),
                                      static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
                    return false;
                }

                ++runtimePrefetchMissesThisFrame_;
                slideIncomingFaceSlotsScratch_.assign(slideIncomingFamilyIdsScratch_.size(), -1);
            }

            slot.id = nextId;
            slot.logicalSegmentCount = 1;
            slot.center = center;
            slot.lodState.ready = true;
            slot.lodState.hasPerFaceRankOffsets = false;
            slot.lodState.currentBaseRank = -1;
            slot.lodState.currentLodIndex = 0xFF;
            slot.lodState.faceFamilyIds.swap(slideIncomingFamilyIdsScratch_);
            slot.lodState.faceRankOffsets.assign(slot.lodState.faceFamilyIds.size(), 0u);
            slot.lodState.currentFaceSlots.swap(slideIncomingFaceSlotsScratch_);
            meta.id = nextId;

            bool addedFamily = false;
            for (const uint16_t fam : slot.lodState.faceFamilyIds)
            {
                if (fam == 0) continue;
                if (FindFamilySlot(seg1FamilySlots_, fam)) continue;
                Seg1FamilySlotEntry slotEntry{};
                slotEntry.familyId = fam;
                slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
                seg1FamilySlots_.push_back(slotEntry);
                addedFamily = true;
            }
            if (addedFamily) InvalidateFamilySlotIndex();

            const bool incomingSlotsReady = usePrefetchedRenderer
                ? !HasMissingRequiredFaceTextureSlots(slot.lodState.currentFaceSlots,
                                                      &slot.lodState.faceFamilyIds)
                : (RebuildSegmentFaceSlotsForLod(slot, 0, seg1FamilySlots_) &&
                   !HasMissingRequiredFaceTextureSlots(slot.lodState.currentFaceSlots,
                                                      &slot.lodState.faceFamilyIds));
            if (!incomingSlotsReady)
            {
                rollbackIncomingBuild();
                slot.id = previousSlotId;
                slot.logicalSegmentCount = previousLogicalSegmentCount;
                slot.center = previousCenter;
                slot.lodState.ready = previousLodReady;
                slot.lodState.hasPerFaceRankOffsets = previousHasPerFaceRankOffsets;
                slot.lodState.currentLodIndex = previousCurrentLodIndex;
                slot.lodState.currentBaseRank = previousCurrentBaseRank;
                slot.lodState.faceFamilyIds.swap(previousFaceFamilyIds);
                slot.lodState.faceRankOffsets.swap(previousFaceRankOffsets);
                slot.lodState.currentFaceSlots.swap(previousFaceSlots);
                meta.id = previousMetaId;
                if (slot.renderer && !slot.lodState.currentFaceSlots.empty())
                {
                    (void)slot.renderer->ApplyFaceTextureSlotsGlobal(slot.lodState.currentFaceSlots);
                }
                ++runtimeSlideStallsThisFrame_;
                SRL::Debug::Print(1, 11, "PKG slide map fail id:%d free:%u",
                                  nextId,
                                  static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
                return false;
            }
            (void)slot.renderer->ApplyFaceTextureSlotsGlobal(slot.lodState.currentFaceSlots);
            ++runtimeFaceRemapsThisFrame_;
            slot.lodState.currentLodIndex = 0;

            ResetSlidePrefetchState();
            activeWindowStartId_ = nextStartId;
            bool foundStart = false;
            for (size_t i = 0; i < windowCount; ++i)
            {
                if (segmentRenderers_[i].id != activeWindowStartId_) continue;
                activeWindowHead_ = i;
                foundStart = true;
                break;
            }
            if (!foundStart)
            {
                activeWindowHead_ = dropIdx % windowCount;
            }
            RefreshFamilyWorkingSet(false);
            segmentHandles_ = BuildSegmentHandleTable();
            ++runtimeSlidesThisFrame_;
        }

        return finalizeSlideWindow();
    }

    for (size_t step = 0; step < stepCount; ++step)
    {
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        if (freeValid && freeBytes <= kWorkRamHardFloorBytes)
        {
            SRL::Debug::Print(1, 11, "PKG slide low step:%u free:%u ok:%u",
                              static_cast<unsigned>(step + 1),
                              static_cast<unsigned>(freeBytes),
                              freeValid ? 1u : 0u);
        }
        if (freeValid && freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u)))
        {
            // In safe mode keep the incoming tail prepared and keep the scratch
            // renderer hot; otherwise we turn every near-floor frame into a
            // prefetch miss on the next slide.
            TrimRuntimeBlobScratchCaches(true);
            int32_t freeDelta = 0;
            if (TrimWorkRamRetainedCapacities(true, &freeDelta))
            {
                (void)ValidateAndRepairWindowState();
                SRL::Debug::Print(1, 12, "PKG slide trim df:%d free:%u",
                                  static_cast<int>(freeDelta),
                                  static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
            }
            if (ShouldRecycleTrackTextureHeap())
            {
                RecycleTrackTextureHeap();
            }
        }

        const int32_t nextId = (direction > 0)
            ? WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(windowCount),
                                   totalSegmentCount_)
            : WrapSegmentIdToRange(activeWindowStartId_ - static_cast<int32_t>(windowCount),
                                   totalSegmentCount_);
        if (nextId <= 0) return false;

        const int32_t outgoingId = (direction > 0)
            ? WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_)
            : WrapSegmentIdToRange(activeWindowStartId_ - (static_cast<int32_t>(windowCount) - 1),
                                   totalSegmentCount_);
        size_t dropIdx = activeWindowHead_ % windowCount;
        bool foundOutgoing = false;
        for (size_t i = 0; i < windowCount; ++i)
        {
            if (segmentRenderers_[i].id != outgoingId) continue;
            dropIdx = i;
            foundOutgoing = true;
            break;
        }
        if (!foundOutgoing)
        {
            dropIdx = (direction > 0)
                ? (activeWindowHead_ % windowCount)
                : ((activeWindowHead_ + windowCount - 1) % windowCount);
        }

        Vector3D center(0.0, 0.0, 0.0);
        slideIncomingFamilyIdsScratch_.clear();
        slideIncomingFaceSlotsScratch_.clear();
        auto prefetchReady = [&]() -> bool
        {
            if (slidePrefetchSegmentId_ != nextId) return false;
            if (kEnableTrackRuntimeStabilization)
            {
                if (!slideScratchRenderer_) return false;
            }
            else if (!slidePrefetchRenderer_) return false;
            if (slidePrefetchFamilyIds_.empty()) return false;
            if (!slidePrefetchLod8Ready_) return false;
            if (slidePrefetchFaceSlots_.size() != slidePrefetchFamilyIds_.size()) return false;
            return !HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_);
        };

        auto tryResolvePrefetchFallbackSlots = [&]() -> bool
        {
            if (slidePrefetchSegmentId_ != nextId) return false;
            if (kEnableTrackRuntimeStabilization)
            {
                if (!slideScratchRenderer_) return false;
            }
            else if (!slidePrefetchRenderer_) return false;
            if (slidePrefetchFamilyIds_.empty()) return false;

            if (slidePrefetchFaceSlots_.size() != slidePrefetchFamilyIds_.size())
            {
                slidePrefetchFaceSlots_.assign(slidePrefetchFamilyIds_.size(), -1);
            }

            // Critical path: when the next segment is about to enter the window,
            // allow a short synchronous warmup for missing families so the slide
            // can progress instead of stalling indefinitely on id N+20.
            constexpr size_t kCriticalSlideWarmupCap = 8u;
            size_t warmedFamilies = 0;
            for (size_t fi = 0; fi < slidePrefetchFamilyIds_.size(); ++fi)
            {
                if (slidePrefetchFaceSlots_[fi] >= 0) continue;
                const uint16_t fam = slidePrefetchFamilyIds_[fi];
                if (fam == 0) continue;

                Seg1FamilySlotEntry* slotEntry = FindFamilySlot(seg1FamilySlots_, fam);
                if (!slotEntry)
                {
                    Seg1FamilySlotEntry init{};
                    init.familyId = fam;
                    init.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
                    seg1FamilySlots_.push_back(init);
                    InvalidateFamilySlotIndex();
                    slotEntry = &seg1FamilySlots_.back();
                }
                if (!slotEntry) continue;

                bool hasLiveSlot = false;
                for (int li = 0; li < 4; ++li)
                {
                    if (IsVdp1TextureSlotLive(slotEntry->lodSlots[static_cast<size_t>(li)]))
                    {
                        hasLiveSlot = true;
                        break;
                    }
                }
                if (!hasLiveSlot && warmedFamilies < kCriticalSlideWarmupCap)
                {
                    const bool loaded = TryLoadFamilyLodSlot(*slotEntry,
                                                             0,
                                                             /*fallbackToLowerLods*/true,
                                                             /*fallbackToHigherLods*/true,
                                                             nullptr,
                                                             nullptr,
                                                             nullptr,
                                                             nullptr);
                    if (loaded)
                    {
                        ++warmedFamilies;
                    }
                }

                int32_t resolved = -1;
                for (int li = 0; li < 4; ++li)
                {
                    const uint16_t slot = slotEntry->lodSlots[static_cast<size_t>(li)];
                    if (!IsVdp1TextureSlotLive(slot)) continue;
                    resolved = static_cast<int32_t>(slot);
                    break;
                }
                if (resolved >= 0)
                {
                    slidePrefetchFaceSlots_[fi] = resolved;
                }
            }

            if (HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_)) return false;
            if (kEnableTrackRuntimeStabilization)
            {
                (void)slideScratchRenderer_->ApplyFaceTextureSlotsGlobal(slidePrefetchFaceSlots_);
            }
            else
            {
                (void)slidePrefetchRenderer_->ApplyFaceTextureSlotsGlobal(slidePrefetchFaceSlots_);
            }
            ++runtimeFaceRemapsThisFrame_;
            return true;
        };

        bool usePrefetchedRenderer = prefetchReady();
        bool usedFallbackSlots = false;
        bool incomingLod8Ready = false;
        if (!usePrefetchedRenderer)
        {
            ++runtimePrefetchMissesThisFrame_;
            TryPrefetchUpcomingSegment();
            if (!kEnableTrackRuntimeStabilization)
            {
                (void)BuildSegmentIntoPrefetch(nextId);
            }
            else
            {
                (void)BuildSegmentIntoPrefetch(nextId);
            }
            PrewarmNextSegmentLod8();
            usePrefetchedRenderer = prefetchReady();
            if (!usePrefetchedRenderer)
            {
                if (kEnableTrackRuntimeStabilization)
                {
                    (void)BuildSegmentIntoPrefetch(nextId);
                    usePrefetchedRenderer = prefetchReady();
                }

                if (!tryResolvePrefetchFallbackSlots())
                {
                    ++runtimeSlideStallsThisFrame_;
                    const size_t stallFree = GetHighWorkRamFreeBytesSafe();
                    SRL::Debug::Print(1, 11, "PKG slide wait id:%d d:%d free:%u",
                                      nextId,
                                      static_cast<int>(direction),
                                      static_cast<unsigned>(stallFree));
                    return false;
                }
                usedFallbackSlots = true;
            }
        }
        else
        {
            ++runtimePrefetchHitsThisFrame_;
        }

        center = slidePrefetchCenter_;
        slideIncomingFamilyIdsScratch_ = slidePrefetchFamilyIds_;
        slideIncomingFaceSlotsScratch_ = slidePrefetchFaceSlots_;
        incomingLod8Ready = !usedFallbackSlots &&
                            !HasMissingRequiredFaceTextureSlots(slideIncomingFaceSlotsScratch_,
                                                                &slideIncomingFamilyIdsScratch_);
        SegmentRenderEntry& slot = segmentRenderers_[dropIdx];
        TrackSegmentEntry& meta = segmentEntries_[dropIdx];

        std::unique_ptr<TrackRenderer> droppedRenderer = std::move(slot.renderer);
        if (kEnableTrackRuntimeStabilization)
        {
            slot.renderer = std::move(slideScratchRenderer_);
            slideScratchRenderer_ = std::move(droppedRenderer);
        }
        else
        {
            slot.renderer = std::move(slidePrefetchRenderer_);
            slideScratchRenderer_ = std::move(droppedRenderer);
        }
        slot.id = nextId;
        slot.logicalSegmentCount = 1;
        slot.center = center;
        slot.lodState.ready = true;
        slot.lodState.hasPerFaceRankOffsets = false;
        slot.lodState.currentBaseRank = -1;
        const size_t incomingFaceCount = slideIncomingFamilyIdsScratch_.size();
        slideIncomingFaceRankOffsetsScratch_.assign(incomingFaceCount, 0);
        if (slideIncomingFaceSlotsScratch_.size() != incomingFaceCount)
        {
            slideIncomingFaceSlotsScratch_.assign(incomingFaceCount, -1);
            incomingLod8Ready = false;
        }

        // Recycle per-slot vectors by swapping with reusable scratch buffers.
        slot.lodState.faceFamilyIds.swap(slideIncomingFamilyIdsScratch_);
        slot.lodState.faceRankOffsets.swap(slideIncomingFaceRankOffsetsScratch_);
        slot.lodState.currentFaceSlots.swap(slideIncomingFaceSlotsScratch_);
        slot.lodState.currentLodIndex = incomingLod8Ready ? 0u : 0xFF;
        meta.id = nextId;

        // Incremental family cache update: avoid full-window rebuild scan every slide.
        bool addedFamily = false;
        for (size_t fi = 0; fi < slot.lodState.faceFamilyIds.size(); ++fi)
        {
            const uint16_t fam = slot.lodState.faceFamilyIds[fi];
            if (fam == 0) continue;
            if (FindFamilySlot(seg1FamilySlots_, fam)) continue;
            Seg1FamilySlotEntry slotEntry{};
            slotEntry.familyId = fam;
            slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
            seg1FamilySlots_.push_back(slotEntry);
            addedFamily = true;
        }
        if (addedFamily) InvalidateFamilySlotIndex();

        // Keep family/palette state bounded to the active window plus prefetch.
        if (fullTrackFamilyCacheReady_ || familyMergeCooldown_ == 0 || seg1FamilySlots_.size() > 1024)
        {
            MergeCurrentWindowFamilies();
            familyMergeCooldown_ = fullTrackFamilyCacheReady_ ? 1u : 6u;
        }
        else
        {
            --familyMergeCooldown_;
        }
        ResetSlidePrefetchState();
        activeWindowStartId_ = WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(direction),
                                                    totalSegmentCount_);
        if (activeWindowStartId_ <= 0) return false;
        bool foundStart = false;
        for (size_t i = 0; i < windowCount; ++i)
        {
            if (segmentRenderers_[i].id != activeWindowStartId_) continue;
            activeWindowHead_ = i;
            foundStart = true;
            break;
        }
        if (!foundStart)
        {
            activeWindowHead_ = dropIdx % windowCount;
        }
        if (prewarmCooldown_ == 0)
        {
            PrewarmNextSegmentLod8();
            prewarmCooldown_ = 2;
        }
        else
        {
            --prewarmCooldown_;
        }
        ++runtimeSlidesThisFrame_;
    }

    return finalizeSlideWindow();
}

void TrackSystem::PrewarmNextSegmentLod8()
{
    if (kEnableTrackRuntimeStabilization) return;
    if (totalSegmentCount_ == 0 || segmentRenderers_.empty()) return;
    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    if (freeValid && freeBytes <= (kWorkRamHardFloorBytes + (16u * 1024u)))
    {
        TrimRuntimeBlobScratchCaches(true);
        bool freeValidAfter = false;
        const size_t freeAfter = GetHighWorkRamFreeBytesSafe(&freeValidAfter);
        if (freeValidAfter && freeAfter <= (kWorkRamHardFloorBytes + (16u * 1024u))) return;
    }
    const size_t windowCount = segmentRenderers_.size();
    const int32_t preloadId = (windowDirection_ > 0)
        ? WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(windowCount),
                               totalSegmentCount_)
        : WrapSegmentIdToRange(activeWindowStartId_ - static_cast<int32_t>(windowCount),
                               totalSegmentCount_);
    if (preloadId <= 0) return;

    static TrackLowWorkU16Vector sPrewarmFamilies{};
    sPrewarmFamilies.clear();
    if (slidePrefetchSegmentId_ == preloadId && !slidePrefetchFamilyIds_.empty())
    {
        sPrewarmFamilies = slidePrefetchFamilyIds_;
    }
    else if (!LoadRuntimeFamilyIdsForSegment(preloadId, sPrewarmFamilies) || sPrewarmFamilies.empty())
    {
        return;
    }

    // Keep prewarm bounded to reduce per-slide spikes.
    const uint16_t texCount = SRL::VDP1::GetTextureCount();
    const bool nearHeapLimit = texCount >= static_cast<uint16_t>(SRL_MAX_TEXTURES - 96);
    const size_t remainingUploadBudget =
        (textureUploadsThisFrame_ < kTextureUploadsBudgetPerFrame)
            ? static_cast<size_t>(kTextureUploadsBudgetPerFrame - textureUploadsThisFrame_)
            : 0u;
    const size_t kPrewarmFamilyCapPerSlide = nearHeapLimit ? 1u : 2u;
    const size_t prewarmCap = std::min(kPrewarmFamilyCapPerSlide, remainingUploadBudget);
    if (prewarmCap == 0) return;
    size_t warmed = 0;
    for (size_t i = 0; i < sPrewarmFamilies.size(); ++i)
    {
        const uint16_t fam = sPrewarmFamilies[i];
        if (fam == 0) continue;

        Seg1FamilySlotEntry* slotEntry = FindFamilySlot(seg1FamilySlots_, fam);
        if (!slotEntry)
        {
            Seg1FamilySlotEntry init{};
            init.familyId = fam;
            init.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
            seg1FamilySlots_.push_back(init);
            InvalidateFamilySlotIndex();
            slotEntry = &seg1FamilySlots_.back();
        }
        if (!slotEntry) continue;
        if (slotEntry->lodSlots[0] != No_Texture && IsVdp1TextureSlotLive(slotEntry->lodSlots[0])) continue;

        if (EnsureFamilyLodSlotLoaded(seg1FamilySlots_, fam, 0))
        {
            ++warmed;
            if (warmed >= prewarmCap) break;
        }
    }

    const bool prefetchRendererResident = kEnableTrackRuntimeStabilization
        ? static_cast<bool>(slideScratchRenderer_)
        : static_cast<bool>(slidePrefetchRenderer_);
    if (slidePrefetchSegmentId_ == preloadId &&
        prefetchRendererResident &&
        !slidePrefetchFamilyIds_.empty())
    {
        (void)BuildSegmentIntoPrefetch(preloadId);
    }
}

void TrackSystem::MergeCurrentWindowFamilies()
{
    FamilySlotVector currentWindowFamilies{};
    (void)BuildTrackFamilyLodSlots(currentWindowFamilies);

    for (size_t fi = 0; fi < slidePrefetchFamilyIds_.size(); ++fi)
    {
        const uint16_t fam = slidePrefetchFamilyIds_[fi];
        if (fam == 0) continue;
        if (FindFamilySlot(currentWindowFamilies, fam)) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = fam;
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        currentWindowFamilies.push_back(slotEntry);
    }

    if (currentWindowFamilies.empty()) return;

    if (seg1FamilySlots_.empty())
    {
        seg1FamilySlots_ = std::move(currentWindowFamilies);
        InvalidateFamilySlotIndex();
        return;
    }

    // Keep cache bounded to current window families while preserving live slots
    // from the previous frame. Destructive slot/palette release is deferred to
    // RefreshFamilyWorkingSet(), which has the exact face-slot references.
    FamilySlotVector nextFamilies = currentWindowFamilies;
    for (auto& family : nextFamilies)
    {
        Seg1FamilySlotEntry* old = FindFamilySlot(seg1FamilySlots_, family.familyId);
        if (!old) continue;
        for (size_t li = 0; li < old->lodSlots.size(); ++li)
        {
            if (!IsVdp1TextureSlotLive(old->lodSlots[li]) && IsVdp1TextureSlotLive(family.lodSlots[li]))
            {
                old->lodSlots[li] = family.lodSlots[li];
            }
            if (IsVdp1TextureSlotLive(old->lodSlots[li]) && !IsVdp1TextureSlotLive(family.lodSlots[li]))
            {
                family.lodSlots[li] = old->lodSlots[li];
            }
        }
    }
    seg1FamilySlots_.swap(nextFamilies);
    InvalidateFamilySlotIndex();
}

void TrackSystem::RefreshFamilyWorkingSet(bool releaseUnused)
{
    (void)releaseUnused;
    if (seg1FamilySlots_.empty()) return;

    usedTextureSlotsThisFrame_.fill(0u);

    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        seg1FamilySlots_[i].workingRefs = { 0, 0, 0, 0 };
    }

    auto markSlotUsed = [&](int32_t slotHint)
    {
        if (slotHint < 0) return;
        if (slotHint >= static_cast<int32_t>(SRL_MAX_TEXTURES)) return;
        const uint16_t slot = static_cast<uint16_t>(slotHint);
        if (!IsVdp1TextureSlotLive(slot)) return;
        usedTextureSlotsThisFrame_[slot] = 1u;
    };

    auto addRef = [&](uint16_t familyId, int32_t slotHint, uint8_t fallbackLodIndex)
    {
        if (familyId == 0) return;
        Seg1FamilySlotEntry* family = FindFamilySlot(seg1FamilySlots_, familyId);
        if (!family) return;

        uint8_t resolvedLod = 0xFF;
        if (slotHint >= 0 && slotHint < static_cast<int32_t>(SRL_MAX_TEXTURES))
        {
            const uint16_t slot = static_cast<uint16_t>(slotHint);
            for (uint8_t li = 0; li < 4; ++li)
            {
                if (family->lodSlots[li] != slot) continue;
                if (!IsVdp1TextureSlotLive(slot)) continue;
                resolvedLod = li;
                break;
            }
        }
        if (resolvedLod > 3) resolvedLod = (fallbackLodIndex <= 3) ? fallbackLodIndex : 0u;

        if (family->workingRefs[resolvedLod] < std::numeric_limits<uint16_t>::max())
        {
            ++family->workingRefs[resolvedLod];
        }
    };

    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        const auto& entry = segmentRenderers_[i];
        if (!entry.renderer) continue;
        if (!entry.lodState.ready) continue;
        if (entry.lodState.faceFamilyIds.empty()) continue;

        const uint8_t fallbackLodIndex = (entry.lodState.currentLodIndex <= 3)
            ? entry.lodState.currentLodIndex
            : 0u;

        std::vector<uint16_t> seenFamilies{};
        std::vector<int16_t> seenSlots{};
        seenFamilies.reserve(16);
        seenSlots.reserve(16);
        for (size_t fi = 0; fi < entry.lodState.faceFamilyIds.size(); ++fi)
        {
            const uint16_t fam = entry.lodState.faceFamilyIds[fi];
            if (fam == 0) continue;
            const int32_t slotHint =
                (fi < entry.lodState.currentFaceSlots.size()) ? entry.lodState.currentFaceSlots[fi] : -1;
            markSlotUsed(slotHint);

            bool seen = false;
            for (size_t si = 0; si < seenFamilies.size(); ++si)
            {
                if (seenFamilies[si] != fam) continue;
                if (seenSlots[si] < 0 && slotHint >= 0) seenSlots[si] = slotHint;
                seen = true;
                break;
            }
            if (seen) continue;
            seenFamilies.push_back(fam);
            seenSlots.push_back(slotHint);
        }
        for (size_t si = 0; si < seenFamilies.size(); ++si)
        {
            addRef(seenFamilies[si], seenSlots[si], fallbackLodIndex);
        }
    }

    if (!slidePrefetchFamilyIds_.empty())
    {
        std::vector<uint16_t> seenFamilies{};
        std::vector<int16_t> seenSlots{};
        seenFamilies.reserve(16);
        seenSlots.reserve(16);
        for (size_t fi = 0; fi < slidePrefetchFamilyIds_.size(); ++fi)
        {
            const uint16_t fam = slidePrefetchFamilyIds_[fi];
            if (fam == 0) continue;
            const int32_t slotHint =
                (fi < slidePrefetchFaceSlots_.size()) ? slidePrefetchFaceSlots_[fi] : -1;
            markSlotUsed(slotHint);

            bool seen = false;
            for (size_t si = 0; si < seenFamilies.size(); ++si)
            {
                if (seenFamilies[si] != fam) continue;
                if (seenSlots[si] < 0 && slotHint >= 0) seenSlots[si] = slotHint;
                seen = true;
                break;
            }
            if (seen) continue;
            seenFamilies.push_back(fam);
            seenSlots.push_back(slotHint);
        }
        for (size_t si = 0; si < seenFamilies.size(); ++si)
        {
            addRef(seenFamilies[si], seenSlots[si], 0u);
        }
    }

}

void TrackSystem::ReleaseUnusedFamilyResourcesEndFrame()
{
    if (kEnableTrackRuntimeStabilization) return;
    if (seg1FamilySlots_.empty()) return;

    // Keep a short grace window before recycling a slot. This avoids
    // releasing resources that are still crossing frame boundaries in the
    // renderer/VDP1 pipeline.
    constexpr uint8_t kUnusedGraceFrames = 2u;

    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        auto& family = seg1FamilySlots_[i];
        for (uint8_t li = 0; li < 4; ++li)
        {
            uint16_t& slot = family.lodSlots[li];
            uint8_t& unusedFrames = family.unusedFrames[li];
            if (slot == No_Texture)
            {
                unusedFrames = 0u;
                continue;
            }
            if (!IsVdp1TextureSlotLive(slot))
            {
                slot = No_Texture;
                unusedFrames = 0u;
                continue;
            }

            const bool referencedByFace =
                (slot < usedTextureSlotsThisFrame_.size()) && (usedTextureSlotsThisFrame_[slot] != 0u);
            const bool referencedByFamily = family.workingRefs[li] != 0u;
            if (referencedByFace || referencedByFamily)
            {
                unusedFrames = 0u;
                continue;
            }

            if (unusedFrames < std::numeric_limits<uint8_t>::max())
            {
                ++unusedFrames;
            }
            if (unusedFrames < kUnusedGraceFrames) continue;

            ReleaseTrackedPaletteBankForSlot(slot);
            QueueReusableTrackTextureSlot(slot);
            slot = No_Texture;
            unusedFrames = 0u;
        }
    }
}

void TrackSystem::EmitFamilyWorkingSetTelemetry() const
{
    uint16_t activeFamilies = 0;
    uint16_t liveSlots = 0;
    std::array<uint16_t, 4> lodFamilies{{0, 0, 0, 0}};
    std::array<uint16_t, 4> lodRefs{{0, 0, 0, 0}};

    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        const auto& family = seg1FamilySlots_[i];
        bool familyActive = false;
        for (uint8_t li = 0; li < 4; ++li)
        {
            if (IsVdp1TextureSlotLive(family.lodSlots[li])) ++liveSlots;
            if (family.workingRefs[li] == 0u) continue;
            familyActive = true;
            ++lodFamilies[li];
            lodRefs[li] = static_cast<uint16_t>(lodRefs[li] + family.workingRefs[li]);
        }
        if (familyActive) ++activeFamilies;
    }

    SRL::Debug::Print(1, 18, "TRK ws fam:%u s:%u l8:%u/%u l16:%u/%u l32:%u/%u l64:%u/%u",
                      static_cast<unsigned>(activeFamilies),
                      static_cast<unsigned>(liveSlots),
                      static_cast<unsigned>(lodFamilies[0]),
                      static_cast<unsigned>(lodRefs[0]),
                      static_cast<unsigned>(lodFamilies[1]),
                      static_cast<unsigned>(lodRefs[1]),
                      static_cast<unsigned>(lodFamilies[2]),
                      static_cast<unsigned>(lodRefs[2]),
                      static_cast<unsigned>(lodFamilies[3]),
                      static_cast<unsigned>(lodRefs[3]));
    SRL::Debug::Print(1, 17, "TRK pal 16:%u 64:%u 128:%u 256:%u",
                      static_cast<unsigned>(CountTrackedBanks(g_trackPaletteBanks.pal16)),
                      static_cast<unsigned>(CountTrackedBanks(g_trackPaletteBanks.pal64)),
                      static_cast<unsigned>(CountTrackedBanks(g_trackPaletteBanks.pal128)),
                      static_cast<unsigned>(CountTrackedBanks(g_trackPaletteBanks.pal256)));
}

bool TrackSystem::UpdateActiveSegmentWindowForPosition(const Vector3D& worldPosition,
                                                       const Vector3D& trackOffset)
{
    if (!segmentsReady_ || totalSegmentCount_ == 0) return false;
    if (segmentCenterCatalog_.empty()) return false;
    if (windowDirection_ < 0) windowDirection_ = 1;
    if (activeWindowSwitchCooldown_ > 0)
    {
        --activeWindowSwitchCooldown_;
        return false;
    }

    const int32_t startId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
    if (startId <= 0) return false;
    const size_t windowCount = segmentRenderers_.size();
    if (windowCount == 0) return false;
    const int32_t nextId = WrapSegmentIdToRange(startId + 1, totalSegmentCount_);
    if (nextId <= 0) return false;

    auto scoreToId = [&](int32_t id) -> SRL::Math::Types::Fxp
    {
        if (id <= 0 || static_cast<size_t>(id) > segmentCenterCatalog_.size())
        {
            return SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        }
        const Vector3D c = segmentCenterCatalog_[static_cast<size_t>(id - 1)] + trackOffset;
        return (c.X - worldPosition.X).Abs() + (c.Z - worldPosition.Z).Abs();
    };

    auto wrapDistanceForward = [&](int32_t fromId, int32_t toId) -> int32_t
    {
        const int32_t total = static_cast<int32_t>(totalSegmentCount_);
        int32_t d = (toId - fromId) % total;
        if (d < 0) d += total;
        return d;
    };

    auto queueDeferredSlide = [&](int8_t slideDirection, int32_t desiredStartId) -> bool
    {
        desiredStartId = WrapSegmentIdToRange(desiredStartId, totalSegmentCount_);
        if (desiredStartId <= 0) return false;
        if (!kEnableTrackRuntimeStabilization)
        {
            const uint8_t stallBefore = runtimeSlideStallsThisFrame_;
            if (!SlideActiveSegmentWindow(1, slideDirection))
            {
                activeWindowSwitchCooldown_ = (runtimeSlideStallsThisFrame_ != stallBefore) ? 1 : 12;
                return false;
            }
            activeWindowSwitchCooldown_ = 0;
            return true;
        }

        const int32_t currentTargetId = WrapSegmentIdToRange(targetWindowStartId_, totalSegmentCount_);
        if (currentTargetId <= 0)
        {
            targetWindowStartId_ = desiredStartId;
        }
        else
        {
            const int32_t total = static_cast<int32_t>(totalSegmentCount_);
            const int32_t deltaFromTarget = wrapDistanceForward(currentTargetId, desiredStartId);
            if (deltaFromTarget > 0 && deltaFromTarget < (total / 2))
            {
                targetWindowStartId_ = desiredStartId;
            }
        }
        activeWindowSwitchCooldown_ = 0;
        return false;
    };

    auto resolveProgressiveLocalSegment = [&](int32_t seedId,
                                              int32_t& outId,
                                              SRL::Math::Types::Fxp& outScore) -> bool
    {
        if (seedId <= 0) return false;
        bool found = false;
        int32_t bestId = -1;
        SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);

        // Keep the search narrow and forward-biased so nearby hairpins do not
        // pull the streaming state to a different logical portion of the lap.
        constexpr int32_t kBackSearch = 1;
        constexpr int32_t kForwardSearch = 3;
        for (int32_t delta = -kBackSearch; delta <= kForwardSearch; ++delta)
        {
            const int32_t candidateId = WrapSegmentIdToRange(seedId + delta, totalSegmentCount_);
            const SRL::Math::Types::Fxp score = scoreToId(candidateId);
            if (!found || score < bestScore)
            {
                found = true;
                bestId = candidateId;
                bestScore = score;
            }
        }

        if (!found || bestId <= 0) return false;
        outId = bestId;
        outScore = bestScore;
        return true;
    };

    const int32_t seedId = trackedCarSegmentValid_
        ? WrapSegmentIdToRange(trackedCarSegmentId_, totalSegmentCount_)
        : startId;

    const int32_t observedId = WrapSegmentIdToRange(observedCarSegmentId_, totalSegmentCount_);
    if (observedCarSegmentId_ > 0 && observedId > 0)
    {
        const int32_t observedForwardDistance = wrapDistanceForward(startId, observedId);
        if (observedForwardDistance > 0 && observedForwardDistance < (static_cast<int32_t>(totalSegmentCount_) / 2))
        {
            trackedCarSegmentId_ = observedId;
            trackedCarSegmentValid_ = true;
            return queueDeferredSlide(+1, observedId);
        }
        if (observedForwardDistance == 0)
        {
            trackedCarSegmentId_ = observedId;
            trackedCarSegmentValid_ = true;
            return false;
        }
        SRL::Debug::Print(1, 13, "TRK obs back s:%d o:%d t:%d",
                          startId,
                          observedId,
                          trackedCarSegmentValid_ ? trackedCarSegmentId_ : -1);
        // If gameplay temporarily reports a segment behind the active window,
        // do not freeze the slide state. Fall through to the local progressive
        // heuristic so the window can continue following the car.
    }

    int32_t localSegmentId = -1;
    SRL::Math::Types::Fxp localScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
    if (resolveProgressiveLocalSegment(seedId, localSegmentId, localScore))
    {
        trackedCarSegmentId_ = localSegmentId;
        trackedCarSegmentValid_ = true;

        const int32_t forwardDistance = wrapDistanceForward(startId, localSegmentId);
        if (forwardDistance > 0)
        {
            return queueDeferredSlide(+1, localSegmentId);
        }
    }

    const SRL::Math::Types::Fxp currentScore = scoreToId(startId);
    const SRL::Math::Types::Fxp nextScore = scoreToId(nextId);
    const SRL::Math::Types::Fxp crossingHysteresis = SRL::Math::Types::Fxp::BuildRaw(8 << 16);
    if ((nextScore + crossingHysteresis) < currentScore)
    {
        trackedCarSegmentId_ = nextId;
        trackedCarSegmentValid_ = true;
        return queueDeferredSlide(+1, nextId);
    }

    return false;
}

void TrackSystem::ResetInitializationState()
{
    ready_ = false;
    segmentsReady_ = false;
    coordinatorReady_ = false;
    fixedVisibleSegmentCap_ = 1;
    totalSegmentCount_ = 0;
    activeWindowStartId_ = 1;
    activeWindowHead_ = 0;
    windowDirection_ = 1;
    activeWindowSwitchCooldown_ = 0;
    targetWindowStartId_ = 1;
    trackedCarSegmentId_ = 1;
    trackedCarSegmentValid_ = false;
    observedCarSegmentId_ = -1;
    segmentCenterCatalog_.clear();
    seg1ComponentEnabled_ = false;
    seg1ComponentVerts_.clear();
    seg1ComponentFaces_.clear();
    seg1ComponentAttrs_.clear();
    seg1FaceFamilyIds_.clear();
    seg1FamilySlots_.clear();
    InvalidateFamilySlotIndex();
    familyMergeCooldown_ = 0;
    for (auto& v : seg1RendererFaceSlotsByLod_) v.clear();
    seg1RendererLodReady_ = false;
    seg1SingleFaceSwapReady_ = false;
    seg1SingleFaceSwapUseAlt_ = false;
    seg1SingleFaceSwapCounter_ = 0;
    seg1SingleFaceSwapFrames_ = 180;
    seg1SingleFaceSwapFace_ = -1;
    seg1SingleFaceSwapBaseSlot_ = -1;
    seg1SingleFaceSwapAltSlot_ = -1;
    seg1SingleFaceSlots_.clear();
    seg1CurrentLodIndex_ = 0;
    seg1LodFrameCounter_ = 0;
    seg1LodSwapFrames_ = 60;
    seg1ComponentCenter_ = Vector3D(0.0, 0.0, 0.0);
    ReleaseRawSegmentCatalog();
    ReleaseSeg1Texbanks();
    ReleaseSeg1TgaCatalog();
    segmentEntries_.clear();
    segmentRenderers_.clear();
    segmentHandles_.clear();
    slideScratchRenderer_.reset();
    slideIncomingFamilyIdsScratch_.clear();
    slideIncomingFaceRankOffsetsScratch_.clear();
    slideIncomingFaceSlotsScratch_.clear();
    ResetSlidePrefetchState();
    ReleaseTrackedPaletteBanks();
    ResetReusableTrackTextureSlots();
    trackTextureHeapBase_ = 0;
    trackTextureHeapBaseValid_ = false;
    trackTextureRecycleCount_ = 0;
    textureUploadsThisFrame_ = 0;
    prewarmCooldown_ = 0;
    workRamTrimCooldown_ = 0;
    workRamTelemetryCooldown_ = 0;
    workRamRepairCount_ = 0;
    fullTrackFamilyCacheReady_ = false;
    lastWindowFreeBytes_ = 0;
    lastWindowFreeValid_ = false;
    usedTextureSlotsThisFrame_.fill(0u);
    soakMonitor_.Reset();
}

size_t TrackSystem::ResolveInitialLoadLimit(const Config& config) const
{
    // Test mode: keep the loaded catalog aligned with the configured visible segment budget
    // so we can isolate experiments on SEG_001 only. The full-catalog path stays available
    // behind this switch for future broader texture-mapping validation.
    constexpr bool kBuildFullCatalogForTextureMapping = false;
    if (kBuildFullCatalogForTextureMapping) return kTrackSegmentLimit;
    if (config.initialSegments == 0) return kTrackSegmentLimit;
    return std::min<size_t>(config.initialSegments, kTrackSegmentLimit);
}

void TrackSystem::PrepareInitialSegmentPackages(size_t loadLimit)
{
    const uint32_t desiredCap = std::max<uint32_t>(1u, static_cast<uint32_t>(loadLimit));
    fixedVisibleSegmentCap_ =
        std::min<uint32_t>(desiredCap,
                           std::max<uint32_t>(1u, static_cast<uint32_t>(totalSegmentCount_)));
    (void)RebuildActiveSegmentWindow(1, fixedVisibleSegmentCap_, +1);
    trackedCarSegmentId_ = 1;
    trackedCarSegmentValid_ = true;
    targetWindowStartId_ = 1;
    TryPrefetchUpcomingSegment();
    SRL::Debug::Print(1, 28, "DBG build tag:TS29B segs:%lu", (unsigned long)segmentEntries_.size());
}

void TrackSystem::ConfigureCoordinatorAndBudget(const Config& config)
{
    // Safety guard: cap draws by configured visible segments.
    const uint32_t kSegmentCap = static_cast<uint32_t>(kTrackSegmentLimit);
    const uint32_t kSafeTrackDrawsPerFrame =
        std::min<uint32_t>(
            std::max<uint32_t>(1u, config.initialSegments),
            kSegmentCap);
    TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::Config coordinatorConfig{};
    coordinatorConfig.budget.maxTrackSegments =
        std::min<uint32_t>(
            std::min<uint32_t>(config.initialSegments, kSegmentCap),
            kSafeTrackDrawsPerFrame);
    coordinatorConfig.budget.maxTrackMeshes = config.initialMeshes;
    coordinatorConfig.budget.maxTrackFaces = config.initialFaces;
    coordinatorConfig.chunkCapacity =
        std::max<size_t>(1u, static_cast<size_t>(coordinatorConfig.budget.maxTrackSegments));

    coordinatorReady_ = coordinator_.Initialize(coordinatorConfig);
    if (!coordinatorReady_)
    {
        SRL::Debug::Print(1, 31, "TrackRenderCoordinator HWR alloc failed");
    }
    const bool effectiveUseSlave = kEnableTrackRuntimeStabilization ? false : config.useSlave;
    producer_.SetUseSlave(effectiveUseSlave);
    producer_.SetMaxFramesInFlight(2);
    producer_.SetRecoveryFrames(120);
    producer_.SetSafeModeStallThreshold(4);
    producer_.SetSafeModeCooldownFrames(90);
    SRL::Debug::Print(1, 31, "TRK slave:%u", effectiveUseSlave ? 1u : 0u);

    AdaptiveTrackBudgetController::Limits adaptiveBudgetLimits{};
    const uint32_t minSegmentsRequested =
        std::min<uint32_t>(std::max<uint32_t>(1, config.minSegments), kSafeTrackDrawsPerFrame);
    const uint32_t maxSegmentsRequested =
        std::min<uint32_t>(std::max<uint32_t>(minSegmentsRequested, config.initialSegments), kSafeTrackDrawsPerFrame);
    const uint32_t maxSegmentsCap = kSegmentCap;
    adaptiveBudgetLimits.minSegments = std::min<uint32_t>(minSegmentsRequested, maxSegmentsCap);
    adaptiveBudgetLimits.maxSegments = std::min<uint32_t>(maxSegmentsRequested, maxSegmentsCap);
    // Lock mesh/face budget to configured startup values to avoid runtime shrink.
    adaptiveBudgetLimits.minMeshes = std::max<uint32_t>(1u, config.initialMeshes);
    adaptiveBudgetLimits.maxMeshes = adaptiveBudgetLimits.minMeshes;
    adaptiveBudgetLimits.minFaces = std::max<uint32_t>(1000u, config.initialFaces);
    adaptiveBudgetLimits.maxFaces = adaptiveBudgetLimits.minFaces;
    budgetController_ = AdaptiveTrackBudgetController(adaptiveBudgetLimits);
}

void TrackSystem::LogInitialSegmentDiagnostics() const
{
    if (!segmentsReady_)
    {
        SRL::Debug::Print(1, 28, "Track rendering skipped: segments missing");
        if (lastSegmentPath_[0] != '\0')
        {
            SRL::Debug::Print(1, 29, "Last segment path tested: %s", lastSegmentPath_);
        }
    }

    if (segmentRenderers_.empty()) return;

    char ids[64]{};
    size_t idsLen = 0;
    const size_t count = std::min(segmentRenderers_.size(), size_t(3));
    for (size_t i = 0; i < count; ++i)
    {
        char tmp[16]{};
        std::snprintf(tmp, sizeof(tmp), "%d", segmentRenderers_[i].id);
        const size_t n = strlen(tmp);
        if (idsLen + n + 2 < sizeof(ids))
        {
            memcpy(ids + idsLen, tmp, n);
            idsLen += n;
            if (i + 1 < count)
            {
                ids[idsLen++] = ',';
                ids[idsLen] = '\0';
            }
        }
    }
    SRL::Debug::Print(1, 29, "DBG build tag:TS27A near(%lu):%s", (unsigned long)count, ids);
}

void TrackSystem::ApplyInitialSdrFamilySlots()
{
    // Build the initial per batch face slots from SDR family ids.
    constexpr bool kEnableMat8FixedIntegration = true;
    if (!kEnableMat8FixedIntegration) return;

    if (kEnableTrackRuntimeStabilization)
    {
        if (!RebuildTrackTextureResidencyForWindow())
        {
            SRL::Debug::Print(1, 19, "SDR lod build fail");
            return;
        }
        SRL::Debug::Print(1, 20, "SDR ok:%u fail:%u fam:%u full:%u",
                          static_cast<unsigned>(segmentRenderers_.size()),
                          0u,
                          static_cast<unsigned>(seg1FamilySlots_.size()),
                          fullTrackFamilyCacheReady_ ? 1u : 0u);
        return;
    }

    FamilySlotVector familyLodSlots{};
    if (fullTrackFamilyCacheReady_ && !seg1FamilySlots_.empty())
    {
        familyLodSlots = seg1FamilySlots_;
    }
    else if (!BuildTrackFamilyLodSlots(familyLodSlots))
    {
        SRL::Debug::Print(1, 19, "SDR lod build fail");
        return;
    }

    unsigned matOk = 0;
    unsigned matFail = 0;

    // Preserve already uploaded slot ids when the active segment window slides.
    if (!seg1FamilySlots_.empty())
    {
        for (auto& target : familyLodSlots)
        {
            for (const auto& current : seg1FamilySlots_)
            {
                if (current.familyId != target.familyId) continue;
                for (size_t li = 0; li < target.lodSlots.size(); ++li)
                {
                    if (target.lodSlots[li] == No_Texture && current.lodSlots[li] != No_Texture)
                    {
                        target.lodSlots[li] = current.lodSlots[li];
                    }
                }
                break;
            }
        }
    }

    size_t logicalRank = 0;
    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        auto& seg = segmentRenderers_[i];
        if (!seg.renderer || !seg.lodState.ready || seg.lodState.faceFamilyIds.empty())
        {
            ++matFail;
            continue;
        }

        seg.lodState.currentFaceSlots.assign(seg.lodState.faceFamilyIds.size(), -1);
        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const bool lodOk = !seg.lodState.hasPerFaceRankOffsets
            ? RebuildSegmentFaceSlotsForLod(seg, desiredLodIndex, familyLodSlots)
            : RebuildSegmentFaceSlotsForBaseRank(seg, logicalRank, familyLodSlots);
        if (!lodOk)
        {
            ++matFail;
            logicalRank += std::max<size_t>(1, static_cast<size_t>(seg.logicalSegmentCount));
            continue;
        }
        (void)seg.renderer->ApplyFaceTextureSlotsGlobal(seg.lodState.currentFaceSlots);
        const bool missing =
            HasMissingRequiredFaceTextureSlots(seg.lodState.currentFaceSlots,
                                               &seg.lodState.faceFamilyIds);
        seg.lodState.currentBaseRank =
            (seg.lodState.hasPerFaceRankOffsets && !missing)
                ? static_cast<int16_t>(logicalRank)
                : -1;
        seg.lodState.currentLodIndex = missing ? 0xFF : desiredLodIndex;
        logicalRank += std::max<size_t>(1, static_cast<size_t>(seg.logicalSegmentCount));
        ++matOk;
    }

    // Persist resolved VDP1 slots back to the shared family cache. When the full
    // track family catalog is active, discarding this state causes the runtime to
    // re-upload textures continuously after the first slide.
    seg1FamilySlots_ = familyLodSlots;
    InvalidateFamilySlotIndex();
    RefreshFamilyWorkingSet(false);
    SRL::Debug::Print(1, 20, "SDR ok:%u fail:%u fam:%u full:%u",
                      matOk,
                      matFail,
                      static_cast<unsigned>(familyLodSlots.size()),
                      fullTrackFamilyCacheReady_ ? 1u : 0u);
}

bool TrackSystem::Initialize(const Config& config)
{
    auto printInitRam = [](int row, const char* tag)
    {
        const auto hwr = SRL::Memory::HighWorkRam::GetReport();
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        const auto crt = SRL::Memory::CartRam::GetReport();
        SRL::Debug::Print(1, row, "%s hf:%u lf:%u cf:%u",
                          tag,
                          static_cast<unsigned>(hwr.FreeSize),
                          static_cast<unsigned>(lwr.FreeSize),
                          static_cast<unsigned>(crt.FreeSize));
    };

    ResetInitializationState();
    if (!BuildSegmentCenterCatalog())
    {
        SRL::Debug::Print(1, 28, "Track catalog missing");
        return false;
    }
    printInitRam(8, "TRKI cat");
    const size_t loadLimit = ResolveInitialLoadLimit(config);
    PrepareInitialSegmentPackages(loadLimit);
    printInitRam(9, "TRKI pkg");
    CaptureTrackTextureHeapBase();
    SegmentRuntimeDraw::HeaderV1 warmRdrHeader{};
    (void)LoadRdrHeaderForSegment(1, warmRdrHeader);
    if (totalSegmentCount_ > 1)
    {
        (void)LoadRdrHeaderForSegment(static_cast<int>(totalSegmentCount_), warmRdrHeader);
    }
    TrimRuntimeBlobScratchCaches(true);
    PrimeRuntimeScratchCapacities();
    (void)PreloadFullTrackFamilyLodCache();
    printInitRam(10, "TRKI fam");

    ConfigureCoordinatorAndBudget(config);
    LogInitialSegmentDiagnostics();
    ApplyInitialSdrFamilySlots();
    printInitRam(11, "TRKI app");

    // Keep the normal multi segment render path active even when only one segment
    // is visible, so single segment tests match the production flow.
    const bool enableSeg1Diagnostics = false;

    // Preload all referenced TGA files into Cart 4MB (diagnostic + fast path source cache).
    if (enableSeg1Diagnostics)
    {
        (void)PreloadTgaCatalogFromSegmentsMap();
        int32_t sCd = -1;
        int32_t sCart = -1;
        if (!seg1TgaCatalog_.empty() && seg1TgaCatalog_[0].cartPtr && seg1TgaCatalog_[0].size > 0)
        {
            DecodedTgaTexture d{};
            if (DecodePalettedTgaMemory(static_cast<const uint8_t*>(seg1TgaCatalog_[0].cartPtr), seg1TgaCatalog_[0].size, d))
            {
                sCart = UploadDecodedTextureToVdp1(d);
            }
        }
        SRL::Debug::Print(1, 5, "TXT cd:%d ct:%d c:%u j:%u",
                          (int)sCd, (int)sCart, (unsigned)seg1TgaPreloadCount_, (unsigned)seg1TgaJsonOk_);
    }

    // Minimal forced texture test for SEG_001 (diagnostic):
    // Apply one known texture slot to all faces to validate renderer texture path.
    if (enableSeg1Diagnostics)
    {
        constexpr bool kEnableSeg1ForcedTextureTest = false;
        if (kEnableSeg1ForcedTextureTest)
        {
            int32_t slot = -1;
            bool asfaltoFromCart = false;
            for (const auto& t : seg1TgaCatalog_)
            {
                if (!NameEqualsIgnoreCase(t.name, "asfalto_8.tga")) continue;
                if (!t.cartPtr || t.size == 0) continue;
                DecodedTgaTexture decoded{};
                if (DecodePalettedTgaMemory(static_cast<const uint8_t*>(t.cartPtr), t.size, decoded))
                {
                    slot = UploadDecodedTextureToVdp1(decoded);
                    if (slot > 0) asfaltoFromCart = true;
                }
                break;
            }
            SRL::Debug::Print(1, 19, "S1 FORCE cart hit:%u cat:%u", asfaltoFromCart ? 1u : 0u, (unsigned)seg1TgaCatalog_.size());
            if (slot < 0) slot = TryLoadTextureFromCd("asfalto_8.tga");
            if (slot < 0) slot = TryLoadTextureFromCd("ASFALTO_8.TGA");
            if (slot > 0)
            {
                for (auto& seg : segmentRenderers_)
                {
                    if (seg.id != 1 || !seg.renderer) continue;
                    const size_t faces = static_cast<size_t>(seg.renderer->FaceCount());
                    if (faces == 0) break;
                    const size_t applied = seg.renderer->ForceTextureAll(static_cast<uint16_t>(slot));
                    std::vector<int16_t> probe{};
                    seg.renderer->CollectFaceTextureSlotsGlobal(probe);
                    const int32_t first = probe.empty() ? -1 : probe[0];
                    SRL::Debug::Print(1, 19, "S1 FORCE tex:%d ap:%u f:%u p0:%d", slot, (unsigned)applied, (unsigned)faces, (int)first);
                    break;
                }
            }
            else
            {
                SRL::Debug::Print(1, 19, "S1 FORCE tex load fail cat:%u", (unsigned)seg1TgaCatalog_.size());
            }
        }
    }

    // Single-face overwrite probe (disabled - unstable in current runtime path).
    if (enableSeg1Diagnostics)
    {
        constexpr bool kEnableSeg1SingleFaceSwapProbe = false;
        if (kEnableSeg1SingleFaceSwapProbe)
        {
            TrackRenderer* seg1Renderer = nullptr;
            for (auto& seg : segmentRenderers_)
            {
                if (seg.id == 1 && seg.renderer) { seg1Renderer = seg.renderer.get(); break; }
            }
            if (seg1Renderer)
            {
                seg1Renderer->CollectFaceTextureSlotsGlobal(seg1SingleFaceSlots_);
                if (!seg1SingleFaceSlots_.empty())
                {
                    int16_t chosenFace = -1;
                    int16_t chosenBase = -1;
                    for (size_t i = 0; i < seg1SingleFaceSlots_.size(); ++i)
                    {
                        if (seg1SingleFaceSlots_[i] > 0)
                        {
                            chosenFace = static_cast<int16_t>(i);
                            chosenBase = seg1SingleFaceSlots_[i];
                            break;
                        }
                    }

                    if (chosenFace >= 0 && chosenBase > 0)
                    {
                        auto overwriteWithCandidates = [&](int32_t slot, const char* a, const char* b) -> bool
                        {
                            if (TryOverwriteTextureSlotFromCd(slot, a)) return true;
                            if (TryOverwriteTextureSlotFromCd(slot, b)) return true;
                            return false;
                        };

                        // Prepare/validate compatibility by writing base then alt then base again.
                        const bool baseOk0 = overwriteWithCandidates(chosenBase, "asfalto_8.tga", "ASFALTO_8.TGA");
                        const bool altOk = overwriteWithCandidates(chosenBase, "area_escape_8.tga", "AREA_ESCAPE_8.TGA");
                        const bool baseOk1 = overwriteWithCandidates(chosenBase, "asfalto_8.tga", "ASFALTO_8.TGA");
                        if (baseOk0 && altOk && baseOk1)
                        {
                            seg1SingleFaceSwapFace_ = chosenFace;
                            seg1SingleFaceSwapBaseSlot_ = chosenBase;
                            seg1SingleFaceSwapAltSlot_ = chosenBase; // same slot overwrite mode
                            seg1SingleFaceSwapReady_ = true;
                            SRL::Debug::Print(1, 20, "S1 OVR ready f:%u s:%u",
                                              (unsigned)seg1SingleFaceSwapFace_,
                                              (unsigned)seg1SingleFaceSwapBaseSlot_);
                        }
                        else
                        {
                            SRL::Debug::Print(1, 20, "S1 OVR skip");
                        }
                    }
                    else
                    {
                        SRL::Debug::Print(1, 20, "S1 OVR no-face");
                    }
                }
            }
        }
    }

    // Componentized pipeline probe (phase 1):
    // Validate and optionally render SEG_001 from GEO/MAT component files.
    if (enableSeg1Diagnostics)
    {
        constexpr bool kUseSeg1ComponentRenderer = false; // usar renderer normal da pista para LOD swap
        SegmentComponent::Blob geoBlob{};
        SegmentComponent::Blob matBlob{};
        SegmentComponent::Loader::GeoView geoView{};
        SegmentComponent::Loader::MatView matView{};
        const bool geoParsed = LoadGeoForSegment(1, geoBlob, geoView);
        const bool matParsed = LoadMat8ForSegment(1, matBlob, matView);
        const bool geoOk = geoParsed;
        const bool matOk = matParsed;
        constexpr bool kShowSeg1ComponentProbeLogs = false;
        if (kShowSeg1ComponentProbeLogs)
        {
            SRL::Debug::Print(1, 24, "CMP SEG001 GEO:%d(%u) MAT:%d(%u)",
                              geoOk ? 1 : 0, (unsigned)geoBlob.size,
                              matOk ? 1 : 0, (unsigned)matBlob.size);
        }
        const bool pairOk = SegmentComponent::Loader::ValidateGeoMatPair(geoView, matView);
        if (kShowSeg1ComponentProbeLogs)
        {
            SRL::Debug::Print(1, 25, "S1 P g:%d m:%d p:%d",
                              geoParsed ? 1 : 0, matParsed ? 1 : 0, pairOk ? 1 : 0);
        }
        if (geoParsed)
        {
            if (kShowSeg1ComponentProbeLogs)
            {
                SRL::Debug::Print(1, 26, "CMP GEO sid:%u v:%u f:%u",
                                  (unsigned)geoView.file.segmentId,
                                  (unsigned)geoView.header.vertexCount,
                                  (unsigned)geoView.header.faceCount);
            }
        }
        if (matParsed)
        {
            if (kShowSeg1ComponentProbeLogs)
            {
                SRL::Debug::Print(1, 27, "CMP MAT sid:%u f:%u",
                                  (unsigned)matView.file.segmentId,
                                  (unsigned)matView.header.faceCount);
            }
        }
        if (pairOk)
        {
            TrackRenderer* seg1Renderer = nullptr;
            SegmentRenderEntry* seg1Entry = nullptr;
            for (auto& seg : segmentRenderers_)
            {
                if (seg.id == 1 && seg.renderer) { seg1Renderer = seg.renderer.get(); seg1Entry = &seg; break; }
            }
            if (seg1Renderer)
            {
                const uint32_t rv = seg1Renderer->VertexCount();
                const uint32_t rf = seg1Renderer->FaceCount();
                const int vOk = (rv == geoView.header.vertexCount) ? 1 : 0;
                const int fOk = (rf == geoView.header.faceCount) ? 1 : 0;
                if (kShowSeg1ComponentProbeLogs)
                {
                    SRL::Debug::Print(1, 28, "CMP SEG001 vs NYA v:%u/%u(%d) f:%u/%u(%d)",
                                      (unsigned)geoView.header.vertexCount, (unsigned)rv, vOk,
                                      (unsigned)geoView.header.faceCount, (unsigned)rf, fOk);
                }
            }

            if (seg1Renderer && seg1Entry)
            {
                seg1ComponentVerts_.clear();
                seg1ComponentFaces_.clear();
                seg1ComponentAttrs_.clear();
                seg1ComponentVerts_.reserve(static_cast<size_t>(geoView.header.vertexCount));
                seg1ComponentFaces_.reserve(static_cast<size_t>(geoView.header.faceCount));
                seg1ComponentAttrs_.reserve(static_cast<size_t>(geoView.header.faceCount));

                SRL::Math::Types::Vector3D minv(32767, 32767, 32767);
                SRL::Math::Types::Vector3D maxv(-32768, -32768, -32768);

                for (uint32_t vi = 0; vi < geoView.header.vertexCount; ++vi)
                {
                    SegmentComponent::GeoVertex gv{};
                    const size_t off = geoView.vertexOffset + static_cast<size_t>(vi) * sizeof(SegmentComponent::GeoVertex);
                    if (!SegmentComponent::Loader::ReadGeoVertexLeAt(geoBlob.bytes, off, gv))
                    {
                        seg1ComponentVerts_.clear();
                        break;
                    }
                    Vector3D v(
                        SRL::Math::Types::Fxp::BuildRaw(gv.x),
                        SRL::Math::Types::Fxp::BuildRaw(gv.y),
                        SRL::Math::Types::Fxp::BuildRaw(gv.z));
                    minv.X = SRL::Math::Min(minv.X, v.X);
                    minv.Y = SRL::Math::Min(minv.Y, v.Y);
                    minv.Z = SRL::Math::Min(minv.Z, v.Z);
                    maxv.X = SRL::Math::Max(maxv.X, v.X);
                    maxv.Y = SRL::Math::Max(maxv.Y, v.Y);
                    maxv.Z = SRL::Math::Max(maxv.Z, v.Z);
                    seg1ComponentVerts_.push_back(v);
                }

                // Load texture catalogs from TEXBANK_{8,16,32,64}.BIN into cart RAM and upload to VDP1.
                const int lodValues[4] = { 8, 16, 32, 64 };
                int familyIdsUsed[512]{};
                size_t familyIdsUsedCount = 0;

                for (uint32_t fi = 0; fi < matView.header.faceCount; ++fi)
                {
                    SegmentComponent::MatFaceBinding mb{};
                    const size_t moff = matView.bindingOffset + static_cast<size_t>(fi) * sizeof(SegmentComponent::MatFaceBinding);
                    if (!SegmentComponent::Loader::ReadMatFaceBindingLeAt(matBlob.bytes, moff, mb)) continue;
                    const int fam = static_cast<int>(mb.materialId);
                    if (fam <= 0) continue;
                    bool exists = false;
                    for (size_t u = 0; u < familyIdsUsedCount; ++u)
                    {
                        if (familyIdsUsed[u] == fam) { exists = true; break; }
                    }
                    if (!exists && familyIdsUsedCount < 512)
                    {
                        familyIdsUsed[familyIdsUsedCount++] = fam;
                    }
                }

                InitializeFamilySlots(seg1FamilySlots_, familyIdsUsed, familyIdsUsedCount);
                size_t texLoaded = 0;
                size_t texFail = 0;
                size_t texMissFamily = 0;
                size_t texDecodeFail = 0;
                size_t texUploadFail = 0;
                size_t texFallbackRecovered = 0;
                uint16_t texLastUnresolvedFam = 0;
                int texLastUnresolvedLod = 0;
                int texLastRecoveredDstLod = 0;
                int texLastRecoveredSrcLod = 0;

                auto loadTexbankToCart = [&](size_t li) -> bool
                {
                    return LoadSeg1TexbankIndexToCart(li, lodValues[li]);
                };

                for (size_t li = 0; li < 4; ++li)
                {
                    if (!loadTexbankToCart(li))
                    {
                        texFail += familyIdsUsedCount;
                        continue;
                    }

                    for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                    {
                        const uint16_t fam = seg1FamilySlots_[u].familyId;
                        bool sawMissingFamily = false;
                        bool sawDecodeFail = false;
                        bool sawUploadFail = false;
                        int loadedFromLodValue = 0;
                        const bool loadedForRequestedLod = TryLoadFamilyLodSlot(seg1FamilySlots_[u],
                                                                                 static_cast<uint8_t>(li),
                                                                                 /*fallbackToLowerLods*/true,
                                                                                 /*fallbackToHigherLods*/false,
                                                                                 &sawMissingFamily,
                                                                                 &sawDecodeFail,
                                                                                 &sawUploadFail,
                                                                                 &loadedFromLodValue);

                        if (loadedForRequestedLod)
                        {
                            ++texLoaded;
                            if (loadedFromLodValue != lodValues[li])
                            {
                                ++texFallbackRecovered;
                                texLastRecoveredDstLod = lodValues[li];
                                texLastRecoveredSrcLod = loadedFromLodValue;
                            }
                        }
                        else
                        {
                            ++texFail;
                            texLastUnresolvedFam = fam;
                            texLastUnresolvedLod = lodValues[li];
                            if (sawDecodeFail) ++texDecodeFail;
                            else if (sawUploadFail) ++texUploadFail;
                            else if (sawMissingFamily) ++texMissFamily;
                            else ++texMissFamily;
                        }
                    }
                }
                SRL::Debug::Print(1, 17, "S1 FB ok:%u u:%u l:%d %d>%d",
                                  (unsigned)texFallbackRecovered,
                                  (unsigned)texLastUnresolvedFam,
                                  texLastUnresolvedLod,
                                  texLastRecoveredDstLod,
                                  texLastRecoveredSrcLod);

                // Fallback path from preloaded cart catalog only (no direct CD reads).
                if (texLoaded == 0 && !seg1FamilySlots_.empty())
                {
                    const char* jsonCandidates[] = {
                        "CD/DATA/segments_map.json",
                        "CD/DATA/segments_map.json;1",
                        "DATA/segments_map.json",
                        "DATA/segments_map.json;1",
                        "segments_map.json",
                        "segments_map.json;1"
                    };
                    std::vector<char> jsonText{};
                    Segment1TextureJson map1{};
                    const bool jsonOk = ReadCdFileText(jsonCandidates, sizeof(jsonCandidates) / sizeof(jsonCandidates[0]), jsonText) &&
                                        ParseSegment1TextureJson(jsonText.data(), map1);
                    const char* renCandidates[] = {
                        "CD/DATA/ren_textures_copy_map.json",
                        "CD/DATA/ren_textures_copy_map.json;1",
                        "DATA/ren_textures_copy_map.json",
                        "DATA/ren_textures_copy_map.json;1",
                        "CD/DATA/REN_TEXTURES_COPY_MAP.JSON",
                        "CD/DATA/REN_TEXTURES_COPY_MAP.JSON;1",
                        "DATA/REN_TEXTURES_COPY_MAP.JSON",
                        "DATA/REN_TEXTURES_COPY_MAP.JSON;1",
                        "ren_textures_copy_map.json",
                        "ren_textures_copy_map.json;1"
                    };
                    std::vector<char> renText{};
                    RenTextureMap renMap{};
                    const bool renOk = ReadCdFileText(renCandidates, sizeof(renCandidates) / sizeof(renCandidates[0]), renText) &&
                                       ParseRenTextureCopyMap(renText.data(), renMap);
                    if (jsonOk)
                    {
                        char fileNorm[64]{};
                        bool firstMapMissLogged = false;
                        for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                        {
                            const int fam = static_cast<int>(seg1FamilySlots_[u].familyId);
                            const int fi = FindFamilyIndex(map1, fam);
                            if (fi < 0) continue;

                            NormalizeTextureFileName(map1.familyTex64[fi], fileNorm, sizeof(fileNorm));
                            const int lodVals[4] = { 8, 16, 32, 64 };
                            bool anyLoaded = false;
                            auto tryLoadFromCartCatalog = [&](const char* name) -> int32_t
                            {
                                if (!name || name[0] == '\0') return -1;
                                char norm[64]{};
                                NormalizeTextureFileName(name, norm, sizeof(norm));
                                for (const auto& t : seg1TgaCatalog_)
                                {
                                    if (!t.cartPtr || t.size == 0) continue;
                                    if (!NameEqualsIgnoreCase(t.name, norm)) continue;
                                    DecodedTgaTexture decoded{};
                                    if (!DecodePalettedTgaMemory(static_cast<const uint8_t*>(t.cartPtr), t.size, decoded)) continue;
                                    return UploadDecodedTextureToVdp1(decoded);
                                }
                                return -1;
                            };
                            for (size_t li = 0; li < 4; ++li)
                            {
                                char candA[64]{};
                                char candB[64]{};
                                char mapped[64]{};
                                BuildLodTextureName(fileNorm, lodVals[li], false, candA, sizeof(candA));
                                BuildLodTextureName(fileNorm, lodVals[li], true, candB, sizeof(candB));
                                int32_t slot = -1;
                                // Deterministic short-name path by familyId: F###_LOD.TGA / F###LOD.TGA
                                char famA[32]{};
                                char famB[32]{};
                                std::snprintf(famA, sizeof(famA), "F%03d_%d.TGA", fam, lodVals[li]);
                                std::snprintf(famB, sizeof(famB), "F%03d%d.TGA", fam, lodVals[li]);
                                slot = tryLoadFromCartCatalog(famA);
                                if (slot < 0) slot = tryLoadFromCartCatalog(famB);

                                if (renOk && FindRenamedTarget(renMap, candA, lodVals[li], mapped, sizeof(mapped)))
                                {
                                    if (slot < 0) slot = tryLoadFromCartCatalog(mapped);
                                }
                                if (slot < 0 && renOk && FindRenamedTarget(renMap, candB, lodVals[li], mapped, sizeof(mapped)))
                                {
                                    slot = tryLoadFromCartCatalog(mapped);
                                }
                                if (slot >= 0)
                                {
                                    seg1FamilySlots_[u].lodSlots[li] = static_cast<uint16_t>(slot);
                                    anyLoaded = true;
                                }
                                else if (!firstMapMissLogged)
                                {
                                    firstMapMissLogged = true;
                                    SRL::Debug::Print(1, 16, "S1MM f:%d l:%d", fam, lodVals[li]);
                                }
                            }
                            if (anyLoaded) ++texLoaded; else ++texFail;
                        }
                        SRL::Debug::Print(1, 28, "S1 RMAP ok:%u c:%u", renOk ? 1u : 0u, (unsigned)renMap.entries.size());
                        if (!renOk)
                        {
                            SRL::Debug::Print(1, 18, "S1 RMAP miss");
                        }
                    }
                }
                SRL::Debug::Print(1, 30, "S1 TEX ok:%u fl:%u fm:%u",
                                  (unsigned)texLoaded, (unsigned)texFail, (unsigned)familyIdsUsedCount);
                SRL::Debug::Print(1, 18, "S1 TEX miss:%u dec:%u up:%u",
                                  (unsigned)texMissFamily, (unsigned)texDecodeFail, (unsigned)texUploadFail);

                // Build fallback remap tables for TrackRenderer path (global face order).
                // This allows LOD swap even when component renderer is disabled.
                seg1RendererLodReady_ = false;
                for (auto& v : seg1RendererFaceSlotsByLod_) v.clear();
                if (seg1Renderer)
                {
                    Segment1TextureJson map1ForSeg{};
                    bool map1ForSegOk = false;
                    if (g_seg1MapCacheValid && !g_seg1MapCache.faceFamily.empty())
                    {
                        map1ForSeg = g_seg1MapCache;
                        map1ForSegOk = true;
                    }
                    else
                    {
                        const char* famCandidates[] = {
                            "CD/DATA/S001FAM.BIN",
                            "CD/DATA/S001FAM.BIN;1",
                            "DATA/S001FAM.BIN",
                            "DATA/S001FAM.BIN;1",
                            "S001FAM.BIN",
                            "S001FAM.BIN;1",
                            "s001fam.bin",
                            "s001fam.bin;1"
                        };
                        std::vector<uint8_t> famBin{};
                        if (ReadCdFileBinary(famCandidates, sizeof(famCandidates) / sizeof(famCandidates[0]), famBin) &&
                            famBin.size() >= 12)
                        {
                            const uint32_t magic = ReadLe32(famBin.data() + 0);
                            const uint16_t ver = ReadLe16(famBin.data() + 4);
                            const uint16_t segId = ReadLe16(famBin.data() + 8);
                            const uint16_t faceCount = ReadLe16(famBin.data() + 10);
                            const size_t need = static_cast<size_t>(12) + static_cast<size_t>(faceCount) * sizeof(uint16_t);
                            if (magic == 0x4D463153 && ver == 1 && segId == 1 && famBin.size() >= need)
                            {
                                map1ForSeg.faceFamily.clear();
                                map1ForSeg.faceFamily.reserve(faceCount);
                                for (uint16_t i = 0; i < faceCount; ++i)
                                {
                                    const size_t off = 12 + static_cast<size_t>(i) * 2;
                                    map1ForSeg.faceFamily.push_back(static_cast<int>(ReadLe16(famBin.data() + off)));
                                }
                                map1ForSegOk = !map1ForSeg.faceFamily.empty();
                            }
                        }
                    }

                    if (!map1ForSegOk)
                    {
                        const char* jsonCandidates[] = {
                            "SMAP.TXT",
                            "SMAP.TXT;1",
                            "CD/DATA/SMAP.TXT",
                            "CD/DATA/SMAP.TXT;1",
                            "DATA/SMAP.TXT",
                            "DATA/SMAP.TXT;1",
                            "segments_map.json",
                            "segments_map.json;1",
                            "CD/DATA/segments_map.json",
                            "CD/DATA/segments_map.json;1",
                            "DATA/segments_map.json",
                            "DATA/segments_map.json;1"
                        };
                        std::vector<char> jsonText{};
                        map1ForSegOk =
                            ReadCdFileText(jsonCandidates, sizeof(jsonCandidates) / sizeof(jsonCandidates[0]), jsonText) &&
                            ParseSegment1TextureJson(jsonText.data(), map1ForSeg) &&
                            !map1ForSeg.faceFamily.empty();
                    }

                    const size_t rendererFaces = static_cast<size_t>(seg1Renderer->FaceCount());
                    const size_t mapFaces = static_cast<size_t>(matView.header.faceCount);
                    const size_t nFaces = std::min(rendererFaces, mapFaces);
                    for (size_t li = 0; li < 4; ++li)
                    {
                        auto& slots = seg1RendererFaceSlotsByLod_[li];
                        slots.assign(rendererFaces, -1);
                        size_t mappedFaces = 0;

                        // Prefer face order from MAT because it is generated from the same OBJ/MTL
                        // used to define per-face material assignment. Use S001FAM/SMAP only to
                        // fill any remaining unmapped faces.
                        for (size_t fi = 0; fi < nFaces; ++fi)
                        {
                            SegmentComponent::MatFaceBinding mb{};
                            const size_t moff = matView.bindingOffset + fi * sizeof(SegmentComponent::MatFaceBinding);
                            if (!SegmentComponent::Loader::ReadMatFaceBindingLeAt(matBlob.bytes, moff, mb)) continue;
                            const uint16_t fam = static_cast<uint16_t>(mb.materialId);
                            if (fam == 0) continue;
                            uint16_t slot = No_Texture;
                            if (TryGetFamilyLodSlot(seg1FamilySlots_, fam, static_cast<uint8_t>(li), slot))
                            {
                                slots[fi] = static_cast<int32_t>(slot);
                                ++mappedFaces;
                            }
                        }

                        if (map1ForSegOk)
                        {
                            const size_t faceCount = std::min(rendererFaces, map1ForSeg.faceFamily.size());
                            for (size_t fi = 0; fi < faceCount; ++fi)
                            {
                                if (slots[fi] >= 0) continue;
                                const uint16_t fam = static_cast<uint16_t>(map1ForSeg.faceFamily[fi]);
                                if (fam == 0) continue;
                                uint16_t slot = No_Texture;
                                if (TryGetFamilyLodSlot(seg1FamilySlots_, fam, static_cast<uint8_t>(li), slot))
                                {
                                    slots[fi] = static_cast<int32_t>(slot);
                                    ++mappedFaces;
                                }
                            }
                        }
                        // Safety fallback: if no face was mapped for this LOD but we have at least
                        // one valid slot, force-apply that slot to all faces to validate path.
                        if (mappedFaces == 0 && !seg1FamilySlots_.empty())
                        {
                            const uint16_t fallbackSlot = seg1FamilySlots_[0].lodSlots[li];
                            if (fallbackSlot != No_Texture)
                            {
                                for (size_t fi = 0; fi < rendererFaces; ++fi)
                                {
                                    slots[fi] = static_cast<int32_t>(fallbackSlot);
                                }
                                mappedFaces = rendererFaces;
                            }
                        }
                        const int lodDbg[4] = { 8, 16, 32, 64 };
                        SRL::Debug::Print(1, 18, "S1 M%d:%u mp:%u", lodDbg[li], (unsigned)mappedFaces, map1ForSegOk ? 1u : 0u);
                    }
                    seg1RendererLodReady_ = (rendererFaces > 0);
                    if (seg1RendererLodReady_)
                    {
                        // Apply initial LOD map immediately.
                        (void)seg1Renderer->ApplyFaceTextureSlotsGlobal(seg1RendererFaceSlotsByLod_[seg1CurrentLodIndex_]);
                    }
                    SRL::Debug::Print(1, 20, "S1 RDY:%d f:%u fm:%u",
                                      seg1RendererLodReady_ ? 1 : 0,
                                      (unsigned)rendererFaces,
                                      (unsigned)familyIdsUsedCount);
                    if (texLoaded == 0)
                    {
                        SRL::Debug::Print(1, 18, "S1MISS c:%u u:%u", (unsigned)texLoaded, (unsigned)texUploadFail);
                    }
                }
                else
                {
                    SRL::Debug::Print(1, 20, "S1 RDY:0 f:0 fm:%u", (unsigned)familyIdsUsedCount);
                }

                seg1FaceFamilyIds_.clear();
                seg1FaceFamilyIds_.reserve(static_cast<size_t>(geoView.header.faceCount));
                for (uint32_t fi = 0; fi < geoView.header.faceCount && !seg1ComponentVerts_.empty(); ++fi)
                {
                    SegmentComponent::GeoFace gf{};
                    const size_t off = geoView.faceOffset + static_cast<size_t>(fi) * sizeof(SegmentComponent::GeoFace);
                    if (!SegmentComponent::Loader::ReadGeoFaceLeAt(geoBlob.bytes, off, gf))
                    {
                        seg1ComponentFaces_.clear();
                        seg1ComponentAttrs_.clear();
                        break;
                    }
                    SRL::Types::Polygon p{};
                    p.Normal = Vector3D(0.0, 0.0, 0.0);
                    for (size_t c = 0; c < 4; ++c)
                    {
                        p.Vertices[c] = gf.vertex[c];
                    }
                    if (gf.kind == static_cast<uint8_t>(SegmentComponent::FaceKind::Triangle))
                    {
                        p.Vertices[3] = p.Vertices[2];
                    }
                    seg1ComponentFaces_.push_back(p);

                    uint16_t texIndex = No_Texture;
                    uint16_t baseColor = static_cast<uint16_t>(0x841F);
                    uint16_t drawMode = CL32KRGB;
                    uint32_t directionFlags = sprPolygon;
                    uint16_t shading = UseLight;

                    SegmentComponent::MatFaceBinding mb{};
                    const size_t moff = matView.bindingOffset + static_cast<size_t>(fi) * sizeof(SegmentComponent::MatFaceBinding);
                    uint16_t faceFamilyId = 0;
                    if (SegmentComponent::Loader::ReadMatFaceBindingLeAt(matBlob.bytes, moff, mb))
                    {
                        const int fam = static_cast<int>(mb.materialId);
                        const uint16_t m = static_cast<uint16_t>(mb.materialId & 0x1F);
                        baseColor = static_cast<uint16_t>(0x8400 | (m ? m : 0x1F));
                        faceFamilyId = static_cast<uint16_t>((fam > 0) ? fam : 0);
                        if (fam > 0)
                        {
                            uint16_t slot = No_Texture;
                            (void)TryGetFamilyLodSlot(seg1FamilySlots_,
                                                      static_cast<uint16_t>(fam),
                                                      seg1CurrentLodIndex_,
                                                      slot);
                            if (slot != No_Texture)
                            {
                                texIndex = slot;
                                baseColor = No_Palet;
                                drawMode = static_cast<uint16_t>(CL32KRGB | CL_Gouraud);
                                directionFlags = sprNoflip;
                                shading = UseLight;
                            }
                        }
                    }
                    seg1FaceFamilyIds_.push_back(faceFamilyId);

                    seg1ComponentAttrs_.push_back(SRL::Types::Attribute(
                        SRL::Types::Attribute::FaceVisibility::DoubleSided,
                        SRL::Types::Attribute::SortMode::Center,
                        texIndex,
                        baseColor,
                        CL32KRGB,
                        drawMode,
                        directionFlags,
                        shading));
                }

                if (kUseSeg1ComponentRenderer &&
                    !seg1ComponentVerts_.empty() &&
                    seg1ComponentFaces_.size() == static_cast<size_t>(geoView.header.faceCount) &&
                    seg1ComponentAttrs_.size() == seg1ComponentFaces_.size())
                {
                    seg1ComponentEnabled_ = true;
                    seg1ComponentCenter_ = (minv + maxv) / SRL::Math::Types::Fxp::BuildRaw(2 << 16);
                    seg1Entry->center = seg1ComponentCenter_;
                    if (kShowSeg1ComponentProbeLogs)
                    {
                        SRL::Debug::Print(1, 29, "CMP SEG001 renderer: component ON v:%u f:%u",
                                          (unsigned)seg1ComponentVerts_.size(),
                                          (unsigned)seg1ComponentFaces_.size());
                    }
                }
                else
                {
                    seg1ComponentEnabled_ = false;
                    if (kShowSeg1ComponentProbeLogs)
                    {
                        SRL::Debug::Print(1, 29, "CMP SEG001 renderer: component OFF");
                    }
                }
            }
        }

        // Fallback path (temporarily disabled for stability).
        constexpr bool kEnableSeg1JsonFallback = false;
        if (kEnableSeg1JsonFallback && !seg1RendererLodReady_)
        {
            TrackRenderer* seg1Renderer = nullptr;
            for (auto& seg : segmentRenderers_)
            {
                if (seg.id == 1 && seg.renderer) { seg1Renderer = seg.renderer.get(); break; }
            }
            if (seg1Renderer)
            {
                const char* jsonCandidates[] = {
                    "CD/DATA/segments_map.json",
                    "CD/DATA/segments_map.json;1",
                    "DATA/segments_map.json",
                    "DATA/segments_map.json;1",
                    "segments_map.json",
                    "segments_map.json;1",
                    "SMAP.TXT;1",
                    "CD/DATA/SMAP.TXT",
                    "CD/DATA/SMAP.TXT;1",
                    "DATA/SMAP.TXT",
                    "DATA/SMAP.TXT;1",
                    "SMAP.TXT"
                };
                std::vector<char> jsonText{};
                Segment1TextureJson map1{};
                const bool jsonOk = ReadCdFileText(jsonCandidates, sizeof(jsonCandidates) / sizeof(jsonCandidates[0]), jsonText) &&
                                    ParseFaceFamilyArrayForSegment1(jsonText.data(), map1);

                if (jsonOk && !map1.faceFamily.empty())
                {
                    const int lodValues[4] = { 8, 16, 32, 64 };
                    int familyIdsUsed[512]{};
                    const size_t familyIdsUsedCount = BuildUniqueUsedFamilies(map1.faceFamily, familyIdsUsed, 512);

                    InitializeFamilySlots(seg1FamilySlots_, familyIdsUsed, familyIdsUsedCount);

                    auto loadTexbankToCart = [&](size_t li) -> bool
                    {
                        return LoadSeg1TexbankIndexToCart(li, lodValues[li]);
                    };

                    size_t texLoaded = 0;
                    size_t texFail = 0;
                    size_t texMissFamily = 0;
                    size_t texDecodeFail = 0;
                    size_t texUploadFail = 0;
                    for (size_t li = 0; li < 4; ++li)
                    {
                        if (!loadTexbankToCart(li))
                        {
                            texFail += familyIdsUsedCount;
                            continue;
                        }

                        for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                        {
                            bool sawMissingFamily = false;
                            bool sawDecodeFail = false;
                            bool sawUploadFail = false;
                            const bool loaded = TryLoadFamilyLodSlot(seg1FamilySlots_[u],
                                                                      static_cast<uint8_t>(li),
                                                                      /*fallbackToLowerLods*/false,
                                                                      /*fallbackToHigherLods*/false,
                                                                      &sawMissingFamily,
                                                                      &sawDecodeFail,
                                                                      &sawUploadFail,
                                                                      nullptr);
                            if (loaded)
                            {
                                ++texLoaded;
                            }
                            else
                            {
                                ++texFail;
                                if (sawDecodeFail) ++texDecodeFail;
                                else if (sawUploadFail) ++texUploadFail;
                                else ++texMissFamily;
                            }
                        }
                    }

                    for (auto& v : seg1RendererFaceSlotsByLod_) v.clear();
                    const size_t rendererFaces = static_cast<size_t>(seg1Renderer->FaceCount());
                    const size_t nFaces = std::min(rendererFaces, map1.faceFamily.size());
                    for (size_t li = 0; li < 4; ++li)
                    {
                        auto& slots = seg1RendererFaceSlotsByLod_[li];
                        slots.assign(rendererFaces, -1);
                        for (size_t fi = 0; fi < nFaces; ++fi)
                        {
                            const uint16_t fam = static_cast<uint16_t>(map1.faceFamily[fi] < 0 ? 0 : map1.faceFamily[fi]);
                            if (fam == 0) continue;
                            uint16_t slot = No_Texture;
                            if (TryGetFamilyLodSlot(seg1FamilySlots_, fam, static_cast<uint8_t>(li), slot))
                            {
                                slots[fi] = static_cast<int32_t>(slot);
                            }
                        }
                    }
                    seg1RendererLodReady_ = (rendererFaces > 0 && !seg1FamilySlots_.empty());
                    if (seg1RendererLodReady_)
                    {
                        (void)seg1Renderer->ApplyFaceTextureSlotsGlobal(seg1RendererFaceSlotsByLod_[seg1CurrentLodIndex_]);
                    }
                    SRL::Debug::Print(1, 30, "S1 TEX ok:%u fl:%u fm:%u",
                                      (unsigned)texLoaded, (unsigned)texFail, (unsigned)familyIdsUsedCount);
                    SRL::Debug::Print(1, 18, "S1 TEX miss:%u dec:%u up:%u",
                                      (unsigned)texMissFamily, (unsigned)texDecodeFail, (unsigned)texUploadFail);
                    SRL::Debug::Print(1, 20, "S1 RDY:%d f:%u fm:%u",
                                      seg1RendererLodReady_ ? 1 : 0,
                                      (unsigned)rendererFaces,
                                      (unsigned)familyIdsUsedCount);
                }
            }
        }
    }

    // Dynamic texture upgrade test (phase 1): only SEG_001 uses 64x64 textures from JSON map.
    // Disabled for stability.
    constexpr bool kEnableSeg1JsonTextureUpgrade = false;
    if (kEnableSeg1JsonTextureUpgrade && !segmentRenderers_.empty())
    {
        const char* jsonCandidates[] = {
            "CD/DATA/segments_map.json",
            "CD/DATA/segments_map.json;1",
            "DATA/segments_map.json",
            "DATA/segments_map.json;1",
            "segments_map.json",
            "segments_map.json;1"
        };
        std::vector<char> jsonText;
        if (ReadCdFileText(jsonCandidates, sizeof(jsonCandidates) / sizeof(jsonCandidates[0]), jsonText))
        {
            Segment1TextureJson map1{};
            if (ParseSegment1TextureJson(jsonText.data(), map1))
            {
                TrackRenderer* seg1Renderer = nullptr;
                for (auto& seg : segmentRenderers_)
                {
                    if (seg.id == 1 && seg.renderer) { seg1Renderer = seg.renderer.get(); break; }
                }
                if (!seg1Renderer)
                {
                    SRL::Debug::Print(1, 23, "SEG001 renderer missing");
                }
                else
                {
                    const size_t seg1FaceCount = static_cast<size_t>(seg1Renderer->FaceCount());
                    if (seg1FaceCount == 0)
                    {
                        SRL::Debug::Print(1, 23, "SEG001 facecount zero");
                    }
                    else
                    {
                        if (map1.faceFamily.size() > seg1FaceCount)
                        {
                            // Strict bound to avoid any mismatch-induced overwrite risk.
                            map1.faceFamily.resize(seg1FaceCount);
                        }

                        std::vector<int16_t> faceSlots;
                        seg1Renderer->CollectFaceTextureSlotsGlobal(faceSlots);
                        const size_t mapCount = std::min(map1.faceFamily.size(), std::min(faceSlots.size(), seg1FaceCount));
                        if (mapCount == 0)
                        {
                            SRL::Debug::Print(1, 23, "SEG001 no face map");
                        }
                        else
                        {
                            int famIds[256]{};
                            int32_t famSlots[256]{};
                            size_t famCount = 0;
                            for (size_t fi = 0; fi < mapCount; ++fi)
                            {
                                const int fam = map1.faceFamily[fi];
                                const int32_t slot = faceSlots[fi];
                                if (fam < 0 || slot <= 0) continue;
                                bool exists = false;
                                for (size_t i = 0; i < famCount; ++i)
                                {
                                    if (famIds[i] == fam) { exists = true; break; }
                                }
                                if (!exists && famCount < 256)
                                {
                                    famIds[famCount] = fam;
                                    famSlots[famCount] = slot;
                                    ++famCount;
                                }
                            }

                            size_t okCount = 0;
                            size_t failCount = 0;
                            constexpr size_t kFamilyUpgradeCap = 24;
                            size_t upgraded = 0;
                            for (size_t i = 0; i < famCount; ++i)
                            {
                                if (upgraded >= kFamilyUpgradeCap) break;
                                const int fam = famIds[i];
                                const int fi = FindFamilyIndex(map1, fam);
                                if (fi < 0) continue;

                                char fileNorm[64]{};
                                NormalizeTextureFileName(map1.familyTex64[fi], fileNorm, sizeof(fileNorm));
                                if (TryOverwriteTextureSlotFromCd(famSlots[i], fileNorm))
                                {
                                    ++okCount;
                                    ++upgraded;
                                }
                                else
                                {
                                    ++failCount;
                                }
                            }
                            SRL::Debug::Print(1, 23, "SEG001 slot-upgrade ok:%u fail:%u fam:%u",
                                              (unsigned)okCount, (unsigned)failCount, (unsigned)famCount);
                        }
                    }
                }
            }
            else
            {
                SRL::Debug::Print(1, 23, "segments_map.json parse fail for SEG001");
            }
        }
        else
        {
            SRL::Debug::Print(1, 23, "segments_map.json not found");
        }
    }

    // Phase A/B warmup (disabled for runtime stability on SH2).
    constexpr bool kEnableSeg1Warmup = false;
    constexpr bool kEnableSeg1WarmupTexbank = false;
    constexpr size_t kSeg1WarmupTexbankCount = 1; // incremental: 8 only
    if (kEnableSeg1Warmup)
    {
        const char* mapCandidates[] = {
            "CD/DATA/S001FAM.BIN",
            "CD/DATA/S001FAM.BIN;1",
            "DATA/S001FAM.BIN",
            "DATA/S001FAM.BIN;1",
            "S001FAM.BIN",
            "S001FAM.BIN;1",
            "s001fam.bin",
            "s001fam.bin;1"
        };
        std::vector<uint8_t> mapBin{};
        bool mapOk = false;
        std::vector<int> faceFamily{};
        if (ReadCdFileBinary(mapCandidates, sizeof(mapCandidates) / sizeof(mapCandidates[0]), mapBin) && mapBin.size() >= 12)
        {
            const uint32_t magic = ReadLe32(mapBin.data() + 0);
            const uint16_t ver = ReadLe16(mapBin.data() + 4);
            const uint16_t segId = ReadLe16(mapBin.data() + 8);
            const uint16_t faceCount = ReadLe16(mapBin.data() + 10);
            if (magic == 0x4D463153 && ver == 1 && segId == 1)
            {
                const size_t need = static_cast<size_t>(12) + static_cast<size_t>(faceCount) * sizeof(uint16_t);
                if (mapBin.size() >= need)
                {
                    faceFamily.reserve(faceCount);
                    for (uint16_t i = 0; i < faceCount; ++i)
                    {
                        const size_t off = 12 + static_cast<size_t>(i) * 2;
                        faceFamily.push_back(static_cast<int>(ReadLe16(mapBin.data() + off)));
                    }
                    mapOk = true;
                }
            }
        }

        size_t famCount = 0;
        if (mapOk && !faceFamily.empty())
        {
            int familyIdsUsed[512]{};
            famCount = BuildUniqueUsedFamilies(faceFamily, familyIdsUsed, 512);
            if (seg1FamilySlots_.empty() && famCount > 0)
            {
                InitializeFamilySlots(seg1FamilySlots_, familyIdsUsed, famCount);
            }
        }

        size_t banksOk = 0;
        if (kEnableSeg1WarmupTexbank)
        {
            const int lodValues[4] = { 8, 16, 32, 64 };
            const size_t loadCount = std::min<size_t>(kSeg1WarmupTexbankCount, 4);
            for (size_t li = 0; li < loadCount; ++li)
            {
                if (LoadSeg1TexbankIndexToCart(li, lodValues[li])) ++banksOk;
            }
        }

        SRL::Debug::Print(1, 20, "S1 MAP ok:%u f:%u fm:%u",
                          mapOk ? 1u : 0u,
                          mapOk ? (unsigned)faceFamily.size() : 0u,
                          (unsigned)famCount);
        SRL::Debug::Print(1, 21, "S1 TBK ok:%u/4", (unsigned)banksOk);
    }

    // Keep track rendering available even when coordinator allocation fails.
    // RenderFrame will use a direct fallback path when coordinator is unavailable.
    ready_ = segmentsReady_;
    return ready_;
}

void TrackSystem::BeginFrame(uint32_t frameId)
{
    textureUploadsThisFrame_ = 0;
    usedTextureSlotsThisFrame_.fill(0u);
    runtimeRdrBuildsThisFrame_ = 0;
    runtimeSdrBuildsThisFrame_ = 0;
    runtimeFaceRemapsThisFrame_ = 0;
    runtimeSlidesThisFrame_ = 0;
    runtimeSlideStallsThisFrame_ = 0;
    runtimePrefetchHitsThisFrame_ = 0;
    runtimePrefetchMissesThisFrame_ = 0;
    runtimeLodSegmentUpdatesThisFrame_ = 0;
    runtimeSafeRenderedThisFrame_ = 0;
    runtimeSafeSkippedThisFrame_ = 0;
    runtimeSafeNoDrawThisFrame_ = 0;
    runtimeSafeReappliedThisFrame_ = 0;
    if (kEnableTrackRuntimeStabilization && segmentsReady_ && totalSegmentCount_ > 0)
    {
        const int32_t currentStartId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
        const int32_t targetStartId = WrapSegmentIdToRange(targetWindowStartId_, totalSegmentCount_);
        if (currentStartId > 0 && targetStartId > 0)
        {
            const int32_t total = static_cast<int32_t>(totalSegmentCount_);
            int32_t backlog = (targetStartId - currentStartId) % total;
            if (backlog < 0) backlog += total;
            if (backlog > 0 && backlog < (total / 2))
            {
                // Safe mode must still behave as a strict sliding window, but
                // when the car moves faster than one segment per frame we need
                // limited catch-up. Otherwise the car outruns the 20-segment
                // window even without an explicit slide stall.
                const uint8_t maxCatchupSlides = 3u;
                const uint8_t slideBudget = static_cast<uint8_t>(
                    std::min<int32_t>(backlog, static_cast<int32_t>(maxCatchupSlides)));
                for (uint8_t i = 0; i < slideBudget; ++i)
                {
                    TryPrefetchUpcomingSegment();
                    if (!SlideActiveSegmentWindow(1, +1))
                    {
                        activeWindowSwitchCooldown_ = 0;
                        break;
                    }

                    const int32_t progressedStartId =
                        WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
                    if (progressedStartId == targetStartId) break;
                }
            }
        }
    }
    coordinator_.BeginFrame(frameId);
}

std::vector<TrackSystem::SegmentHandle> TrackSystem::BuildVisibleSegmentOrder(
    const Vector3D& trackOffset,
    const Vector3D& cameraLocation)
{
    std::vector<SegmentHandle> orderedHandles{};
    if (segmentRenderers_.empty()) return orderedHandles;
    const size_t windowCount = segmentRenderers_.size();
    if (kEnableTrackRuntimeStabilization ||
        segmentHandles_.empty() ||
        segmentHandles_.size() != windowCount)
    {
        segmentHandles_ = BuildSegmentHandleTable();
    }
    if (segmentHandles_.empty()) return orderedHandles;
    orderedHandles.reserve(windowCount);
    size_t invalidHandleCount = 0;
    for (size_t i = 0; i < windowCount; ++i)
    {
        const SegmentHandle h = segmentHandles_[i];
        if (!segmentPool_.Resolve(h))
        {
            ++invalidHandleCount;
            continue;
        }
        orderedHandles.push_back(h);
    }
    if (invalidHandleCount > 0)
    {
        segmentHandles_ = BuildSegmentHandleTable();
        orderedHandles.clear();
        for (size_t i = 0; i < windowCount; ++i)
        {
            const SegmentHandle h = segmentHandles_[i % windowCount];
            if (!segmentPool_.Resolve(h)) continue;
            orderedHandles.push_back(h);
        }
        if (orderedHandles.size() != windowCount)
        {
            SRL::Debug::Print(1, 23, "TRK vis hole start:%d head:%u n:%u ok:%u",
                              activeWindowStartId_,
                              static_cast<unsigned>(activeWindowHead_),
                              static_cast<unsigned>(windowCount),
                              static_cast<unsigned>(orderedHandles.size()));
        }
        if (orderedHandles.empty())
        {
            return orderedHandles;
        }
    }
    if (orderedHandles.size() > 1 && totalSegmentCount_ > 0)
    {
        const int32_t startId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
        const int32_t dir = (windowDirection_ < 0) ? -1 : 1;
        std::sort(orderedHandles.begin(), orderedHandles.end(),
            [&](const SegmentHandle& a, const SegmentHandle& b)
            {
                const auto* ea = segmentPool_.Resolve(a);
                const auto* eb = segmentPool_.Resolve(b);
                if (!ea && !eb) return false;
                if (!ea) return false;
                if (!eb) return true;
                int32_t da = (dir > 0) ? (ea->id - startId) : (startId - ea->id);
                int32_t db = (dir > 0) ? (eb->id - startId) : (startId - eb->id);
                if (da < 0) da += static_cast<int32_t>(totalSegmentCount_);
                if (db < 0) db += static_cast<int32_t>(totalSegmentCount_);
                if (da == db) return ea->id < eb->id;
                return da < db;
            });
    }

    auto depthMetricToCamera = [&](const SegmentRenderEntry* e) -> SRL::Math::Types::Fxp
    {
        if (!e) return SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        const Vector3D c = e->center + trackOffset;
        const auto dx = (c.X - cameraLocation.X).Abs();
        const auto dz = (c.Z - cameraLocation.Z).Abs();
        const auto major = (dx > dz) ? dx : dz;
        const auto minor = (dx > dz) ? dz : dx;
        return major + minor;
    };

    const size_t keepCount =
        std::min<size_t>(
            orderedHandles.size(),
            static_cast<size_t>(fixedVisibleSegmentCap_));
    orderedHandles.resize(keepCount);
    UpdateVisibleSegmentLods(orderedHandles);

    // Draw in camera-space painter order (far -> near).
    // VDP1 has no Z-buffer for this path, so camera-relative order is required
    // to avoid distortion when camera rotates around the car.
    if (kEnableTrackRuntimeStabilization)
    {
        std::sort(orderedHandles.begin(), orderedHandles.end(),
            [&](const SegmentHandle& a, const SegmentHandle& b)
            {
                const auto* ea = segmentPool_.Resolve(a);
                const auto* eb = segmentPool_.Resolve(b);
                if (!ea && !eb) return false;
                if (!ea) return false;
                if (!eb) return true;
                const auto da = depthMetricToCamera(ea);
                const auto db = depthMetricToCamera(eb);
                if (da == db)
                {
                    const int32_t startId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
                    const int32_t dir = (windowDirection_ < 0) ? -1 : 1;
                    int32_t wa = (dir > 0) ? (ea->id - startId) : (startId - ea->id);
                    int32_t wb = (dir > 0) ? (eb->id - startId) : (startId - eb->id);
                    if (wa < 0) wa += static_cast<int32_t>(totalSegmentCount_);
                    if (wb < 0) wb += static_cast<int32_t>(totalSegmentCount_);
                    return wa < wb;
                }
                return da > db; // far first
            });
        return orderedHandles;
    }

    std::sort(orderedHandles.begin(), orderedHandles.end(),
        [&](const SegmentHandle& a, const SegmentHandle& b)
        {
            const auto* ea = segmentPool_.Resolve(a);
            const auto* eb = segmentPool_.Resolve(b);
            if (!ea && !eb) return false;
            if (!ea) return false;
            if (!eb) return true;
            const auto da = depthMetricToCamera(ea);
            const auto db = depthMetricToCamera(eb);
            if (da == db) return ea->id < eb->id;
            return da > db; // far first
        });
    return orderedHandles;
}

void TrackSystem::RunSeg1DiagnosticsForFrame()
{
    // Single-face overwrite probe disabled.
    if (false && seg1SingleFaceSwapReady_ && seg1SingleFaceSwapBaseSlot_ > 0)
    {
        if (seg1SingleFaceSwapCounter_ >= seg1SingleFaceSwapFrames_)
        {
            seg1SingleFaceSwapCounter_ = 0;
            seg1SingleFaceSwapUseAlt_ = !seg1SingleFaceSwapUseAlt_;

            bool ok = false;
            if (seg1SingleFaceSwapUseAlt_)
            {
                ok = TryOverwriteTextureSlotFromCd(seg1SingleFaceSwapBaseSlot_, "area_escape_8.tga") ||
                     TryOverwriteTextureSlotFromCd(seg1SingleFaceSwapBaseSlot_, "AREA_ESCAPE_8.TGA");
            }
            else
            {
                ok = TryOverwriteTextureSlotFromCd(seg1SingleFaceSwapBaseSlot_, "asfalto_8.tga") ||
                     TryOverwriteTextureSlotFromCd(seg1SingleFaceSwapBaseSlot_, "ASFALTO_8.TGA");
            }
            SRL::Debug::Print(1, 21, "S1 OVR sw:%u ok:%u",
                              seg1SingleFaceSwapUseAlt_ ? 1u : 0u,
                              ok ? 1u : 0u);
        }
        else
        {
            ++seg1SingleFaceSwapCounter_;
        }
    }

    // SEG_001 LOD cycle test (experimental).
    // Disabled by default to keep texture assignment deterministic in gameplay.
    constexpr bool kEnableSeg1LodCycleTest = false;
    constexpr bool kEnableSeg1LodCycleLogs = false;
    const bool isTrueSingleSegmentScene =
        (segmentRenderers_.size() == 1) &&
        (segmentRenderers_[0].logicalSegmentCount == 1) &&
        (segmentRenderers_[0].id == 1);
    if (kEnableSeg1LodCycleTest &&
        isTrueSingleSegmentScene &&
        (seg1ComponentEnabled_ || seg1RendererLodReady_) &&
        !seg1FamilySlots_.empty())
    {
        if (seg1LodFrameCounter_ >= seg1LodSwapFrames_)
        {
            seg1LodFrameCounter_ = 0;
            seg1CurrentLodIndex_ = static_cast<uint8_t>((seg1CurrentLodIndex_ + 1) & 0x03);

            if (seg1ComponentEnabled_ &&
                !seg1ComponentAttrs_.empty() &&
                seg1ComponentAttrs_.size() == seg1FaceFamilyIds_.size())
            {
                for (size_t fi = 0; fi < seg1ComponentAttrs_.size(); ++fi)
                {
                    const uint16_t fam = seg1FaceFamilyIds_[fi];
                    uint16_t slot = No_Texture;
                    if (fam != 0)
                    {
                        (void)TryGetFamilyLodSlot(seg1FamilySlots_, fam, seg1CurrentLodIndex_, slot);
                    }

                    auto& attr = seg1ComponentAttrs_[fi];
                    if (slot != No_Texture)
                    {
                        attr.Texture = slot;
                        attr.ColorMode = No_Palet;
                    }
                }
            }

            if (seg1RendererLodReady_)
            {
                size_t appliedRenderer = 0;
                for (auto& entry : segmentRenderers_)
                {
                    if (entry.id == 1 && entry.renderer)
                    {
                        appliedRenderer = entry.renderer->ApplyFaceTextureSlotsGlobal(
                            seg1RendererFaceSlotsByLod_[seg1CurrentLodIndex_]);
                        break;
                    }
                }
                if (kEnableSeg1LodCycleLogs)
                {
                    SRL::Debug::Print(1, 21, "S1 AP rdr:%u", (unsigned)appliedRenderer);
                }
            }
            else if (kEnableSeg1LodCycleLogs)
            {
                SRL::Debug::Print(1, 21, "S1 AP rdr:off");
            }
            if (kEnableSeg1LodCycleLogs)
            {
                const int lodDbg[4] = { 8, 16, 32, 64 };
                const unsigned rdrFaces = seg1RendererLodReady_ ? (unsigned)seg1RendererFaceSlotsByLod_[seg1CurrentLodIndex_].size() : 0u;
                SRL::Debug::Print(1, 22, "S1L c:%u j:%u l:%d r:%u",
                                  (unsigned)seg1TgaPreloadCount_,
                                  (unsigned)seg1TgaJsonOk_,
                                  lodDbg[seg1CurrentLodIndex_], rdrFaces);
            }
        }
        else
        {
            ++seg1LodFrameCounter_;
        }
    }
}

void TrackSystem::RenderVisibleSegmentOrder(
    const std::vector<SegmentHandle>& orderedHandles,
    const Vector3D& trackOffset,
    const Vector3D& lightDirection,
    const Vector3D& cameraLocation,
    std::array<uint8_t, kTrackSegmentLimit + 1>& preparedCountById,
    std::array<uint8_t, kTrackSegmentLimit + 1>& renderedCountById,
    bool& segment01Logged,
    bool& segment01Prepared)
{
    if (kEnableTrackRuntimeStabilization)
    {
        if (ValidateAndRepairWindowState())
        {
            RefreshFamilyWorkingSet(false);
        }
        std::vector<SegmentRenderEntry*> orderedEntries{};
        orderedEntries.reserve(segmentRenderers_.size());
        for (auto& entry : segmentRenderers_)
        {
            if (!entry.renderer) continue;
            orderedEntries.push_back(&entry);
        }

        auto depthMetricToCamera = [&](const SegmentRenderEntry* e) -> SRL::Math::Types::Fxp
        {
            if (!e) return SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
            const Vector3D c = e->center + trackOffset;
            const auto dx = (c.X - cameraLocation.X).Abs();
            const auto dz = (c.Z - cameraLocation.Z).Abs();
            const auto major = (dx > dz) ? dx : dz;
            const auto minor = (dx > dz) ? dz : dx;
            return major + minor;
        };

        std::sort(orderedEntries.begin(), orderedEntries.end(),
            [&](const SegmentRenderEntry* a, const SegmentRenderEntry* b)
            {
                if (!a && !b) return false;
                if (!a) return false;
                if (!b) return true;
                const auto da = depthMetricToCamera(a);
                const auto db = depthMetricToCamera(b);
                if (da == db)
                {
                    const int32_t startId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
                    const int32_t dir = (windowDirection_ < 0) ? -1 : 1;
                    int32_t wa = (dir > 0) ? (a->id - startId) : (startId - a->id);
                    int32_t wb = (dir > 0) ? (b->id - startId) : (startId - b->id);
                    if (wa < 0) wa += static_cast<int32_t>(totalSegmentCount_);
                    if (wb < 0) wb += static_cast<int32_t>(totalSegmentCount_);
                    return wa < wb;
                }
                return da > db; // far first
            });

        const size_t limit = std::min<size_t>(
            orderedEntries.size(),
            std::max<size_t>(1u, static_cast<size_t>(fixedVisibleSegmentCap_)));
        size_t renderedEntries = 0;
        for (size_t i = 0; i < limit; ++i)
        {
            auto* entry = orderedEntries[i];
            if (!entry || !entry->renderer) continue;
            if (!IsRendererStateIntegral(*entry->renderer))
            {
                if (!TryRepairRendererState(*entry->renderer) &&
                    !RebuildSafeSegmentEntry(*entry))
                {
                    SRL::Debug::Print(1, 21, "TRK inv id:%d m:%u f:%u v:%u d:%u",
                                      entry->id,
                                      static_cast<unsigned>(entry->renderer->MeshCount()),
                                      static_cast<unsigned>(entry->renderer->FaceCount()),
                                      static_cast<unsigned>(entry->renderer->VertexCount()),
                                      static_cast<unsigned>(entry->renderer->DrawLimit()));
                    ++runtimeSafeSkippedThisFrame_;
                    continue;
                }
            }
            const uint32_t missingSlots = CountMissingOrDeadRequiredFaceTextureSlots(
                entry->lodState.currentFaceSlots,
                &entry->lodState.faceFamilyIds);
            if (missingSlots > 0)
            {
                const bool remapped = RebuildSegmentFaceSlotsForLod(*entry, 0, seg1FamilySlots_);
                if (remapped)
                {
                    (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
                    entry->lodState.currentLodIndex = 0;
                    ++runtimeSafeReappliedThisFrame_;
                }
                if (!remapped ||
                    CountMissingOrDeadRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                               &entry->lodState.faceFamilyIds) > 0)
                {
                    if (!RebuildSafeSegmentEntry(*entry))
                    {
                        SRL::Debug::Print(1, 21, "TRK slot miss id:%d m:%u",
                                          entry->id,
                                          static_cast<unsigned>(missingSlots));
                        ++runtimeSafeSkippedThisFrame_;
                        continue;
                    }
                }
            }
            if (!entry->lodState.currentFaceSlots.empty() &&
                entry->renderer->FaceCount() == entry->lodState.currentFaceSlots.size())
            {
                (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
                ++runtimeSafeReappliedThisFrame_;
            }
            entry->renderer->SetOffset(trackOffset);
            entry->renderer->Render(lightDirection, cameraLocation);
            if (entry->renderer->LastDrawnMeshes() == 0)
            {
                if (!entry->lodState.currentFaceSlots.empty() &&
                    entry->renderer->FaceCount() == entry->lodState.currentFaceSlots.size())
                {
                    (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
                    entry->renderer->Render(lightDirection, cameraLocation);
                    ++runtimeSafeReappliedThisFrame_;
                }
                if (entry->renderer->LastDrawnMeshes() == 0)
                {
                    ++runtimeSafeNoDrawThisFrame_;
                    continue;
                }
            }
            ++runtimeSafeRenderedThisFrame_;
            ++renderedEntries;
            const int sid = entry->id;
            if (sid > 0 && sid <= static_cast<int>(kTrackSegmentLimit))
            {
                if (renderedCountById[static_cast<size_t>(sid)] < 255)
                {
                    ++renderedCountById[static_cast<size_t>(sid)];
                }
            }
        }
        return;
    }

    if (!coordinatorReady_)
    {
        // Fallback render path when coordinator is unavailable.
        for (size_t i = 0; i < orderedHandles.size(); ++i)
        {
            auto* entry = segmentPool_.Resolve(orderedHandles[i]);
            if (!entry || !entry->renderer) continue;
            entry->renderer->SetOffset(trackOffset);
            entry->renderer->Render(lightDirection, cameraLocation);
            const int sid = entry->id;
            if (sid > 0 && sid <= static_cast<int>(kTrackSegmentLimit))
            {
                if (renderedCountById[static_cast<size_t>(sid)] < 255)
                {
                    ++renderedCountById[static_cast<size_t>(sid)];
                }
            }
        }
        return;
    }

    coordinator_.Prepare(
        orderedHandles,
        coordinator_.Budget().maxTrackSegments,
        [&](const SegmentHandle& handle) -> SegmentRenderEntry*
        {
            return segmentPool_.Resolve(handle);
        },
        [&](SegmentRenderEntry& entry) -> TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult
        {
            TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult estimate{};
            if (seg1ComponentEnabled_ && entry.id == 1)
            {
                estimate.rendered = true;
                estimate.meshes = 1;
                estimate.faces = static_cast<uint32_t>(seg1ComponentFaces_.size());
                return estimate;
            }
            auto* renderer = entry.renderer.get();
            if (!renderer)
            {
                return estimate;
            }
            // Do not budget corrupted renderers for this frame.
            if (!TryRepairRendererState(*renderer))
            {
                SRL::Debug::Print(1, 23, "TRK prep skip seg:%d", entry.id);
                return estimate;
            }
            estimate.rendered = true;
            estimate.meshes = static_cast<uint32_t>(renderer->DrawLimit());
            estimate.faces = renderer->FaceCount();
            return estimate;
        },
        [&](SegmentRenderEntry& entry, TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::PreparedChunk& chunk)
        {
            chunk.segmentId = entry.id;
            chunk.center = entry.center;
            if (entry.id > 0 && entry.id <= static_cast<int>(kTrackSegmentLimit))
            {
                const size_t idx = static_cast<size_t>(entry.id);
                if (preparedCountById[idx] < 255) ++preparedCountById[idx];
            }
            if (entry.id == 1)
            {
                segment01Prepared = true;
            }
        });

    coordinator_.SetProducerStats(producer_.Stats());
    coordinator_.Execute(
        [&](const SegmentHandle& handle) -> SegmentRenderEntry*
        {
            return segmentPool_.Resolve(handle);
        },
        [&](SegmentRenderEntry& entry,
            const TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::PreparedChunk& chunk)
            -> TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult
        {
            if (seg1ComponentEnabled_ && chunk.segmentId == 1 &&
                !seg1ComponentVerts_.empty() && !seg1ComponentFaces_.empty() &&
                seg1ComponentAttrs_.size() == seg1ComponentFaces_.size())
            {
                SRL::Scene3D::PushMatrix();
                SRL::Scene3D::Translate(trackOffset);
                SRL::Types::Mesh mesh{};
                mesh.Vertices = seg1ComponentVerts_.data();
                mesh.VertexCount = seg1ComponentVerts_.size();
                mesh.Faces = seg1ComponentFaces_.data();
                mesh.FaceCount = seg1ComponentFaces_.size();
                mesh.Attributes = seg1ComponentAttrs_.data();
                SRL::Scene3D::DrawMesh(mesh);
                SRL::Scene3D::PopMatrix();

                if (!segment01Logged)
                {
                    segment01Logged = true;
                }

                TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult result{};
                result.rendered = true;
                result.meshes = 1;
                result.faces = static_cast<uint32_t>(seg1ComponentFaces_.size());
                return result;
            }

            auto* renderer = entry.renderer.get();
            if (!renderer)
            {
                return {};
            }
            // Guard draw path and retry one repair before issuing commands.
            if (!IsRendererStateIntegral(*renderer))
            {
                if (!TryRepairRendererState(*renderer))
                {
                    SRL::Debug::Print(1, 23, "TRK draw skip seg:%d", chunk.segmentId);
                    return {};
                }
            }

            renderer->SetOffset(trackOffset);
            if (chunk.segmentId > 0 && chunk.segmentId <= static_cast<int>(kTrackSegmentLimit))
            {
                if (renderedCountById[static_cast<size_t>(chunk.segmentId)] < 255)
                {
                    ++renderedCountById[static_cast<size_t>(chunk.segmentId)];
                }
            }
            renderer->Render(lightDirection, cameraLocation);

            if (!segment01Logged && chunk.segmentId == 1)
            {
                segment01Logged = true;
            }

            TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult result{};
            result.rendered = renderer->LastDrawnMeshes() > 0;
            result.meshes = renderer->LastDrawnMeshes();
            result.faces = renderer->LastDrawnFaces();
            return result;
        });
}

void TrackSystem::RenderFrame(bool renderTrack,
                              const Vector3D& trackOffset,
                              const Vector3D& lightDirection,
                              const Vector3D& cameraLocation,
                              const Vector3D& carWorldPosition)
{
    if (!renderTrack || !ready_)
    {
        return;
    }

    bool segment01Logged = false;
    bool segment01Prepared = false;
    const bool windowSlid = UpdateActiveSegmentWindowForPosition(carWorldPosition, trackOffset);
    if (!windowSlid) TryPrefetchUpcomingSegment();
    if (!windowSlid) PrewarmNextSegmentLod8();
    RunWorkRamMaintenance(windowSlid);

    std::vector<SegmentHandle> orderedHandles{};
    if (!kEnableTrackRuntimeStabilization)
    {
        orderedHandles = BuildVisibleSegmentOrder(trackOffset, cameraLocation);
    }
    if (!kEnableTrackRuntimeStabilization && orderedHandles.empty() && !segmentRenderers_.empty())
    {
        // Last safety net: keep rendering possible even after transient handle corruption.
        segmentHandles_ = BuildSegmentHandleTable();
        orderedHandles = BuildVisibleSegmentOrder(trackOffset, cameraLocation);
    }
    RefreshFamilyWorkingSet(false);
    RunSeg1DiagnosticsForFrame();
    std::array<uint8_t, kTrackSegmentLimit + 1> preparedCountById{};
    std::array<uint8_t, kTrackSegmentLimit + 1> renderedCountById{};
    RenderVisibleSegmentOrder(orderedHandles,
                              trackOffset,
                              lightDirection,
                              cameraLocation,
                              preparedCountById,
                              renderedCountById,
                              segment01Logged,
                              segment01Prepared);
    for (size_t id = 1; id <= kTrackSegmentLimit; ++id)
    {
        if (preparedCountById[id] > 1)
        {
            SRL::Debug::Print(1, 25, "WARN prep dup seg:%u count:%u", (unsigned)id, (unsigned)preparedCountById[id]);
        }
        if (renderedCountById[id] > 1)
        {
            SRL::Debug::Print(1, 24, "WARN rend dup seg:%u count:%u", (unsigned)id, (unsigned)renderedCountById[id]);
        }
    }

    (void)segment01Prepared;
}

void TrackSystem::EndFrame()
{
    coordinator_.PresentTelemetry();
    soakMonitor_.Update(ready_, coordinator_.Telemetry());
    soakMonitor_.Present();
    SRL::Debug::Print(1, 19, "RT rdr:%u sdr:%u rm:%u lod:%u",
                      static_cast<unsigned>(runtimeRdrBuildsThisFrame_),
                      static_cast<unsigned>(runtimeSdrBuildsThisFrame_),
                      static_cast<unsigned>(runtimeFaceRemapsThisFrame_),
                      static_cast<unsigned>(runtimeLodSegmentUpdatesThisFrame_));
    SRL::Debug::Print(1, 20, "RT sl:%u st:%u ph:%u pm:%u sr:%u sk:%u nd:%u ra:%u",
                      static_cast<unsigned>(runtimeSlidesThisFrame_),
                      static_cast<unsigned>(runtimeSlideStallsThisFrame_),
                      static_cast<unsigned>(runtimePrefetchHitsThisFrame_),
                      static_cast<unsigned>(runtimePrefetchMissesThisFrame_),
                      static_cast<unsigned>(runtimeSafeRenderedThisFrame_),
                      static_cast<unsigned>(runtimeSafeSkippedThisFrame_),
                      static_cast<unsigned>(runtimeSafeNoDrawThisFrame_),
                      static_cast<unsigned>(runtimeSafeReappliedThisFrame_));
    ReleaseUnusedFamilyResourcesEndFrame();
    EmitFamilyWorkingSetTelemetry();
    constexpr bool kEnablePerFrameDebugPrints = false;
    if (!kEnablePerFrameDebugPrints) return;
    // Work RAM monitor for runtime stability tuning.
    const auto hwr = SRL::Memory::HighWorkRam::GetReport();
    const auto lwr = SRL::Memory::LowWorkRam::GetReport();
    if (hwr.TotalSize == 0 || hwr.FreeSize > hwr.TotalSize)
    {
        SRL::Debug::Print(1, 30, "WR H CORRUPT free:%lu tot:%lu",
                          static_cast<unsigned long>(hwr.FreeSize),
                          static_cast<unsigned long>(hwr.TotalSize));
        return;
    }
    const unsigned long hwrUsed = static_cast<unsigned long>(hwr.TotalSize - hwr.FreeSize);
    const unsigned long hwrTotal = static_cast<unsigned long>(hwr.TotalSize);
    const unsigned long lwrUsed = static_cast<unsigned long>(lwr.TotalSize - lwr.FreeSize);
    const unsigned long lwrTotal = static_cast<unsigned long>(lwr.TotalSize);
    SRL::Debug::Print(1, 30, "WR H:%lu/%lu L:%lu/%lu", hwrUsed, hwrTotal, lwrUsed, lwrTotal);
    SRL::Debug::Print(1, 4, "TGA c:%u a:%u f:%u j:%u                    ",
                      (unsigned)seg1TgaPreloadCount_,
                      (unsigned)seg1TgaAttemptCount_,
                      (unsigned)seg1TgaFailCount_,
                      (unsigned)seg1TgaJsonOk_);
    SRL::Debug::Print(1, 24, "SMAP b:%u sig:%s                         ", (unsigned)g_smapBytes, g_smapSig);
    SRL::Debug::Print(1, 25, "TGA last name:%s                         ", g_tgaLastName);
    SRL::Debug::Print(1, 26, "TGA last try:%s                          ", g_tgaLastTry);
    SRL::Debug::Print(1, 27, "TGA last res:%s                          ", g_tgaLastResult);
    if (!seg1FamilySlots_.empty())
    {
        const Seg1FamilySlotEntry* fam1 = nullptr;
        for (const auto& e : seg1FamilySlots_)
        {
            if (e.familyId == 1)
            {
                fam1 = &e;
                break;
            }
        }
        if (fam1)
        {
            SRL::Debug::Print(1, 28, "F1 s8:%u s16:%u s32:%u s64:%u           ",
                              (unsigned)fam1->lodSlots[0],
                              (unsigned)fam1->lodSlots[1],
                              (unsigned)fam1->lodSlots[2],
                              (unsigned)fam1->lodSlots[3]);
            auto texDim = [&](uint16_t slot, char* out, size_t outSize)
            {
                if (!out || outSize == 0) return;
                out[0] = '\0';
                if (slot == No_Texture || slot >= SRL_MAX_TEXTURES || SRL::VDP1::Metadata[slot].Texture == nullptr)
                {
                    std::snprintf(out, outSize, "--");
                    return;
                }
                auto* t = SRL::VDP1::Metadata[slot].Texture;
                std::snprintf(out, outSize, "%ux%u", (unsigned)t->Width, (unsigned)t->Height);
            };
            char d8[12]{}, d16[12]{}, d32[12]{}, d64[12]{};
            texDim(fam1->lodSlots[0], d8, sizeof(d8));
            texDim(fam1->lodSlots[1], d16, sizeof(d16));
            texDim(fam1->lodSlots[2], d32, sizeof(d32));
            texDim(fam1->lodSlots[3], d64, sizeof(d64));
            SRL::Debug::Print(1, 29, "F1 d8:%s d16:%s d32:%s d64:%s         ", d8, d16, d32, d64);
        }
        else
        {
            SRL::Debug::Print(1, 28, "F1 slots:none                           ");
            SRL::Debug::Print(1, 29, "F1 dims:none                            ");
        }
    }
    else
    {
        SRL::Debug::Print(1, 28, "F1 slots:empty                          ");
        SRL::Debug::Print(1, 29, "F1 dims:empty                           ");
    }

    // Stability mode: keep a fixed budget to avoid frame-to-frame visibility oscillation.
    const bool enableAdaptiveBudget = false;
    if (enableAdaptiveBudget)
    {
        const FrameBudget nextBudget = budgetController_.Update(coordinator_.Budget(), coordinator_.Telemetry());
        coordinator_.SetBudget(nextBudget);
    }
}

bool TrackSystem::FindNearestSegment(const Vector3D& worldPosition,
                                     const Vector3D& trackOffset,
                                     int32_t& outSegmentId,
                                     Vector3D& outSegmentCenter) const
{
    if (!segmentCenterCatalog_.empty())
    {
        bool hasCandidate = false;
        SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        for (size_t i = 0; i < segmentCenterCatalog_.size(); ++i)
        {
            const Vector3D center = segmentCenterCatalog_[i] + trackOffset;
            const SRL::Math::Types::Fxp dx = (center.X - worldPosition.X).Abs();
            const SRL::Math::Types::Fxp dz = (center.Z - worldPosition.Z).Abs();
            const SRL::Math::Types::Fxp score = dx + dz;
            if (!hasCandidate || score < bestScore)
            {
                hasCandidate = true;
                bestScore = score;
                outSegmentId = static_cast<int32_t>(i + 1);
                outSegmentCenter = center;
            }
        }
        if (!hasCandidate)
        {
            outSegmentId = -1;
            outSegmentCenter = Vector3D(0.0, 0.0, 0.0);
        }
        return hasCandidate;
    }

    if (segmentRenderers_.empty())
    {
        outSegmentId = -1;
        outSegmentCenter = Vector3D(0.0, 0.0, 0.0);
        return false;
    }

    bool hasCandidate = false;
    SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
    for (const auto& segment : segmentRenderers_)
    {
        const Vector3D center = segment.center + trackOffset;
        const SRL::Math::Types::Fxp dx = (center.X - worldPosition.X).Abs();
        const SRL::Math::Types::Fxp dz = (center.Z - worldPosition.Z).Abs();
        const SRL::Math::Types::Fxp score = dx + dz;
        if (!hasCandidate || score < bestScore)
        {
            hasCandidate = true;
            bestScore = score;
            outSegmentId = segment.id;
            outSegmentCenter = center;
        }
    }
    if (!hasCandidate)
    {
        outSegmentId = -1;
        outSegmentCenter = Vector3D(0.0, 0.0, 0.0);
    }
    return hasCandidate;
}

bool TrackSystem::FindSegmentCenterById(const int32_t segmentId,
                                        const Vector3D& trackOffset,
                                        Vector3D& outSegmentCenter) const
{
    if (segmentId > 0 &&
        static_cast<size_t>(segmentId) <= segmentCenterCatalog_.size())
    {
        outSegmentCenter = segmentCenterCatalog_[static_cast<size_t>(segmentId - 1)] + trackOffset;
        return true;
    }

    for (const auto& segment : segmentRenderers_)
    {
        if (segment.id != segmentId)
        {
            continue;
        }

        outSegmentCenter = segment.center + trackOffset;
        return true;
    }

    outSegmentCenter = Vector3D(0.0, 0.0, 0.0);
    return false;
}

bool TrackSystem::HasSmoothSegments() const
{
    for (const auto& segment : segmentRenderers_)
    {
        if (segment.renderer && segment.renderer->IsSmooth())
        {
            return true;
        }
    }
    return false;
}

uint32_t TrackSystem::MaxSegmentFaceCount() const
{
    uint32_t maxFaces = 0;
    for (const auto& segment : segmentRenderers_)
    {
        if (!segment.renderer) continue;
        maxFaces = std::max(maxFaces, segment.renderer->FaceCount());
    }
    return maxFaces;
}

uint32_t TrackSystem::MaxSegmentVertexCount() const
{
    uint32_t maxVertices = 0;
    for (const auto& segment : segmentRenderers_)
    {
        if (!segment.renderer) continue;
        maxVertices = std::max(maxVertices, segment.renderer->VertexCount());
    }
    return maxVertices;
}
