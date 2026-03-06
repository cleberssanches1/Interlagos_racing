#include "track_system.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <string.h>
#include <vector>
#include <errno.h>

#include "modelObject.hpp"
#include "resource_loader.hpp"
#include "segment_component_loader.hpp"
#include "segment_draw_ready_loader.hpp"
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
    std::vector<int> faceFamily{};
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
    std::vector<RenTextureMapEntry> entries{};
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
    std::vector<PackedAssetEntryMeta> entries{};
};

struct BdrBatchBuildResult
{
    std::unique_ptr<TrackRenderer> renderer{};
    Vector3D center{};
    std::vector<uint16_t> familyIds{};
    std::vector<uint8_t> faceRankOffsets{};
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

static void NormalizeTextureFileName(const char* in, char* out, size_t outSize);
static bool LoadPackedAssetIndexToCart(const char* const* candidates, size_t count, PackedAssetCache& cache);
static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentComponent::Blob& out);
static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentDrawReady::Blob& out);
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

static size_t BuildUniqueUsedFamilies(const std::vector<int>& faceFamily, int* outIds, size_t outCap)
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
    return SRL::VDP1::TryLoadTexture(tex.width, tex.height, tex.mode, paletteId, const_cast<uint8_t*>(tex.pixels.data()));
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
    char packedName[16]{};
    std::snprintf(packedName, sizeof(packedName), "S%03d.SDR", segmentId);
    const char* packCandidates[] = {
        "CD/DATA/SDR.BIN",
        "CD/DATA/SDR.BIN;1",
        "DATA/SDR.BIN",
        "DATA/SDR.BIN;1",
        "SDR.BIN",
        "SDR.BIN;1"
    };
    if (LoadPackedAssetIndexToCart(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]), sSdrPackCache) &&
        LoadPackedAssetEntryToBlob(sSdrPackCache, packedName, outBlob))
    {
        return SegmentDrawReady::Loader::Parse(outBlob, outView);
    }
    (void)segmentId;
    outBlob = {};
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
    const size_t kWorkRamHeadroomBytes = 96 * 1024;
    const size_t hwrFree = SRL::Memory::HighWorkRam::GetFreeSpace();
    const size_t usable = (hwrFree > kWorkRamHeadroomBytes) ? (hwrFree - kWorkRamHeadroomBytes) : 0;

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
    const size_t overhead = 8u * 1024u;
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
    outFreeBytes = SRL::Memory::HighWorkRam::GetFreeSpace();
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

    if (outFreeBytes <= reserveBytes) return false;
    const size_t usable = outFreeBytes - reserveBytes;
    return outEstimatedBytes <= usable;
}

