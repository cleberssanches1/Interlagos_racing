#include "track_system.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <string.h>
#include <vector>

#include "modelObject.hpp"
#include "resource_loader.hpp"
#include "segment_component_loader.hpp"
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

static bool ReadCdFileText(const char* const* names, size_t count, std::vector<char>& outText)
{
    for (size_t i = 0; i < count; ++i)
    {
        SRL::Cd::File f(names[i]);
        if (!f.Exists() || f.Size.Bytes <= 0) continue;
        if (!f.Open()) continue;
        const size_t size = static_cast<size_t>(f.Size.Bytes);
        std::vector<char> buf(size + 1, '\0');
        const int32_t r = f.Read(static_cast<int32_t>(size), buf.data());
        if (r <= 0) continue;
        outText.assign(buf.begin(), buf.begin() + static_cast<size_t>(r));
        outText.push_back('\0');
        return true;
    }
    return false;
}

static bool ReadCdFileBinary(const char* const* names, size_t count, std::vector<uint8_t>& outData)
{
    for (size_t i = 0; i < count; ++i)
    {
        SRL::Cd::File f(names[i]);
        if (!f.Exists() || f.Size.Bytes <= 0) continue;
        if (!f.Open()) continue;
        const size_t size = static_cast<size_t>(f.Size.Bytes);
        std::vector<uint8_t> buf(size);
        const int32_t r = f.Read(static_cast<int32_t>(size), buf.data());
        if (r <= 0) continue;
        outData.assign(buf.begin(), buf.begin() + static_cast<size_t>(r));
        return true;
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

static int FindTexbankFileIndexByFamily(const TexbankIndexLod& idx, int familyId)
{
    for (size_t i = 0; i < idx.count; ++i)
    {
        if (idx.familyIds[i] == familyId) return static_cast<int>(i);
    }
    return -1;
}

static bool ParseFaceFamilyArrayForSegment1(const char* json, Segment1TextureJson& out)
{
    const char* segs = strstr(json, "\"segments\"");
    if (!segs) return false;
    const char* id1 = strstr(segs, "\"id\":1");
    if (!id1) id1 = strstr(segs, "\"id\" : 1");
    if (!id1) return false;
    const char* arrKey = strstr(id1, "\"faceTextureFamily\"");
    if (!arrKey) return false;
    const char* b0 = strchr(arrKey, '[');
    if (!b0) return false;
    const char* b1 = strchr(b0, ']');
    if (!b1) return false;

    out.faceFamily.clear();
    const char* p = b0 + 1;
    while (p < b1)
    {
        while (p < b1 && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) ++p;
        if (p >= b1) break;
        char* endp = nullptr;
        const long v = strtol(p, &endp, 10);
        if (endp == p)
        {
            ++p;
            continue;
        }
        out.faceFamily.push_back(static_cast<int>(v));
        p = endp;
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

static int32_t TryLoadTextureFromCd(const char* fileName)
{
    const char* dirs[][2] = {
        { "DATA", nullptr },
        { "DATA", "ARQ_TGA" },
        { "ARQ_TGA", nullptr },
        { "data", nullptr },
        { "data", "arq_tga" },
        { "arq_tga", nullptr },
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
        int32_t slot = SRL::VDP1::TryLoadTexture((SRL::Bitmap::IBitmap*)&bmp);
        SRL::Cd::ChangeDir((const char*)0);
        if (slot >= 0) return slot;
    }
    SRL::Cd::ChangeDir((const char*)0);
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
    const bool famOk = ParseTextureFamilies64(json, out);
    const bool facesOk = ParseFaceFamilyArrayForSegment1(json, out);
    return famOk && facesOk;
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

static bool DecodePalettedTgaMemory(const uint8_t* data, size_t size, DecodedTgaTexture& out)
{
    out = {};
    if (!data || size < 18) return false;
    const uint8_t idLen = data[0];
    const uint8_t colorMapType = data[1];
    const uint8_t imageType = data[2];
    if (colorMapType != 1) return false;
    if (imageType != 1) return false; // uncompressed colormapped TGA

    const uint16_t cmapFirst = ReadLe16(data + 3);
    const uint16_t cmapLen = ReadLe16(data + 5);
    const uint8_t cmapDepth = data[7];
    (void)cmapFirst;
    out.width = ReadLe16(data + 12);
    out.height = ReadLe16(data + 14);
    const uint8_t pixelDepth = data[16];
    if (out.width == 0 || out.height == 0) return false;
    if (pixelDepth != 8) return false;
    if (cmapLen == 0 || cmapLen > 256) return false;
    if (!(cmapDepth == 24 || cmapDepth == 32 || cmapDepth == 16)) return false;

    size_t off = 18 + static_cast<size_t>(idLen);
    const size_t cmapBytes = static_cast<size_t>(cmapLen) * static_cast<size_t>(cmapDepth / 8);
    if (off + cmapBytes > size) return false;

    out.palette.resize(cmapLen);
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
        out.palette[i] = hc;
    }
    off += cmapBytes;

    const size_t srcPixels = static_cast<size_t>(out.width) * static_cast<size_t>(out.height);
    if (off + srcPixels > size) return false;
    const uint8_t* src = data + off;

    if (cmapLen <= 16)
    {
        out.mode = SRL::CRAM::TextureColorMode::Paletted16;
        out.pixels.resize(srcPixels / 2);
        for (size_t i = 0; i + 1 < srcPixels; i += 2)
        {
            out.pixels[i / 2] = static_cast<uint8_t>(((src[i] & 0x0F) << 4) | (src[i + 1] & 0x0F));
        }
    }
    else if (cmapLen <= 64)
    {
        out.mode = SRL::CRAM::TextureColorMode::Paletted64;
        out.pixels.assign(src, src + srcPixels);
    }
    else if (cmapLen <= 128)
    {
        out.mode = SRL::CRAM::TextureColorMode::Paletted128;
        out.pixels.assign(src, src + srcPixels);
    }
    else
    {
        out.mode = SRL::CRAM::TextureColorMode::Paletted256;
        out.pixels.assign(src, src + srcPixels);
    }
    return true;
}

static int32_t UploadDecodedTextureToVdp1(const DecodedTgaTexture& tex)
{
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
            SRL::Debug::Print(1, 12, "Segment %03u failed to copy (missing?)", unsigned(i));
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
    renderers.reserve(entries.size());
    for (auto& entry : entries)
    {
        if (!entry.copy.cartPtr || entry.copy.size == 0) continue;

        auto model = std::make_unique<ModelObject>();
        const int32_t cartFreeBefore = SRL::Memory::CartRam::GetFreeSpace();
        SRL::Debug::Print(1, 14, "Segment load begin %03d bytes:%u cartFree:%d",
                          entry.id, (unsigned)entry.copy.size, cartFreeBefore);

        if (!model->LoadFromMemory(entry.copy.cartPtr, entry.copy.size, 0, false, 0, false, true))
        {
            const int32_t cartFreeAfter = SRL::Memory::CartRam::GetFreeSpace();
            SRL::Debug::Print(1, 14, "Segment load fail %03d cartFreeNow:%d", entry.id, cartFreeAfter);
            continue;
        }
        auto renderer = std::make_unique<TrackRenderer>();
        ModelObject* rawModel = model.release();

        if (!renderer->InitializeFromModelObject(rawModel, 0))
        {
            SRL::Debug::Print(1, 15, "Renderer init fail %03d", entry.id);
            delete rawModel;
            continue;
        }

        // Keep original model path for stable segment placement.
        renderer->SetUseOriginal(true);
        // Keep original face visibility from model to avoid front/back overdraw artifacts.
        renderer->SetForceDoubleSided(false);
        renderer->SetDrawLimit(renderer->MeshCount());
        SegmentRenderEntry item{};
        item.id = entry.id;
        item.center = ComputeRendererCenter(*renderer);
        item.renderer = std::move(renderer);
        renderers.push_back(std::move(item));
    }
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

bool TrackSystem::Initialize(const Config& config)
{
    ready_ = false;
    segmentsReady_ = false;
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
    segmentEntries_.clear();
    segmentRenderers_.clear();
    segmentHandles_.clear();
    soakMonitor_.Reset();

    const size_t loadLimit =
        (config.initialSegments == 0) ? kTrackSegmentLimit : std::min<size_t>(config.initialSegments, kTrackSegmentLimit);
    segmentEntries_.reserve(loadLimit);
    segmentRenderers_.reserve(loadLimit);

    // 1) Preload all track segment NYA blobs to cart RAM once (raw catalog).
    constexpr size_t kRawCatalogMaxSegments = 512;
    size_t rawCopied = 0;
    for (size_t i = 1; i <= kRawCatalogMaxSegments; ++i)
    {
        TrackSegmentCopy copy = CopySegmentById(i);
        if (!copy.cartPtr || copy.size == 0)
        {
            if (i == 1)
            {
                SRL::Debug::Print(1, 12, "Segment %03u failed to copy (missing?)", unsigned(i));
            }
            break;
        }
        rawSegmentCatalog_.push_back({ static_cast<int>(i), copy });
        ++rawCopied;
    }
    SRL::Debug::Print(1, 13, "Track catalog copied %u", unsigned(rawCopied));

    // 2) Build runtime renderers from catalog entries (no extra CD access).
    size_t builtCount = 0;
    for (size_t i = 1; i <= loadLimit; ++i)
    {
        const TrackSegmentCopy* copy = FindRawSegmentCopyById(static_cast<int>(i));
        if (!copy)
        {
            SRL::Debug::Print(1, 12, "Segment %03u missing in cart catalog", unsigned(i));
            break;
        }

        std::vector<TrackSegmentEntry> singleEntry;
        singleEntry.reserve(1);
        singleEntry.push_back({ static_cast<int>(i), *copy });

        auto built = BuildSegmentRenderers(singleEntry);
        if (!built.empty())
        {
            segmentRenderers_.push_back(std::move(built[0]));
            ++builtCount;
        }

        // Keep registry by id (raw data lives in rawSegmentCatalog_).
        segmentEntries_.push_back({ static_cast<int>(i), {} });
    }
    SRL::Debug::Print(1, 13, "Track segments built %u/%u", unsigned(builtCount), unsigned(segmentEntries_.size()));
    SRL::Debug::Print(1, 26, "Track segment registry entries:%lu", (unsigned long)segmentEntries_.size());
    segmentsReady_ = !segmentRenderers_.empty();
    segmentHandles_ = BuildSegmentHandleTable();

    (void)config.useSlave; // stability mode: always use synchronous/double-buffer producer

    TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::Config coordinatorConfig{};
    coordinatorConfig.budget.maxTrackSegments = std::min<uint32_t>(config.initialSegments, static_cast<uint32_t>(kTrackSegmentLimit));
    coordinatorConfig.budget.maxTrackMeshes = config.initialMeshes;
    coordinatorConfig.budget.maxTrackFaces = config.initialFaces;
    coordinatorConfig.chunkCapacity = kTrackSegmentLimit;

    const bool coordinatorReady = coordinator_.Initialize(coordinatorConfig);
    if (!coordinatorReady)
    {
        SRL::Debug::Print(1, 31, "TrackRenderCoordinator HWR alloc failed");
    }

    AdaptiveTrackBudgetController::Limits adaptiveBudgetLimits{};
    const uint32_t minSegmentsRequested = std::max<uint32_t>(1, config.minSegments);
    const uint32_t maxSegmentsCap = static_cast<uint32_t>(kTrackSegmentLimit);
    adaptiveBudgetLimits.minSegments = std::min<uint32_t>(minSegmentsRequested, maxSegmentsCap);
    adaptiveBudgetLimits.maxSegments = adaptiveBudgetLimits.minSegments;
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
        SRL::Debug::Print(1, 27, "Nearest segment candidates (%lu): %s", (unsigned long)count, ids);
    }

    // Single-face overwrite probe (disabled - unstable in current runtime path).
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
    {
        constexpr bool kUseSeg1ComponentRenderer = false; // usar renderer normal da pista para LOD swap
        const char* geoCandidates[] = {
            "CD/DATA/SEG_001.GEO", "CD/DATA/SEG_001.GEO;1",
            "DATA/SEG_001.GEO", "DATA/SEG_001.GEO;1",
            "SEG_001.GEO", "SEG_001.GEO;1",
            "seg_001.geo", "seg_001.geo;1"
        };
        const char* matCandidates[] = {
            "CD/DATA/SEG_001.MAT", "CD/DATA/SEG_001.MAT;1",
            "DATA/SEG_001.MAT", "DATA/SEG_001.MAT;1",
            "SEG_001.MAT", "SEG_001.MAT;1",
            "seg_001.mat", "seg_001.mat;1"
        };

        SegmentComponent::Blob geoBlob{};
        SegmentComponent::Blob matBlob{};
        const bool geoOk = SegmentComponent::Loader::LoadFirstExistingFromCd(
            geoCandidates, sizeof(geoCandidates) / sizeof(geoCandidates[0]), geoBlob);
        const bool matOk = SegmentComponent::Loader::LoadFirstExistingFromCd(
            matCandidates, sizeof(matCandidates) / sizeof(matCandidates[0]), matBlob);
        constexpr bool kShowSeg1ComponentProbeLogs = false;
        if (kShowSeg1ComponentProbeLogs)
        {
            SRL::Debug::Print(1, 24, "CMP SEG001 GEO:%d(%u) MAT:%d(%u)",
                              geoOk ? 1 : 0, (unsigned)geoBlob.size,
                              matOk ? 1 : 0, (unsigned)matBlob.size);
        }

        SegmentComponent::Loader::GeoView geoView{};
        SegmentComponent::Loader::MatView matView{};
        const bool geoParsed = geoOk && SegmentComponent::Loader::ParseGeo(geoBlob, geoView);
        const bool matParsed = matOk && SegmentComponent::Loader::ParseMat(matBlob, matView);
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
                    if (!SegmentComponent::Loader::ReadPodAt(geoBlob.bytes, off, gv))
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
                    if (!SegmentComponent::Loader::ReadPodAt(matBlob.bytes, moff, mb)) continue;
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

                    char n0[40]{}, n1[40]{}, n2[28]{}, n3[28]{}, n4[24]{}, n5[24]{}, n6[24]{}, n7[24]{};
                    std::snprintf(n0, sizeof(n0), "CD/DATA/TEXBANK_%d.BIN", bank.lod);
                    std::snprintf(n1, sizeof(n1), "CD/DATA/TEXBANK_%d.BIN;1", bank.lod);
                    std::snprintf(n2, sizeof(n2), "DATA/TEXBANK_%d.BIN", bank.lod);
                    std::snprintf(n3, sizeof(n3), "DATA/TEXBANK_%d.BIN;1", bank.lod);
                    std::snprintf(n4, sizeof(n4), "TEXBANK_%d.BIN", bank.lod);
                    std::snprintf(n5, sizeof(n5), "TEXBANK_%d.BIN;1", bank.lod);
                    std::snprintf(n6, sizeof(n6), "texbank_%d.bin", bank.lod);
                    std::snprintf(n7, sizeof(n7), "texbank_%d.bin;1", bank.lod);
                    const char* cands[] = { n0, n1, n2, n3, n4, n5, n6, n7 };

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
                        const Seg1TexbankEntry* entry = nullptr;
                        for (const auto& e : bank.entries)
                        {
                            if (e.familyId == fam) { entry = &e; break; }
                        }
                        if (!entry) { ++texFail; continue; }

                        DecodedTgaTexture decoded{};
                        if (!DecodePalettedTgaMemory(bankBytes + entry->offset, entry->size, decoded))
                        {
                            ++texFail;
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
                        }
                    }
                }
                SRL::Debug::Print(1, 30, "S1 TEX ok:%u fl:%u fm:%u",
                                  (unsigned)texLoaded, (unsigned)texFail, (unsigned)familyIdsUsedCount);

                // Build fallback remap tables for TrackRenderer path (global face order).
                // This allows LOD swap even when component renderer is disabled.
                seg1RendererLodReady_ = false;
                for (auto& v : seg1RendererFaceSlotsByLod_) v.clear();
                if (seg1Renderer)
                {
                    const size_t rendererFaces = static_cast<size_t>(seg1Renderer->FaceCount());
                    const size_t mapFaces = static_cast<size_t>(matView.header.faceCount);
                    const size_t nFaces = std::min(rendererFaces, mapFaces);
                    for (size_t li = 0; li < 4; ++li)
                    {
                        auto& slots = seg1RendererFaceSlotsByLod_[li];
                        slots.assign(rendererFaces, -1);
                        size_t mappedFaces = 0;
                        for (size_t fi = 0; fi < nFaces; ++fi)
                        {
                            SegmentComponent::MatFaceBinding mb{};
                            const size_t moff = matView.bindingOffset + fi * sizeof(SegmentComponent::MatFaceBinding);
                            if (!SegmentComponent::Loader::ReadPodAt(matBlob.bytes, moff, mb)) continue;
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
                        const int lodDbg[4] = { 8, 16, 32, 64 };
                        SRL::Debug::Print(1, 18, "S1 M%d:%u", lodDbg[li], (unsigned)mappedFaces);
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
                        SRL::Debug::Print(1, 18, "S1 TEX raw miss");
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
                    if (!SegmentComponent::Loader::ReadPodAt(geoBlob.bytes, off, gf))
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
                    uint16_t baseColor = 0x83FF;
                    uint16_t drawMode = CL32KRGB;
                    uint32_t directionFlags = sprPolygon;
                    uint16_t shading = UseLight;

                    SegmentComponent::MatFaceBinding mb{};
                    const size_t moff = matView.bindingOffset + static_cast<size_t>(fi) * sizeof(SegmentComponent::MatFaceBinding);
                    uint16_t faceFamilyId = 0;
                    if (SegmentComponent::Loader::ReadPodAt(matBlob.bytes, moff, mb))
                    {
                        const int fam = static_cast<int>(mb.materialId);
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
                    "segments_map.json;1"
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

                        char n0[40]{}, n1[40]{}, n2[28]{}, n3[28]{}, n4[24]{}, n5[24]{}, n6[24]{}, n7[24]{};
                        std::snprintf(n0, sizeof(n0), "CD/DATA/TEXBANK_%d.BIN", bank.lod);
                        std::snprintf(n1, sizeof(n1), "CD/DATA/TEXBANK_%d.BIN;1", bank.lod);
                        std::snprintf(n2, sizeof(n2), "DATA/TEXBANK_%d.BIN", bank.lod);
                        std::snprintf(n3, sizeof(n3), "DATA/TEXBANK_%d.BIN;1", bank.lod);
                        std::snprintf(n4, sizeof(n4), "TEXBANK_%d.BIN", bank.lod);
                        std::snprintf(n5, sizeof(n5), "TEXBANK_%d.BIN;1", bank.lod);
                        std::snprintf(n6, sizeof(n6), "texbank_%d.bin", bank.lod);
                        std::snprintf(n7, sizeof(n7), "texbank_%d.bin;1", bank.lod);
                        const char* cands[] = { n0, n1, n2, n3, n4, n5, n6, n7 };

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
                            if (!entry) { ++texFail; continue; }

                            DecodedTgaTexture decoded{};
                            if (!DecodePalettedTgaMemory(bankBytes + entry->offset, entry->size, decoded))
                            {
                                ++texFail;
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

    ready_ = coordinatorReady && segmentsReady_;
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
    auto manhattanToCamera = [&](const SegmentRenderEntry* e) -> SRL::Math::Types::Fxp
    {
        if (!e) return SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        const Vector3D c = e->center + trackOffset;
        return (c.X - cameraLocation.X).Abs() + (c.Z - cameraLocation.Z).Abs();
    };

    // 1) Budget selection must keep nearest segments first.
    std::sort(orderedHandles.begin(), orderedHandles.end(),
        [&](const SegmentHandle& a, const SegmentHandle& b)
        {
            const auto* ea = segmentPool_.Resolve(a);
            const auto* eb = segmentPool_.Resolve(b);
            if (!ea && !eb) return false;
            if (!ea) return false;
            if (!eb) return true;
            const auto da = manhattanToCamera(ea);
            const auto db = manhattanToCamera(eb);
            if (da == db) return ea->id < eb->id;
            return da < db;
        });
    if (!orderedHandles.empty())
    {
        const size_t keepCount =
            std::min<size_t>(static_cast<size_t>(coordinator_.Budget().maxTrackSegments), orderedHandles.size());
        orderedHandles.resize(keepCount);

        // 2) Rendering order for VDP1 painter: far -> near.
        std::sort(orderedHandles.begin(), orderedHandles.end(),
            [&](const SegmentHandle& a, const SegmentHandle& b)
            {
                const auto* ea = segmentPool_.Resolve(a);
                const auto* eb = segmentPool_.Resolve(b);
                if (!ea && !eb) return false;
                if (!ea) return false;
                if (!eb) return true;
                const auto da = manhattanToCamera(ea);
                const auto db = manhattanToCamera(eb);
                if (da == db) return ea->id > eb->id;
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
    if ((seg1ComponentEnabled_ || seg1RendererLodReady_) && !seg1FamilySlots_.empty())
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
                    else
                    {
                        // Fallback path without texture for missing family/lod slot.
                        attr.Texture = No_Texture;
                        attr.ColorMode = 0x83FF;
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
            SRL::Debug::Print(1, 22, "S1 LOD:%d c:%u r:%u",
                              lodDbg[seg1CurrentLodIndex_], cmpFaces, rdrFaces);
        }
        else
        {
            ++seg1LodFrameCounter_;
        }
    }
    else
    {
        SRL::Debug::Print(1, 22, "S1 LOD idle c:%u r:%u f:%u",
                          seg1ComponentEnabled_ ? 1u : 0u,
                          seg1RendererLodReady_ ? 1u : 0u,
                          (unsigned)seg1FamilySlots_.size());
    }

    std::array<uint8_t, kTrackSegmentLimit + 1> preparedCountById{};
    std::array<uint8_t, kTrackSegmentLimit + 1> renderedCountById{};

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

    const FrameBudget nextBudget = budgetController_.Update(coordinator_.Budget(), coordinator_.Telemetry());
    coordinator_.SetBudget(nextBudget);
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