static bool LoadSdrFamilyIdsForSegment(int segmentId, std::vector<uint16_t>& outFamilyIds)
{
    outFamilyIds.clear();

    SegmentDrawReady::Blob sdrBlob{};
    SegmentDrawReady::Loader::View sdrView{};
    if (!LoadSdrForSegment(segmentId, sdrBlob, sdrView))
    {
        return false;
    }

    if (sdrView.header.faceCount == 0) return false;
    outFamilyIds.assign(static_cast<size_t>(sdrView.header.faceCount), 0);
    for (uint32_t fi = 0; fi < sdrView.header.faceCount; ++fi)
    {
        uint16_t familyId = 0;
        const size_t off = sdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
        if (!SegmentDrawReady::Loader::ReadFamilyIdLeAt(sdrBlob.bytes, off, familyId)) return false;
        outFamilyIds[static_cast<size_t>(fi)] = familyId;
    }

    return true;
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

    std::vector<int32_t> remap(rendererFaces, -1);
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

static bool BuildRendererFromSdr(int segmentId, TrackRenderer& renderer, Vector3D* outCenter)
{
    SegmentDrawReady::Blob sdrBlob{};
    SegmentDrawReady::Loader::View sdrView{};
    if (!LoadSdrForSegment(segmentId, sdrBlob, sdrView))
    {
        return false;
    }

    std::vector<SRL::Math::Types::Vector3D> verts{};
    std::vector<SRL::Types::Polygon> faces{};
    std::vector<SRL::Types::Attribute> attrs{};
    verts.reserve(sdrView.header.vertexCount);
    faces.reserve(sdrView.header.faceCount);
    attrs.reserve(sdrView.header.faceCount);

    for (uint32_t vi = 0; vi < sdrView.header.vertexCount; ++vi)
    {
        SegmentDrawReady::Vertex sv{};
        const size_t off = sdrView.verticesOffset + static_cast<size_t>(vi) * sizeof(SegmentDrawReady::Vertex);
        if (!SegmentDrawReady::Loader::ReadVertexLeAt(sdrBlob.bytes, off, sv)) return false;

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
        if (!SegmentDrawReady::Loader::ReadFaceLeAt(sdrBlob.bytes, foff, sf)) return false;
        if (!SegmentDrawReady::Loader::ReadAttrBaseLeAt(sdrBlob.bytes, aoff, sa)) return false;

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

    const bool ok = renderer.InitializeFromComponentData(verts, faces, attrs);
    if (ok && outCenter)
    {
        *outCenter = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sdrView.header.centerX),
            SRL::Math::Types::Fxp::BuildRaw(sdrView.header.centerY),
            SRL::Math::Types::Fxp::BuildRaw(sdrView.header.centerZ));
    }
    return ok;
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
    std::vector<uint16_t> familyIds{};
    std::vector<uint8_t> faceRankOffsets{};

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
    if (!renderer->InitializeFromComponentData(verts, faces, attrs))
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
                                    std::vector<uint16_t>& ioFamilyIds,
                                    std::vector<uint8_t>& ioFaceRankOffsets,
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
    if (BuildRendererFromSdr(segmentId, renderer, outCenter))
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
static bool LoadPackedAssetIndexToCart(const char* const* candidates, size_t count, PackedAssetCache& cache)
{
    if (cache.cartPtr && cache.size > 0 && !cache.entries.empty()) return true;

    cache = {};
    const char* foundPath = nullptr;
    for (size_t i = 0; i < count; ++i)
    {
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

    const int32_t read = f.Read(static_cast<int32_t>(bytes), mem);
    if (read <= 0 || static_cast<uint32_t>(read) > bytes)
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    cache.cartPtr = mem;
    cache.size = static_cast<uint32_t>(read);

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

    cache.entries.clear();
    cache.entries.reserve(entryCount);
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
        cache.entries.push_back(e);
    }

    return !cache.entries.empty();
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentComponent::Blob& out)
{
    out = {};
    if (!cache.cartPtr || cache.size == 0 || cache.entries.empty() || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        std::vector<uint8_t> tmp(e.size);
        ::memcpy(tmp.data(), src, e.size);
        out.bytes = std::move(tmp);
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    return false;
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentDrawReady::Blob& out)
{
    out = {};
    if (!cache.cartPtr || cache.size == 0 || cache.entries.empty() || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        std::vector<uint8_t> tmp(e.size);
        ::memcpy(tmp.data(), src, e.size);
        out.bytes = std::move(tmp);
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    return false;
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, BatchDrawReady::Blob& out)
{
    out = {};
    if (!cache.cartPtr || cache.size == 0 || cache.entries.empty() || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        std::vector<uint8_t> tmp(e.size);
        ::memcpy(tmp.data(), src, e.size);
        out.bytes = std::move(tmp);
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

bool TrackSystem::LoadSeg1TexbankIndexToCart(size_t lodIndex, int lodValue)
{
    if (lodIndex >= seg1Texbanks_.size()) return false;
    auto& bank = seg1Texbanks_[lodIndex];
    if (bank.cartPtr && bank.size > 0 && !bank.entries.empty()) return true;

    bank = {};
    bank.lod = lodValue;

    char n0[40]{}, n1[40]{}, n2[28]{}, n3[28]{}, n4[24]{}, n5[24]{}, n6[24]{}, n7[24]{}, n8[24]{}, n9[24]{}, n10[24]{}, n11[24]{};
    std::snprintf(n0, sizeof(n0), "CD/DATA/TEXBANK_%d.BIN", bank.lod);
    std::snprintf(n1, sizeof(n1), "CD/DATA/TEXBANK_%d.BIN;1", bank.lod);
    std::snprintf(n2, sizeof(n2), "DATA/TEXBANK_%d.BIN", bank.lod);
    std::snprintf(n3, sizeof(n3), "DATA/TEXBANK_%d.BIN;1", bank.lod);
    std::snprintf(n4, sizeof(n4), "TEXBANK_%d.BIN", bank.lod);
    std::snprintf(n5, sizeof(n5), "TEXBANK_%d.BIN;1", bank.lod);
    std::snprintf(n6, sizeof(n6), "texbank_%d.bin", bank.lod);
    std::snprintf(n7, sizeof(n7), "texbank_%d.bin;1", bank.lod);
    std::snprintf(n8, sizeof(n8), "CD/DATA/TBK%d.BIN", bank.lod);
    std::snprintf(n9, sizeof(n9), "CD/DATA/TBK%d.BIN;1", bank.lod);
    std::snprintf(n10, sizeof(n10), "TBK%d.BIN", bank.lod);
    std::snprintf(n11, sizeof(n11), "TBK%d.BIN;1", bank.lod);
    const char* cands[] = { n0, n1, n2, n3, n4, n5, n6, n7, n8, n9, n10, n11 };

    const char* foundPath = nullptr;
    bool found = false;
    for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]); ++i)
    {
        SRL::Cd::File probe(cands[i]);
        if (probe.Exists() && probe.Size.Bytes > 0)
        {
            foundPath = cands[i];
            found = true;
            break;
        }
    }
    if (!found || !foundPath) return false;
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
    bank.cartPtr = mem;
    bank.size = static_cast<uint32_t>(read);

    const uint8_t* p = static_cast<const uint8_t*>(bank.cartPtr);
    if (bank.size < 20) return false;
    const uint32_t magic = ReadLe32(p + 0);
    const uint16_t ver = ReadLe16(p + 4);
    const uint16_t lod = ReadLe16(p + 6);
    const uint32_t count = ReadLe32(p + 8);
    const uint32_t dataOff = ReadLe32(p + 12);
    (void)ver;
    if (magic != 0x314B4254 || lod != static_cast<uint16_t>(bank.lod)) return false;
    const uint32_t entryBase = 20;
    const uint32_t entrySize = 16;
    if (entryBase + count * entrySize > bank.size) return false;
    if (dataOff > bank.size) return false;

    bank.entries.clear();
    bank.entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t o = entryBase + i * entrySize;
        Seg1TexbankEntry e{};
        e.familyId = static_cast<uint16_t>(ReadLe32(p + o + 0));
        e.offset = ReadLe32(p + o + 4);
        e.size = ReadLe32(p + o + 8);
        if (e.offset + e.size <= bank.size)
        {
            bank.entries.push_back(e);
        }
    }
    return !bank.entries.empty();
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

std::vector<TrackSystem::TrackSegmentEntry> TrackSystem::CopyAllTrackSegments(size_t maxSegments)
{
    std::vector<TrackSegmentEntry> segments;
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
        segments.push_back({ static_cast<int>(i), copy });
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

std::vector<TrackSystem::SegmentRenderEntry> TrackSystem::BuildSegmentRenderers(std::vector<TrackSegmentEntry>& entries)
{
    std::vector<SegmentRenderEntry> renderers;
    if (entries.empty()) return renderers;
    // Keep the new direct VDP1 command path disabled until the draw path is validated.
    constexpr bool kUseTrackVdp1Commands = false;
    renderers.reserve(1);

    // Single-segment package: use the direct SDR loader path.
    // This mirrors the robust contract of the NYA flow (one prepared mesh per draw)
    // and avoids extra merge/remap steps.
    if (entries.size() == 1)
    {
        const int segmentId = entries[0].id;
        auto renderer = std::make_unique<TrackRenderer>();
        Vector3D center(0.0, 0.0, 0.0);
        if (!BuildRendererFromSdr(segmentId, *renderer, &center))
        {
            SRL::Debug::Print(1, 15, "SDR init fail %03d", segmentId);
            return {};
        }

        std::vector<uint16_t> familyIds{};
        if (!LoadSdrFamilyIdsForSegment(segmentId, familyIds) || familyIds.empty())
        {
            SRL::Debug::Print(1, 15, "SDR fam fail %03d", segmentId);
            return {};
        }

        renderer->SetUseOriginal(false);
        renderer->SetSglDirect(false);
        renderer->SetVdp1Commands(kUseTrackVdp1Commands);
        renderer->SetDirect2D(false);
        renderer->SetForceDoubleSided(false);
        renderer->SetScale(SRL::Math::Types::Fxp::BuildRaw(1 << 16));
        renderer->SetDrawLimit(renderer->MeshCount());

        SegmentRenderEntry item{};
        item.id = segmentId;
        item.logicalSegmentCount = 1;
        item.center = center;
        item.renderer = std::move(renderer);
        item.lodState.ready = true;
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
    std::vector<uint16_t> batchFamilyIds{};
    std::vector<uint8_t> batchFaceRankOffsets{};
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
    if (!renderer->InitializeFromComponentData(batchVerts, batchFaces, batchAttrs))
    {
        SRL::Debug::Print(1, 15, "SDR pkg init fail %03d", entries.front().id);
        return {};
    }
    renderer->SetUseOriginal(false);
    renderer->SetSglDirect(false);
    renderer->SetVdp1Commands(kUseTrackVdp1Commands);
    renderer->SetDirect2D(false);
    renderer->SetForceDoubleSided(false);
    renderer->SetScale(SRL::Math::Types::Fxp::BuildRaw(1 << 16));
    renderer->SetDrawLimit(renderer->MeshCount());

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
    item.lodState.currentFaceSlots.assign(item.lodState.faceFamilyIds.size(), -1);
    renderers.push_back(std::move(item));
    return renderers;
}

std::vector<TrackSystem::SegmentHandle> TrackSystem::BuildSegmentHandleTable()
{
    segmentPool_.Reset();
    std::vector<SegmentHandle> handles;
    handles.reserve(segmentRenderers_.size());
    for (auto& entry : segmentRenderers_)
    {
        handles.push_back(segmentPool_.Add(&entry));
    }
    return handles;
}

// Build shared family ids used by the track renderer set. Texture slots are loaded lazily.
bool TrackSystem::BuildTrackFamilyLodSlots(std::vector<Seg1FamilySlotEntry>& outSlots)
{
    outSlots.clear();
    std::vector<uint16_t> familyIdsUsed{};
    familyIdsUsed.reserve(256);

    for (const auto& seg : segmentRenderers_)
    {
        if (!seg.renderer) continue;
        if (!seg.lodState.ready) continue;
        for (size_t fi = 0; fi < seg.lodState.faceFamilyIds.size(); ++fi)
        {
            const uint16_t fam = seg.lodState.faceFamilyIds[fi];
            if (fam == 0) continue;
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

    outSlots.reserve(familyIdsUsed.size());
    for (size_t i = 0; i < familyIdsUsed.size(); ++i)
    {
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = familyIdsUsed[i];
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };

        outSlots.push_back(slotEntry);
    }

    return !outSlots.empty();
}

// Build the per face family table for one segment renderer from SDR1.
bool TrackSystem::BuildSegmentLodState(SegmentRenderEntry& entry,
                                       const SegmentComponent::Blob& matBlob,
                                       const SegmentComponent::Loader::MatView& matView,
                                       std::vector<Seg1FamilySlotEntry>& familySlots)
{
    if (!entry.renderer) return false;
    if (!entry.lodState.ready) return false;
    if (entry.lodState.faceFamilyIds.empty()) return false;
    entry.lodState.currentFaceSlots.assign(entry.lodState.faceFamilyIds.size(), -1);

    (void)matBlob;
    (void)matView;

    entry.lodState.currentLodIndex = 0xFF;
    entry.lodState.currentBaseRank = -1;
    return RebuildSegmentFaceSlotsForBaseRank(entry, 0, familySlots);
}

// Upload one family texture slot only when a lod band actually needs it.
bool TrackSystem::EnsureFamilyLodSlotLoaded(std::vector<Seg1FamilySlotEntry>& familySlots,
                                            uint16_t familyId,
                                            uint8_t lodIndex)
{
    if (lodIndex > 3) return false;

    Seg1FamilySlotEntry* slotEntry = nullptr;
    for (size_t i = 0; i < familySlots.size(); ++i)
    {
        if (familySlots[i].familyId != familyId) continue;
        slotEntry = &familySlots[i];
        break;
    }
    if (!slotEntry) return false;
    if (slotEntry->lodSlots[lodIndex] != No_Texture) return true;

    const int lodValues[4] = { 8, 16, 32, 64 };
    // Fallback policy:
    // 1) requested lod
    // 2) smaller lods (cheaper)
    // 3) larger lods (prevent holes when only high lod exists)
    uint8_t searchOrder[4]{};
    size_t searchCount = 0;
    searchOrder[searchCount++] = lodIndex;
    for (int li = static_cast<int>(lodIndex) - 1; li >= 0; --li)
    {
        searchOrder[searchCount++] = static_cast<uint8_t>(li);
    }
    for (uint8_t li = static_cast<uint8_t>(lodIndex + 1); li < 4; ++li)
    {
        searchOrder[searchCount++] = li;
    }

    for (size_t si = 0; si < searchCount; ++si)
    {
        const int fallbackLi = static_cast<int>(searchOrder[si]);
        if (!LoadSeg1TexbankIndexToCart(static_cast<size_t>(fallbackLi), lodValues[fallbackLi])) continue;
        const auto& bank = seg1Texbanks_[static_cast<size_t>(fallbackLi)];
        const uint8_t* bankBytes = static_cast<const uint8_t*>(bank.cartPtr);
        if (!bankBytes) continue;

        const Seg1TexbankEntry* bankEntry = nullptr;
        for (const auto& e : bank.entries)
        {
            if (e.familyId != familyId) continue;
            bankEntry = &e;
            break;
        }
        if (!bankEntry) continue;

        DecodedTgaTexture decoded{};
        if (!DecodePalettedTgaMemory(bankBytes + bankEntry->offset, bankEntry->size, decoded)) continue;

        const int32_t slot = UploadDecodedTextureToVdp1(decoded);
        if (slot < 0) continue;

        slotEntry->lodSlots[lodIndex] = static_cast<uint16_t>(slot);
        return true;
    }

    return false;
}

// Rebuild one segment face slot table on demand for the selected lod band.
bool TrackSystem::RebuildSegmentFaceSlotsForLod(SegmentRenderEntry& entry,
                                                uint8_t lodIndex,
                                                std::vector<Seg1FamilySlotEntry>& familySlots)
{
    if (!entry.renderer) return false;
    if (lodIndex > 3) return false;
    if (entry.lodState.faceFamilyIds.empty()) return false;

    const size_t faceCount = entry.lodState.faceFamilyIds.size();
    entry.lodState.currentFaceSlots.assign(faceCount, -1);

    for (size_t fi = 0; fi < faceCount; ++fi)
    {
        const uint16_t fam = entry.lodState.faceFamilyIds[fi];
        if (fam == 0) continue;

        (void)EnsureFamilyLodSlotLoaded(familySlots, fam, lodIndex);

        uint16_t slot = No_Texture;
        for (size_t si = 0; si < familySlots.size(); ++si)
        {
            if (familySlots[si].familyId != fam) continue;
            slot = familySlots[si].lodSlots[lodIndex];
            break;
        }

        if (slot != No_Texture)
        {
            entry.lodState.currentFaceSlots[fi] = static_cast<int32_t>(slot);
        }
    }

    return true;
}

bool TrackSystem::RebuildSegmentFaceSlotsForBaseRank(SegmentRenderEntry& entry,
                                                     size_t baseRank,
                                                     std::vector<Seg1FamilySlotEntry>& familySlots)
{
    if (!entry.renderer) return false;
    if (entry.lodState.faceFamilyIds.empty()) return false;

    const size_t faceCount = entry.lodState.faceFamilyIds.size();
    entry.lodState.currentFaceSlots.assign(faceCount, -1);

    const bool hasRankOffsets = entry.lodState.faceRankOffsets.size() == faceCount;
    for (size_t fi = 0; fi < faceCount; ++fi)
    {
        const uint16_t fam = entry.lodState.faceFamilyIds[fi];
        if (fam == 0) continue;

        const size_t rank = baseRank + (hasRankOffsets ? static_cast<size_t>(entry.lodState.faceRankOffsets[fi]) : 0u);
        const uint8_t lodIndex = ResolveSegmentLodIndexByRank(rank);
        (void)EnsureFamilyLodSlotLoaded(familySlots, fam, lodIndex);

        uint16_t slot = No_Texture;
        for (size_t si = 0; si < familySlots.size(); ++si)
        {
            if (familySlots[si].familyId != fam) continue;
            slot = familySlots[si].lodSlots[lodIndex];
            break;
        }

        if (slot != No_Texture)
        {
            entry.lodState.currentFaceSlots[fi] = static_cast<int32_t>(slot);
        }
    }

    return true;
}

// Map a near to far rank into the current fixed lod bands for track rendering.
uint8_t TrackSystem::ResolveSegmentLodIndexByRank(size_t rank) const
{
    if (rank < 3) return 3;    // 64x64 (3)
    if (rank < 8) return 2;    // 32x32 (5)
    if (rank < 13) return 1;   // 16x16 (5)
    return 0;                  // 8x8  (7)
}

// Apply lod changes only when a visible segment crosses a band boundary.
void TrackSystem::UpdateVisibleSegmentLods(const std::vector<SegmentHandle>& nearToFarHandles)
{
    std::vector<Seg1FamilySlotEntry>& familySlots = seg1FamilySlots_;
    size_t logicalRank = 0;
    for (size_t rank = 0; rank < nearToFarHandles.size(); ++rank)
    {
        auto* entry = segmentPool_.Resolve(nearToFarHandles[rank]);
        if (!entry || !entry->renderer) continue;
        if (!entry->lodState.ready) continue;

        const int16_t desiredBaseRank = static_cast<int16_t>(logicalRank);
        if (entry->lodState.currentBaseRank == desiredBaseRank)
        {
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }

        if (!RebuildSegmentFaceSlotsForBaseRank(*entry, logicalRank, familySlots))
        {
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }
        (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
        entry->lodState.currentBaseRank = desiredBaseRank;
        entry->lodState.currentLodIndex = 0xFE;
        logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
    }
}

bool TrackSystem::Initialize(const Config& config)
{
    ready_ = false;
    segmentsReady_ = false;
    coordinatorReady_ = false;
    fixedVisibleSegmentCap_ = 1;
    seg1ComponentEnabled_ = false;
    seg1ComponentVerts_.clear();
    seg1ComponentFaces_.clear();
    seg1ComponentAttrs_.clear();
    seg1FaceFamilyIds_.clear();
    seg1FamilySlots_.clear();
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
    soakMonitor_.Reset();

    // Test mode: keep the loaded catalog aligned with the configured visible segment budget
    // so we can isolate experiments on SEG_001 only. The full-catalog path stays available
    // behind this switch for future broader texture-mapping validation.
    constexpr bool kBuildFullCatalogForTextureMapping = false;
    const size_t loadLimit =
        kBuildFullCatalogForTextureMapping
            ? kTrackSegmentLimit
            : ((config.initialSegments == 0)
                   ? kTrackSegmentLimit
                   : std::min<size_t>(config.initialSegments, kTrackSegmentLimit));
    fixedVisibleSegmentCap_ = std::max<uint32_t>(1u, static_cast<uint32_t>(loadLimit));
    segmentEntries_.reserve(loadLimit);
    segmentRenderers_.reserve(loadLimit);

    // Build runtime draw packages with contiguous SDR segments.
    // Requested package size is capped by a Work RAM safety guard.
    // For stability, keep packages small enough to avoid large merged batches.
    constexpr size_t kRequestedSegmentsPerDrawPackage = 1;
    const size_t segmentsPerDrawPackage = ComputeSafeSegmentsPerPackage(kRequestedSegmentsPerDrawPackage);
    size_t builtCount = 0;
    constexpr size_t kHighWorkRamReserveBytes = 96u * 1024u;
    for (size_t sid = 1; sid <= loadLimit; )
    {
        const size_t maxCount = std::min(segmentsPerDrawPackage, (loadLimit - sid + 1));
        size_t chosenCount = maxCount;
        size_t chosenEnd = sid;
        size_t batchEstimatedBytes = 0;
        size_t hwrFreeBytes = 0;
        bool admitted = false;

        while (chosenCount > 0)
        {
            const size_t testEnd = sid + chosenCount - 1;
            if (CanAdmitSdrBatchInHighWorkRam(sid, testEnd, kHighWorkRamReserveBytes, batchEstimatedBytes, hwrFreeBytes))
            {
                admitted = true;
                chosenEnd = testEnd;
                break;
            }
            --chosenCount;
        }

        if (!admitted)
        {
            SRL::Debug::Print(1, 11, "PKG stop HWR sid:%u need:%u free:%u",
                              static_cast<unsigned>(sid),
                              static_cast<unsigned>(batchEstimatedBytes),
                              static_cast<unsigned>(hwrFreeBytes));
            break;
        }

        std::vector<TrackSegmentEntry> batchEntries{};
        batchEntries.reserve(chosenCount);
        for (size_t id = sid; id <= chosenEnd; ++id)
        {
            batchEntries.push_back({ static_cast<int>(id), {} });
            segmentEntries_.push_back({ static_cast<int>(id), {} });
        }

        auto built = BuildSegmentRenderers(batchEntries);
        if (!built.empty())
        {
            SRL::Debug::Print(1, 11, "PKG add id:%u..%u", (unsigned)sid, (unsigned)chosenEnd);
            segmentRenderers_.push_back(std::move(built[0]));
            ++builtCount;
        }
        sid = chosenEnd + 1;
    }
    SRL::Debug::Print(1, 13, "Track pkg built %u segs:%u", unsigned(builtCount), unsigned(segmentEntries_.size()));
    SRL::Debug::Print(1, 28, "DBG build tag:TS29A segs:%lu", (unsigned long)segmentEntries_.size());
    segmentsReady_ = !segmentRenderers_.empty();
    segmentHandles_ = BuildSegmentHandleTable();

    (void)config.useSlave; // stability mode: always use synchronous/double-buffer producer

    // Safety guard: cap draws by configured visible segments.
    const uint32_t kSafeTrackDrawsPerFrame =
        std::min<uint32_t>(
            std::max<uint32_t>(1u, config.initialSegments),
            static_cast<uint32_t>(kTrackSegmentLimit));
    TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::Config coordinatorConfig{};
    coordinatorConfig.budget.maxTrackSegments =
        std::min<uint32_t>(
            std::min<uint32_t>(config.initialSegments, static_cast<uint32_t>(kTrackSegmentLimit)),
            kSafeTrackDrawsPerFrame);
    coordinatorConfig.budget.maxTrackMeshes = config.initialMeshes;
    coordinatorConfig.budget.maxTrackFaces = config.initialFaces;
    coordinatorConfig.chunkCapacity = kTrackSegmentLimit;

    coordinatorReady_ = coordinator_.Initialize(coordinatorConfig);
    if (!coordinatorReady_)
    {
        SRL::Debug::Print(1, 31, "TrackRenderCoordinator HWR alloc failed");
    }

    AdaptiveTrackBudgetController::Limits adaptiveBudgetLimits{};
    const uint32_t minSegmentsRequested =
        std::min<uint32_t>(std::max<uint32_t>(1, config.minSegments), kSafeTrackDrawsPerFrame);
    const uint32_t maxSegmentsRequested =
        std::min<uint32_t>(std::max<uint32_t>(minSegmentsRequested, config.initialSegments), kSafeTrackDrawsPerFrame);
    const uint32_t maxSegmentsCap = static_cast<uint32_t>(kTrackSegmentLimit);
    adaptiveBudgetLimits.minSegments = std::min<uint32_t>(minSegmentsRequested, maxSegmentsCap);
    adaptiveBudgetLimits.maxSegments = std::min<uint32_t>(maxSegmentsRequested, maxSegmentsCap);
    // Lock mesh/face budget to configured startup values to avoid runtime shrink.
    adaptiveBudgetLimits.minMeshes = std::max<uint32_t>(1u, config.initialMeshes);
    adaptiveBudgetLimits.maxMeshes = adaptiveBudgetLimits.minMeshes;
    adaptiveBudgetLimits.minFaces = std::max<uint32_t>(1000u, config.initialFaces);
    adaptiveBudgetLimits.maxFaces = adaptiveBudgetLimits.minFaces;
    budgetController_ = AdaptiveTrackBudgetController(adaptiveBudgetLimits);

    if (!segmentsReady_)
    {
        SRL::Debug::Print(1, 28, "Track rendering skipped: segments missing");
        if (lastSegmentPath_[0] != '\0')
        {
            SRL::Debug::Print(1, 29, "Last segment path tested: %s", lastSegmentPath_);
        }
    }

    if (!segmentRenderers_.empty())
    {
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

    // Build the initial per batch face slots from SDR family ids.
    {
        constexpr bool kEnableMat8FixedIntegration = true;
        if (kEnableMat8FixedIntegration)
        {
            unsigned matOk = 0;
            unsigned matFail = 0;
            std::vector<Seg1FamilySlotEntry> familyLodSlots{};
            if (!BuildTrackFamilyLodSlots(familyLodSlots))
            {
                SRL::Debug::Print(1, 19, "SDR lod build fail");
            }
            else
            {
                seg1FamilySlots_ = familyLodSlots;
                size_t logicalRank = 0;
                for (size_t i = 0; i < segmentRenderers_.size(); ++i)
                {
                    auto& seg = segmentRenderers_[i];
                    if (!seg.renderer) { ++matFail; continue; }

                    SegmentComponent::Blob unusedMatBlob{};
                    SegmentComponent::Loader::MatView unusedMatView{};
                    if (!BuildSegmentLodState(seg, unusedMatBlob, unusedMatView, familyLodSlots))
                    {
                        ++matFail;
                        continue;
                    }

                    if (!RebuildSegmentFaceSlotsForBaseRank(seg, logicalRank, familyLodSlots))
                    {
                        ++matFail;
                        logicalRank += std::max<size_t>(1, static_cast<size_t>(seg.logicalSegmentCount));
                        continue;
                    }
                    (void)seg.renderer->ApplyFaceTextureSlotsGlobal(seg.lodState.currentFaceSlots);
                    seg.lodState.currentBaseRank = static_cast<int16_t>(logicalRank);
                    seg.lodState.currentLodIndex = 0xFE;
                    logicalRank += std::max<size_t>(1, static_cast<size_t>(seg.logicalSegmentCount));
                    ++matOk;
                }

                SRL::Debug::Print(1, 20, "SDR ok:%u fail:%u fam:%u", matOk, matFail, (unsigned)familyLodSlots.size());
            }
        }
    }

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
                    std::vector<int32_t> probe{};
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
                    int32_t chosenFace = -1;
                    int32_t chosenBase = -1;
                    for (size_t i = 0; i < seg1SingleFaceSlots_.size(); ++i)
                    {
                        if (seg1SingleFaceSlots_[i] > 0)
                        {
                            chosenFace = static_cast<int32_t>(i);
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

                seg1FamilySlots_.clear();
                seg1FamilySlots_.reserve(familyIdsUsedCount);
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
                for (size_t u = 0; u < familyIdsUsedCount; ++u)
                {
                    Seg1FamilySlotEntry slotEntry{};
                    slotEntry.familyId = static_cast<uint16_t>(familyIdsUsed[u]);
                    slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
                    seg1FamilySlots_.push_back(slotEntry);
                }

                auto loadTexbankToCart = [&](size_t li) -> bool
                {
                    auto& bank = seg1Texbanks_[li];
                    if (bank.cartPtr && bank.size > 0 && !bank.entries.empty()) return true;
                    bank = {};
                    bank.lod = lodValues[li];

                    char n0[40]{}, n1[40]{}, n2[28]{}, n3[28]{}, n4[24]{}, n5[24]{}, n6[24]{}, n7[24]{}, n8[24]{}, n9[24]{}, n10[28]{}, n11[28]{};
                    std::snprintf(n0, sizeof(n0), "CD/DATA/TEXBANK_%d.BIN", bank.lod);
                    std::snprintf(n1, sizeof(n1), "CD/DATA/TEXBANK_%d.BIN;1", bank.lod);
                    std::snprintf(n2, sizeof(n2), "DATA/TEXBANK_%d.BIN", bank.lod);
                    std::snprintf(n3, sizeof(n3), "DATA/TEXBANK_%d.BIN;1", bank.lod);
                    std::snprintf(n4, sizeof(n4), "TEXBANK_%d.BIN", bank.lod);
                    std::snprintf(n5, sizeof(n5), "TEXBANK_%d.BIN;1", bank.lod);
                    std::snprintf(n6, sizeof(n6), "texbank_%d.bin", bank.lod);
                    std::snprintf(n7, sizeof(n7), "texbank_%d.bin;1", bank.lod);
                    std::snprintf(n8, sizeof(n8), "TBK%d.BIN", bank.lod);
                    std::snprintf(n9, sizeof(n9), "TBK%d.BIN;1", bank.lod);
                    std::snprintf(n10, sizeof(n10), "DATA/TBK%d.BIN", bank.lod);
                    std::snprintf(n11, sizeof(n11), "DATA/TBK%d.BIN;1", bank.lod);
                    const char* cands[] = { n0, n1, n2, n3, n4, n5, n6, n7, n8, n9, n10, n11 };

                    SRL::Cd::File f(nullptr);
                    bool found = false;
                    for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]); ++i)
                    {
                        SRL::Cd::File probe(cands[i]);
                        if (probe.Exists() && probe.Size.Bytes > 0)
                        {
                            f = probe;
                            found = true;
                            break;
                        }
                    }
                    if (!found || f.Size.Bytes <= 0) return false;
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
                    bank.cartPtr = mem;
                    bank.size = static_cast<uint32_t>(read);

                    const uint8_t* p = static_cast<const uint8_t*>(bank.cartPtr);
                    if (bank.size < 20) return false;
                    const uint32_t magic = ReadLe32(p + 0);
                    const uint16_t ver = ReadLe16(p + 4);
                    const uint16_t lod = ReadLe16(p + 6);
                    const uint32_t count = ReadLe32(p + 8);
                    const uint32_t dataOff = ReadLe32(p + 12);
                    (void)ver;
                    if (magic != 0x314B4254 || lod != static_cast<uint16_t>(bank.lod)) return false;
                    const uint32_t entryBase = 20;
                    const uint32_t entrySize = 16;
                    if (entryBase + count * entrySize > bank.size) return false;
                    if (dataOff > bank.size) return false;

                    bank.entries.clear();
                    bank.entries.reserve(count);
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        const uint32_t o = entryBase + i * entrySize;
                        Seg1TexbankEntry e{};
                        e.familyId = static_cast<uint16_t>(ReadLe32(p + o + 0));
                        e.offset = ReadLe32(p + o + 4);
                        e.size = ReadLe32(p + o + 8);
                        if (e.offset + e.size <= bank.size)
                        {
                            bank.entries.push_back(e);
                        }
                    }
                    return !bank.entries.empty();
                };

                for (size_t li = 0; li < 4; ++li)
                {
                    if (!loadTexbankToCart(li))
                    {
                        texFail += familyIdsUsedCount;
                        continue;
                    }
                    const auto& bank = seg1Texbanks_[li];
                    const uint8_t* bankBytes = static_cast<const uint8_t*>(bank.cartPtr);

                    for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                    {
                        const uint16_t fam = seg1FamilySlots_[u].familyId;
                        bool loadedForRequestedLod = false;
                        bool sawDecodeFail = false;
                        bool sawUploadFail = false;
                        bool sawMissingFamily = false;

                        for (int fallbackLi = static_cast<int>(li); fallbackLi >= 0; --fallbackLi)
                        {
                            if (!loadTexbankToCart(static_cast<size_t>(fallbackLi))) continue;
                            const auto& fallbackBank = seg1Texbanks_[static_cast<size_t>(fallbackLi)];
                            const uint8_t* fallbackBytes = static_cast<const uint8_t*>(fallbackBank.cartPtr);
                            if (!fallbackBytes) continue;

                            const Seg1TexbankEntry* entry = nullptr;
                            for (const auto& e : fallbackBank.entries)
                            {
                                if (e.familyId == fam) { entry = &e; break; }
                            }
                            if (!entry)
                            {
                                sawMissingFamily = true;
                                continue;
                            }

                            DecodedTgaTexture decoded{};
                            if (!DecodePalettedTgaMemory(fallbackBytes + entry->offset, entry->size, decoded))
                            {
                                sawDecodeFail = true;
                                if (fallbackLi == static_cast<int>(li))
                                {
                                    SRL::Debug::Print(1, 17, "S1 DEC fail f:%u l:%d", (unsigned)fam, lodValues[li]);
                                }
                                continue;
                            }

                            const int32_t slot = UploadDecodedTextureToVdp1(decoded);
                            if (slot >= 0)
                            {
                                seg1FamilySlots_[u].lodSlots[li] = static_cast<uint16_t>(slot);
                                ++texLoaded;
                                if (fallbackLi != static_cast<int>(li))
                                {
                                    ++texFallbackRecovered;
                                    texLastRecoveredDstLod = lodValues[li];
                                    texLastRecoveredSrcLod = lodValues[fallbackLi];
                                }
                                loadedForRequestedLod = true;
                                break;
                            }

                            sawUploadFail = true;
                            if (fallbackLi == static_cast<int>(li))
                            {
                                SRL::Debug::Print(1, 17, "S1 UP fail f:%u l:%d", (unsigned)fam, lodValues[li]);
                            }
                        }

                        if (!loadedForRequestedLod)
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
                            for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                            {
                                if (seg1FamilySlots_[u].familyId == fam)
                                {
                                    slot = seg1FamilySlots_[u].lodSlots[li];
                                    break;
                                }
                            }
                            if (slot != No_Texture)
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
                                for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                                {
                                    if (seg1FamilySlots_[u].familyId == fam)
                                    {
                                        slot = seg1FamilySlots_[u].lodSlots[li];
                                        break;
                                    }
                                }
                                if (slot != No_Texture)
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
                            for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                            {
                                if (seg1FamilySlots_[u].familyId == static_cast<uint16_t>(fam))
                                {
                                    slot = seg1FamilySlots_[u].lodSlots[seg1CurrentLodIndex_];
                                    break;
                                }
                            }
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

                    seg1FamilySlots_.clear();
                    seg1FamilySlots_.reserve(familyIdsUsedCount);
                    for (size_t u = 0; u < familyIdsUsedCount; ++u)
                    {
                        Seg1FamilySlotEntry slotEntry{};
                        slotEntry.familyId = static_cast<uint16_t>(familyIdsUsed[u]);
                        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
                        seg1FamilySlots_.push_back(slotEntry);
                    }

                    auto loadTexbankToCart = [&](size_t li) -> bool
                    {
                        auto& bank = seg1Texbanks_[li];
                        if (bank.cartPtr && bank.size > 0 && !bank.entries.empty()) return true;
                        bank = {};
                        bank.lod = lodValues[li];

                        char n0[40]{}, n1[40]{}, n2[28]{}, n3[28]{}, n4[24]{}, n5[24]{}, n6[24]{}, n7[24]{}, n8[24]{}, n9[24]{}, n10[28]{}, n11[28]{};
                        std::snprintf(n0, sizeof(n0), "CD/DATA/TEXBANK_%d.BIN", bank.lod);
                        std::snprintf(n1, sizeof(n1), "CD/DATA/TEXBANK_%d.BIN;1", bank.lod);
                        std::snprintf(n2, sizeof(n2), "DATA/TEXBANK_%d.BIN", bank.lod);
                        std::snprintf(n3, sizeof(n3), "DATA/TEXBANK_%d.BIN;1", bank.lod);
                        std::snprintf(n4, sizeof(n4), "TEXBANK_%d.BIN", bank.lod);
                        std::snprintf(n5, sizeof(n5), "TEXBANK_%d.BIN;1", bank.lod);
                        std::snprintf(n6, sizeof(n6), "texbank_%d.bin", bank.lod);
                        std::snprintf(n7, sizeof(n7), "texbank_%d.bin;1", bank.lod);
                        std::snprintf(n8, sizeof(n8), "TBK%d.BIN", bank.lod);
                        std::snprintf(n9, sizeof(n9), "TBK%d.BIN;1", bank.lod);
                        std::snprintf(n10, sizeof(n10), "DATA/TBK%d.BIN", bank.lod);
                        std::snprintf(n11, sizeof(n11), "DATA/TBK%d.BIN;1", bank.lod);
                        const char* cands[] = { n0, n1, n2, n3, n4, n5, n6, n7, n8, n9, n10, n11 };

                        SRL::Cd::File f(nullptr);
                        bool found = false;
                        for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]); ++i)
                        {
                            SRL::Cd::File probe(cands[i]);
                            if (probe.Exists() && probe.Size.Bytes > 0)
                            {
                                f = probe;
                                found = true;
                                break;
                            }
                        }
                        if (!found || f.Size.Bytes <= 0) return false;
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
                        bank.cartPtr = mem;
                        bank.size = static_cast<uint32_t>(read);

                        const uint8_t* p = static_cast<const uint8_t*>(bank.cartPtr);
                        if (bank.size < 20) return false;
                        const uint32_t magic = ReadLe32(p + 0);
                        const uint16_t ver = ReadLe16(p + 4);
                        const uint16_t lod = ReadLe16(p + 6);
                        const uint32_t count = ReadLe32(p + 8);
                        const uint32_t dataOff = ReadLe32(p + 12);
                        (void)ver;
                        if (magic != 0x314B4254 || lod != static_cast<uint16_t>(bank.lod)) return false;
                        const uint32_t entryBase = 20;
                        const uint32_t entrySize = 16;
                        if (entryBase + count * entrySize > bank.size) return false;
                        if (dataOff > bank.size) return false;

                        bank.entries.clear();
                        bank.entries.reserve(count);
                        for (uint32_t i = 0; i < count; ++i)
                        {
                            const uint32_t o = entryBase + i * entrySize;
                            Seg1TexbankEntry e{};
                            e.familyId = static_cast<uint16_t>(ReadLe32(p + o + 0));
                            e.offset = ReadLe32(p + o + 4);
                            e.size = ReadLe32(p + o + 8);
                            if (e.offset + e.size <= bank.size)
                            {
                                bank.entries.push_back(e);
                            }
                        }
                        return !bank.entries.empty();
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
                        const auto& bank = seg1Texbanks_[li];
                        const uint8_t* bankBytes = static_cast<const uint8_t*>(bank.cartPtr);

                        for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                        {
                            const uint16_t fam = seg1FamilySlots_[u].familyId;
                            const Seg1TexbankEntry* entry = nullptr;
                            for (const auto& e : bank.entries)
                            {
                                if (e.familyId == fam) { entry = &e; break; }
                            }
                            if (!entry) { ++texFail; ++texMissFamily; continue; }

                            DecodedTgaTexture decoded{};
                            if (!DecodePalettedTgaMemory(bankBytes + entry->offset, entry->size, decoded))
                            {
                                ++texFail;
                                ++texDecodeFail;
                                continue;
                            }
                            const int32_t slot = UploadDecodedTextureToVdp1(decoded);
                            if (slot >= 0)
                            {
                                seg1FamilySlots_[u].lodSlots[li] = static_cast<uint16_t>(slot);
                                ++texLoaded;
                            }
                            else
                            {
                                ++texFail;
                                ++texUploadFail;
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
                            for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                            {
                                if (seg1FamilySlots_[u].familyId == fam)
                                {
                                    slot = seg1FamilySlots_[u].lodSlots[li];
                                    break;
                                }
                            }
                            if (slot != No_Texture) slots[fi] = static_cast<int32_t>(slot);
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

                        std::vector<int32_t> faceSlots;
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
                seg1FamilySlots_.reserve(famCount);
                for (size_t u = 0; u < famCount; ++u)
                {
                    Seg1FamilySlotEntry slotEntry{};
                    slotEntry.familyId = static_cast<uint16_t>(familyIdsUsed[u]);
                    slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
                    seg1FamilySlots_.push_back(slotEntry);
                }
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
    coordinator_.BeginFrame(frameId);
}

void TrackSystem::RenderFrame(bool renderTrack,
                              const Vector3D& trackOffset,
                              const Vector3D& lightDirection,
                              const Vector3D& cameraLocation)
{
    if (!renderTrack || !ready_)
    {
        return;
    }

    bool segment01Logged = false;
    bool segment01Prepared = false;
    std::vector<SegmentHandle> orderedHandles = segmentHandles_;
    auto depthMetricToCamera = [&](const SegmentRenderEntry* e) -> SRL::Math::Types::Fxp
    {
        if (!e) return SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        const Vector3D c = e->center + trackOffset;
        const auto dx = (c.X - cameraLocation.X).Abs();
        const auto dz = (c.Z - cameraLocation.Z).Abs();
        // Overflow-safe depth key for fixed-point on SH2.
        // Keep monotonic far/near behavior without multiplication.
        const auto major = (dx > dz) ? dx : dz;
        const auto minor = (dx > dz) ? dz : dx;
        return major + minor;
    };
    // Keep selection in logical track order so the same SDR cache works for
    // forward and reverse traversal. A future gameplay flag can flip this.
    constexpr bool kReverseTrackDirection = false;
    std::sort(orderedHandles.begin(), orderedHandles.end(),
        [&](const SegmentHandle& a, const SegmentHandle& b)
        {
            const auto* ea = segmentPool_.Resolve(a);
            const auto* eb = segmentPool_.Resolve(b);
            if (!ea && !eb) return false;
            if (!ea) return false;
            if (!eb) return true;
            return kReverseTrackDirection ? (ea->id > eb->id) : (ea->id < eb->id);
        });
    if (!orderedHandles.empty())
    {
        // Deterministic test window: draw the configured package set for this profile.
        // Keep up to the configured budget for this frame.
        const size_t keepCount =
            std::min<size_t>(
                orderedHandles.size(),
                static_cast<size_t>(fixedVisibleSegmentCap_));
        orderedHandles.resize(keepCount);
        UpdateVisibleSegmentLods(orderedHandles);

        // Draw in camera-space painter order (far -> near).
        // VDP1 has no Z-buffer for this path, so camera-relative order is required
        // to avoid distortion when camera rotates around the car.
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
                    return kReverseTrackDirection ? (ea->id > eb->id) : (ea->id < eb->id);
                }
                // far first
                return da > db;
            });
    }

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

    // SEG_001 LOD cycle test: every ~3s swap texture slots among {8,16,32,64}.
    // Keep this test isolated to true single-segment scenes only.
    const bool isTrueSingleSegmentScene =
        (segmentRenderers_.size() == 1) &&
        (segmentRenderers_[0].logicalSegmentCount == 1) &&
        (segmentRenderers_[0].id == 1);
    if (isTrueSingleSegmentScene &&
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
                        for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                        {
                            if (seg1FamilySlots_[u].familyId == fam)
                            {
                                slot = seg1FamilySlots_[u].lodSlots[seg1CurrentLodIndex_];
                                break;
                            }
                        }
                    }

                    auto& attr = seg1ComponentAttrs_[fi];
                    if (slot != No_Texture)
                    {
                        // Keep render flags stable and only swap texture slot.
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
                SRL::Debug::Print(1, 21, "S1 AP rdr:%u", (unsigned)appliedRenderer);
            }
            else
            {
                SRL::Debug::Print(1, 21, "S1 AP rdr:off");
            }
            const int lodDbg[4] = { 8, 16, 32, 64 };
            const unsigned cmpFaces = (unsigned)seg1ComponentAttrs_.size();
            const unsigned rdrFaces = seg1RendererLodReady_ ? (unsigned)seg1RendererFaceSlotsByLod_[seg1CurrentLodIndex_].size() : 0u;
            SRL::Debug::Print(1, 22, "S1L c:%u j:%u l:%d r:%u",
                              (unsigned)seg1TgaPreloadCount_,
                              (unsigned)seg1TgaJsonOk_,
                              lodDbg[seg1CurrentLodIndex_], rdrFaces);
        }
        else
        {
            ++seg1LodFrameCounter_;
        }
    }
    else
    {
        SRL::Debug::Print(1, 22, "S1LI c:%u j:%u f:%u r:%u",
                          (unsigned)seg1TgaPreloadCount_,
                          (unsigned)seg1TgaJsonOk_,
                          (unsigned)seg1FamilySlots_.size(),
                          seg1RendererLodReady_ ? 1u : 0u);
    }

    std::array<uint8_t, kTrackSegmentLimit + 1> preparedCountById{};
    std::array<uint8_t, kTrackSegmentLimit + 1> renderedCountById{};

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
        for (size_t id = 1; id <= kTrackSegmentLimit; ++id)
        {
            if (renderedCountById[id] > 1)
            {
                SRL::Debug::Print(1, 24, "WARN rend dup seg:%u count:%u", (unsigned)id, (unsigned)renderedCountById[id]);
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
    // Work RAM monitor for runtime stability tuning.
    const auto hwr = SRL::Memory::HighWorkRam::GetReport();
    const auto lwr = SRL::Memory::LowWorkRam::GetReport();
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
