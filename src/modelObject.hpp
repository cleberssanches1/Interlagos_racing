#pragma once

#include <srl.hpp>
#include <vector>

// Desliga logs desta unidade (tela VDP2). Altere para true para depurar carregamentos.
constexpr bool kModelLog = true;
#define MO_LOG(...) do { if constexpr (kModelLog) { SRL::Debug::Print(__VA_ARGS__); } } while(0)

/** @brief Detect whether object has size function
 * @tparam T Object type
 */
template<typename T>
concept HasLoadSizeFunction = requires {
    { std::declval<T>().LoadSize() } -> std::same_as<size_t>;
};

/** @brief Get object pointer from stream buffer
 * @tparam T Object type
 * @param iterator Stream buffer
 * @param count Number of objects
 * @return T* Object pointer
 */
template<typename T>
T* GetAndIterate(char*& iterator, size_t count = 1)
{
    T* ptr = reinterpret_cast<T*>(iterator);

    if constexpr (HasLoadSizeFunction<T>)
    {
        iterator += ptr->LoadSize() * count;
    }
    else
    {
        iterator += (sizeof(T) * count);
    }

    return ptr;
}
    
/** @brief Model object
 */
class ModelObject
{
private:

    /** @brief Model file header
     */
    struct ModelHeader
    {
        /** @brief Mesh type, 0 = PDATA, 1 = XPDATA 
         */
        uint32_t Type;

        /** @brief Number of meshes inside the model file
         */
        uint32_t MeshCount;

        /** @brief Number of textures inside the mesh file
         */
        uint32_t TextureCount;
    };

    /** @brief Texture header, textures are always RGB1555
     */
    struct TextureHeader
    {
        /** @brief Width of the texture
         */
        uint16_t Width;

        /** @brief Height of the texture
         */
        uint16_t Height;

        /** @brief Object size
         * @return Object size
         */
        size_t LoadSize() const
        {
            return sizeof(TextureHeader) + (sizeof(SRL::Types::HighColor) * (Width * Height));
        }

        /** @brief Object data
         * @return The data pointer
         */
        SRL::Types::HighColor* Data() const
        {
            return (SRL::Types::HighColor*)(((char*)this) + sizeof(TextureHeader));
        }
    };

    struct TextureHeaderV2
    {
        uint32_t Magic;
        uint16_t Version;
        uint8_t ColorMode;
        uint8_t Reserved;
        uint16_t PaletteId;
        uint16_t PaletteColorCount;
    };

    static constexpr uint32_t kTextureHeaderV2Magic = 0x4E595458; // "NYTX"

    static SRL::CRAM::TextureColorMode DecodeTextureColorMode(uint8_t mode)
    {
        switch (mode)
        {
        case 2: return SRL::CRAM::TextureColorMode::Paletted16;
        case 4: return SRL::CRAM::TextureColorMode::Paletted64;
        case 5: return SRL::CRAM::TextureColorMode::Paletted128;
        case 6: return SRL::CRAM::TextureColorMode::Paletted256;
        default: return SRL::CRAM::TextureColorMode::RGB555;
        }
    }

    static bool IsKnownTextureColorMode(uint8_t mode)
    {
        return mode == 0 || mode == 2 || mode == 4 || mode == 5 || mode == 6;
    }

    static TextureHeaderV2 DecodeTextureHeaderV2Raw(const uint8_t* raw8)
    {
        TextureHeaderV2 out{};
        out.Magic = kTextureHeaderV2Magic;

        // Preferred layout (new exporter):
        // [Version:2][ColorMode:1][Reserved:1][PaletteId:2][PaletteColorCount:2]
        uint16_t versionNew = uint16_t(raw8[0] << 8 | raw8[1]);
        uint8_t colorNew = raw8[2];
        uint8_t reservedNew = raw8[3];
        uint16_t palIdNew = uint16_t(raw8[4] << 8 | raw8[5]);
        uint16_t palCntNew = uint16_t(raw8[6] << 8 | raw8[7]);

        // Legacy layout:
        // [Version:1][ColorMode:1][PaletteId:2][PaletteColorCount:2][Reserved:2]
        uint16_t versionLegacy = raw8[0];
        uint8_t colorLegacy = raw8[1];
        uint16_t palIdLegacy = uint16_t(raw8[2] << 8 | raw8[3]);
        uint16_t palCntLegacy = uint16_t(raw8[4] << 8 | raw8[5]);
        uint8_t reservedLegacy = raw8[7];

        const bool newLooksValid = IsKnownTextureColorMode(colorNew);
        const bool legacyLooksValid = IsKnownTextureColorMode(colorLegacy);
        const bool useLegacy = legacyLooksValid && !newLooksValid;

        if (useLegacy)
        {
            out.Version = versionLegacy;
            out.ColorMode = colorLegacy;
            out.Reserved = reservedLegacy;
            out.PaletteId = palIdLegacy;
            out.PaletteColorCount = palCntLegacy;
        }
        else
        {
            out.Version = versionNew;
            out.ColorMode = colorNew;
            out.Reserved = reservedNew;
            out.PaletteId = palIdNew;
            out.PaletteColorCount = palCntNew;
        }

        return out;
    }

    static size_t TextureDataByteSize(uint16_t width, uint16_t height, SRL::CRAM::TextureColorMode mode)
    {
        const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        switch (mode)
        {
        case SRL::CRAM::TextureColorMode::Paletted16:
            return pixels >> 1; // 4bpp
        case SRL::CRAM::TextureColorMode::Paletted64:
        case SRL::CRAM::TextureColorMode::Paletted128:
        case SRL::CRAM::TextureColorMode::Paletted256:
            return pixels;      // 8bpp
        default:
            return pixels * sizeof(SRL::Types::HighColor); // 16bpp
        }
    }

    static bool IsPalettedMode(SRL::CRAM::TextureColorMode mode)
    {
        return mode != SRL::CRAM::TextureColorMode::RGB555;
    }

    static uint16_t ReservedPaletteBanksForMode(SRL::CRAM::TextureColorMode mode)
    {
        // Reserve low CRAM banks to avoid conflicts with VDP2/debug/font palettes.
        // Reservation is expressed in mode-specific bank units.
        switch (mode)
        {
        case SRL::CRAM::TextureColorMode::Paletted256: return 1;  // 16*16 colors
        case SRL::CRAM::TextureColorMode::Paletted128: return 2;  // 2 * 128 colors
        case SRL::CRAM::TextureColorMode::Paletted64:  return 4;  // 4 * 64 colors
        case SRL::CRAM::TextureColorMode::Paletted16:  return 16; // 16 * 16 colors
        default: return 0;
        }
    }

    static uint16_t MaxPaletteBanksForMode(SRL::CRAM::TextureColorMode mode)
    {
        switch (mode)
        {
        case SRL::CRAM::TextureColorMode::Paletted256: return 8;
        case SRL::CRAM::TextureColorMode::Paletted128: return 16;
        case SRL::CRAM::TextureColorMode::Paletted64:  return 32;
        case SRL::CRAM::TextureColorMode::Paletted16:  return 128;
        default: return 0;
        }
    }

    static int32_t AllocatePaletteBank(SRL::CRAM::TextureColorMode mode)
    {
        if (!IsPalettedMode(mode))
        {
            return 0;
        }

        const uint16_t start = ReservedPaletteBanksForMode(mode);
        const uint16_t limit = MaxPaletteBanksForMode(mode);
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

    // SGL defines No_Texture as 0. Reserve texture slot 0 with a dummy texture
    // so real model textures never collide with the "no texture" sentinel.
    static void EnsureTextureZeroReserved()
    {
        if (SRL::VDP1::GetTextureCount() != 0) return;

        static SRL::Types::HighColor dummy[8 * 8];
        for (size_t i = 0; i < (8 * 8); ++i)
        {
            dummy[i] = SRL::Types::HighColor::FromRGB555(1, 1, 1);
        }

        (void)SRL::VDP1::TryLoadTexture(
            8,
            8,
            SRL::CRAM::TextureColorMode::RGB555,
            0,
            dummy);
    }

    /** @brief Mesh data header
     */
    struct MeshHeader
    {
        /** @brief Number of points in the mesh
         */
        uint32_t PointCount;

        /** @brief Number of polygons in the mesh
         */
        uint32_t PolygonCount;
    };

    /** @brief Face attributes (XPDATA original formato) */
    struct Attribute
    {
        uint8_t HasTexture : 1;
        uint8_t HasMeshEffect : 1;
        uint8_t IsDoubleSided : 1;
        uint8_t HasTransparency: 1;
        uint8_t HasFlatShading : 1;
        uint8_t HasHalfBrightness : 1;
        uint8_t SortMode : 2;
        uint8_t IsWireframe : 1;
        uint8_t Reserved : 7;
        SRL::Types::HighColor BaseColor;
        int32_t Texture;
    };

    /** @brief Face flags gravados em PDATA (8 bytes por face) */
    struct FaceFlagsDisk
    {
        uint8_t Flags = 0;
        uint8_t Flags2 = 0;
        uint16_t BaseColor = 0;
        int32_t TextureId = -1;

        bool HasMeshEffect() const { return (Flags & 0x40) != 0; }
        bool HasTexture() const { return (Flags & 0x80) != 0; }
        bool IsDoubleSided() const { return (Flags & 0x20) != 0; }
        bool IsHalfBright() const { return (Flags & 0x04) != 0; }
        bool IsHalfTransparent() const { return (Flags & 0x10) != 0; }
        bool IsWireframe() const { return (Flags2 & 0x80) != 0; }
    };

    /** @brief Face flags raw layout used by smooth meshes (legacy/new exporters) */
    struct SmoothFaceFlagsRaw
    {
        uint8_t Flags = 0;
        uint8_t Flags2 = 0;
        uint16_t BaseColor = 0;
        int32_t TextureId = -1;
    };

    enum class SmoothFaceFlagsEncoding
    {
        LegacyAttributeBits,
        FaceFlagsDiskBits
    };

    static SmoothFaceFlagsEncoding DetectSmoothFaceEncoding(const SmoothFaceFlagsRaw* flags, size_t count)
    {
        if (!flags || count == 0) return SmoothFaceFlagsEncoding::LegacyAttributeBits;

        size_t texturedBit0 = 0;
        size_t texturedBit7 = 0;
        size_t wireBit0 = 0;
        size_t wireBit7 = 0;

        for (size_t i = 0; i < count; ++i)
        {
            texturedBit0 += (flags[i].Flags & 0x01) ? 1 : 0;
            texturedBit7 += (flags[i].Flags & 0x80) ? 1 : 0;
            wireBit0 += (flags[i].Flags2 & 0x01) ? 1 : 0;
            wireBit7 += (flags[i].Flags2 & 0x80) ? 1 : 0;
        }

        const bool diskLikely = (texturedBit7 > texturedBit0 * 2) || (wireBit7 > wireBit0 * 2);
        const bool legacyLikely = (texturedBit0 > texturedBit7 * 2) || (wireBit0 > wireBit7 * 2);

        if (diskLikely && !legacyLikely) return SmoothFaceFlagsEncoding::FaceFlagsDiskBits;
        if (legacyLikely && !diskLikely) return SmoothFaceFlagsEncoding::LegacyAttributeBits;

        // Prefer modern disk flags when ambiguous (SEG_*.NYA exporter).
        return SmoothFaceFlagsEncoding::FaceFlagsDiskBits;
    }

    /** @brief Loaded mesh data
     */
    void* meshes;

    /** @brief Number of loaded meshes
     */
    size_t meshCount;

    /** @brief Index of first loaded texture
     */
    int32_t startTextureIndex;

    /** @brief Number of loaded textures
     */
    size_t textureCount;

    /** @brief Mesh type
     */
    uint32_t type;

    /** @brief Carregar somente primeiro mesh (diagnstico) */
    bool firstMeshOnly = false;
    bool forceBigEndian = false;
    /** @brief Limitador opcional de meshes carregados (0 = todos) */
    size_t maxMeshesToLoad = 0;
    // Forca alocacao em HighWorkRam (modo pista)
    bool forceHwrAlloc = false;
    // Forca leitura somente em streaming (nao carrega buffer inteiro)
    bool streamOnly = false;
    // True when model payload (mesh/flags/textures) must be byte-swapped from BE.
    bool swapDataEndian = false;

    /** @brief Offset in gouraud table
     */
    size_t gouraudOffset;

public:

    /** @brief Disable copy to avoid double-free of mesh buffers */
    ModelObject(const ModelObject&) = delete;
    ModelObject& operator=(const ModelObject&) = delete;

    /** @brief Default constructor */
    ModelObject() noexcept
        : meshes(nullptr),
          meshCount(0),
          startTextureIndex(-1),
          textureCount(0),
          type(0),
          firstMeshOnly(false),
          forceBigEndian(false),
          maxMeshesToLoad(0),
          forceHwrAlloc(false),
          streamOnly(false),
          swapDataEndian(false),
          gouraudOffset(0)
    {}

    /** @brief Move constructor */
    ModelObject(ModelObject&& other) noexcept
    {
        this->meshes = other.meshes;
        this->meshCount = other.meshCount;
        this->startTextureIndex = other.startTextureIndex;
        this->textureCount = other.textureCount;
        this->type = other.type;
        this->gouraudOffset = other.gouraudOffset;

        other.meshes = nullptr;
        other.meshCount = 0;
        other.textureCount = 0;
        other.startTextureIndex = -1;
        other.type = 0;
        other.gouraudOffset = 0;
    }

    /** @brief Move assignment */
    ModelObject& operator=(ModelObject&& other) noexcept
    {
        if (this != &other)
        {
            if (this->meshes)
            {
                if (this->type == 0)
                    delete[] (SRL::Types::Mesh*)this->meshes;
                else
                    delete[] (SRL::Types::SmoothMesh*)this->meshes;
            }

            this->meshes = other.meshes;
            this->meshCount = other.meshCount;
            this->startTextureIndex = other.startTextureIndex;
            this->textureCount = other.textureCount;
            this->type = other.type;
            this->gouraudOffset = other.gouraudOffset;

            other.meshes = nullptr;
            other.meshCount = 0;
            other.textureCount = 0;
            other.startTextureIndex = -1;
            other.type = 0;
            other.gouraudOffset = 0;
        }
        return *this;
    }

private:

    void RemapLoadedTextureIndices(size_t textureBase, const std::vector<int32_t>& remap)
    {
        if (!this->meshes || remap.empty()) return;

        auto remapAttr = [&](SRL::Types::Attribute& attr)
        {
            if (attr.Texture == No_Texture) return;
            if (attr.Texture < textureBase) return;

            const size_t raw = static_cast<size_t>(attr.Texture - textureBase);
            if (raw >= remap.size())
            {
                attr.Texture = No_Texture;
                return;
            }

            const int32_t actual = remap[raw];
            if (actual < 0)
            {
                attr.Texture = static_cast<uint16_t>(No_Texture);
                return;
            }

            attr.Texture = static_cast<uint16_t>(actual);

            // Update attribute color mode/palette for paletted textures.
            // Legacy path assumes RGB555 (CL32KRGB + No_Palet), which breaks 4bpp/8bpp assets.
            const auto& meta = SRL::VDP1::Metadata[attr.Texture];
            uint16_t colorMode = CL32KRGB;
            uint16_t palette = No_Palet;
            switch (meta.ColorMode)
            {
            case SRL::CRAM::TextureColorMode::Paletted256:
                colorMode = CL256Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 8);
                break;
            case SRL::CRAM::TextureColorMode::Paletted128:
                colorMode = CL128Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 7);
                break;
            case SRL::CRAM::TextureColorMode::Paletted64:
                colorMode = CL64Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 6);
                break;
            case SRL::CRAM::TextureColorMode::Paletted16:
                colorMode = CL16Bnk;
                palette = static_cast<uint16_t>(meta.PaletteId << 4);
                break;
            default:
                colorMode = CL32KRGB;
                palette = No_Palet;
                break;
            }

            attr.Display = (attr.Display & ~(CL32KRGB | CL16Bnk | CL64Bnk | CL128Bnk | CL256Bnk)) | colorMode;
            attr.ColorMode = palette;
        };

        if (this->type == 1)
        {
            auto* meshes = reinterpret_cast<SRL::Types::SmoothMesh*>(this->meshes);
            for (size_t mi = 0; mi < this->meshCount; ++mi)
            {
                auto& mesh = meshes[mi];
                if (!mesh.Attributes) continue;
                for (size_t fi = 0; fi < mesh.FaceCount; ++fi) remapAttr(mesh.Attributes[fi]);
            }
        }
        else
        {
            auto* meshes = reinterpret_cast<SRL::Types::Mesh*>(this->meshes);
            for (size_t mi = 0; mi < this->meshCount; ++mi)
            {
                auto& mesh = meshes[mi];
                if (!mesh.Attributes) continue;
                for (size_t fi = 0; fi < mesh.FaceCount; ++fi) remapAttr(mesh.Attributes[fi]);
            }
        }
    }

    /** @brief Load flat mesh entry a partir do stream */
    bool LoadFlatMeshStream(SRL::Cd::File& file, size_t entryId)
    {
        MeshHeader meshHeader{};
        if (file.Read(sizeof(MeshHeader), &meshHeader) <= 0) return false;
        auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
        auto ReadBE16 = [](const uint8_t* p) -> uint16_t { return uint16_t(p[0]) << 8 | uint16_t(p[1]); };
        if (this->swapDataEndian)
        {
            meshHeader.PointCount = ReadBE32((uint8_t*)&meshHeader + 0);
            meshHeader.PolygonCount = ReadBE32((uint8_t*)&meshHeader + 4);
        }
        SRL::Types::Mesh mesh;
        mesh.VertexCount = meshHeader.PointCount;
        mesh.FaceCount = meshHeader.PolygonCount;
        if (this->forceHwrAlloc)
        {
            mesh.Vertices   = cartnew SRL::Math::Types::Vector3D[meshHeader.PointCount];
            mesh.Faces      = cartnew SRL::Types::Polygon[meshHeader.PolygonCount];
            mesh.Attributes = cartnew SRL::Types::Attribute[meshHeader.PolygonCount];
        }
        else
        {
            mesh.Vertices   = new SRL::Math::Types::Vector3D[meshHeader.PointCount];
            mesh.Faces      = new SRL::Types::Polygon[meshHeader.PolygonCount];
            mesh.Attributes = new SRL::Types::Attribute[meshHeader.PolygonCount];
        }

        if (!mesh.Vertices || !mesh.Faces || !mesh.Attributes)
        {
            MO_LOG(1, 6, "NYA alloc fail flat stream mesh:%lu v:%lu f:%lu",
                   (unsigned long)entryId,
                   (unsigned long)meshHeader.PointCount,
                   (unsigned long)meshHeader.PolygonCount);
            return false;
        }

        if (file.Read(sizeof(SRL::Math::Types::Vector3D) * meshHeader.PointCount, mesh.Vertices) <= 0) return false;
        if (this->swapDataEndian)
        {
            auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
            for (size_t v = 0; v < meshHeader.PointCount; ++v)
            {
                auto& p = mesh.Vertices[v];
                p.X = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.X)));
                p.Y = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Y)));
                p.Z = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Z)));
            }
        }
        if (file.Read(sizeof(SRL::Types::Polygon) * meshHeader.PolygonCount, mesh.Faces) <= 0) return false;
        if (this->swapDataEndian)
        {
            auto ReadBE16 = [](const uint8_t* p) -> uint16_t { return uint16_t(p[0]) << 8 | uint16_t(p[1]); };
            auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
            for (size_t f = 0; f < meshHeader.PolygonCount; ++f)
            {
                auto& poly = mesh.Faces[f];
                for (int vi = 0; vi < 4; ++vi)
                {
                    poly.Vertices[vi] = ReadBE16((uint8_t*)&poly.Vertices[vi]);
                }
                poly.Normal.X = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&poly.Normal.X)));
                poly.Normal.Y = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&poly.Normal.Y)));
                poly.Normal.Z = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&poly.Normal.Z)));
            }
        }

        for (size_t attributeIndex = 0; attributeIndex < meshHeader.PolygonCount; attributeIndex++)
        {
            FaceFlagsDisk flags{};
            if (file.Read(sizeof(FaceFlagsDisk), &flags) <= 0) return false;

            uint16_t color = this->swapDataEndian ? ReadBE16((const uint8_t*)&flags.BaseColor) : flags.BaseColor;
            uint16_t mode = CL32KRGB |
                            (flags.HasMeshEffect() ? MESHon : MESHoff) |
                            (flags.IsHalfTransparent() ? CL_Trans : 0) |
                            (flags.IsHalfBright() ? CL_Half : 0);
            uint16_t display = CL32KRGB;
            auto vis = flags.IsDoubleSided() ? SRL::Types::Attribute::FaceVisibility::DoubleSided : SRL::Types::Attribute::FaceVisibility::SingleSided;
            auto sort = SRL::Types::Attribute::SortMode::Center;
            auto spr = flags.IsWireframe() ? sprPolyLine : sprPolygon;

            mesh.Attributes[attributeIndex] = SRL::Types::Attribute(
                vis,
                sort,
                No_Texture,
                color,
                mode,
                display,
                spr,
                UseLight);
        }

        ((SRL::Types::Mesh*)this->meshes)[entryId] = std::move(mesh);
        return true;
    }

    void SkipFlatMeshBuffer(char** iterator)
    {
        MeshHeader* meshHeader = GetAndIterate<MeshHeader>(*iterator);
        auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
        if (this->swapDataEndian)
        {
            meshHeader->PointCount = ReadBE32((uint8_t*)meshHeader + 0);
            meshHeader->PolygonCount = ReadBE32((uint8_t*)meshHeader + 4);
        }
        GetAndIterate<SRL::Math::Types::Vector3D>(*iterator, meshHeader->PointCount);
        GetAndIterate<SRL::Types::Polygon>(*iterator, meshHeader->PolygonCount);
        GetAndIterate<FaceFlagsDisk>(*iterator, meshHeader->PolygonCount);
    }

    /** @brief Load flat mesh entry usando buffer na memria */
    bool LoadFlatMeshBuffer(char** iterator, size_t entryId)
    {
        MeshHeader* meshHeader = GetAndIterate<MeshHeader>(*iterator);
        auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
        auto ReadBE16 = [](const uint8_t* p) -> uint16_t { return uint16_t(p[0]) << 8 | uint16_t(p[1]); };
        if (this->swapDataEndian)
        {
            meshHeader->PointCount = ReadBE32((uint8_t*)meshHeader + 0);
            meshHeader->PolygonCount = ReadBE32((uint8_t*)meshHeader + 4);
        }

        SRL::Types::Mesh mesh;
        mesh.VertexCount = meshHeader->PointCount;
        mesh.FaceCount = meshHeader->PolygonCount;
        if (this->forceHwrAlloc)
        {
            mesh.Vertices   = cartnew SRL::Math::Types::Vector3D[meshHeader->PointCount];
            mesh.Faces      = cartnew SRL::Types::Polygon[meshHeader->PolygonCount];
            mesh.Attributes = cartnew SRL::Types::Attribute[meshHeader->PolygonCount];
        }
        else
        {
            mesh.Vertices   = new SRL::Math::Types::Vector3D[meshHeader->PointCount];
            mesh.Faces      = new SRL::Types::Polygon[meshHeader->PolygonCount];
            mesh.Attributes = new SRL::Types::Attribute[meshHeader->PolygonCount];
        }
        
        if (!mesh.Vertices || !mesh.Faces || !mesh.Attributes)
        {
            MO_LOG(1, 6, "NYA alloc fail flat buffer mesh:%lu v:%lu f:%lu",
                   (unsigned long)entryId,
                   (unsigned long)meshHeader->PointCount,
                   (unsigned long)meshHeader->PolygonCount);
            return false;
        }

        SRL::Math::Types::Vector3D* points = GetAndIterate<SRL::Math::Types::Vector3D>(*iterator, meshHeader->PointCount);
        slDMACopy(points, mesh.Vertices, sizeof(SRL::Math::Types::Vector3D) * meshHeader->PointCount);
        if (this->swapDataEndian)
        {
            auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
            for (size_t v = 0; v < meshHeader->PointCount; ++v)
            {
                auto& p = mesh.Vertices[v];
                p.X = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.X)));
                p.Y = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Y)));
                p.Z = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Z)));
            }
        }

        SRL::Types::Polygon* faces = GetAndIterate<SRL::Types::Polygon>(*iterator, meshHeader->PolygonCount);
        slDMACopy(faces, mesh.Faces, sizeof(SRL::Types::Polygon) * meshHeader->PolygonCount);

        for (size_t attributeIndex = 0; attributeIndex < meshHeader->PolygonCount; attributeIndex++)
        {
            FaceFlagsDisk* flags = GetAndIterate<FaceFlagsDisk>(*iterator);

            uint16_t color = this->swapDataEndian ? ReadBE16((const uint8_t*)&flags->BaseColor) : flags->BaseColor;
            uint16_t mode = CL32KRGB |
                            (flags->HasMeshEffect() ? MESHon : MESHoff) |
                            (flags->IsHalfTransparent() ? CL_Trans : 0) |
                            (flags->IsHalfBright() ? CL_Half : 0);
            uint16_t display = CL32KRGB;
            auto vis = flags->IsDoubleSided() ? SRL::Types::Attribute::FaceVisibility::DoubleSided : SRL::Types::Attribute::FaceVisibility::SingleSided;
            auto sort = SRL::Types::Attribute::SortMode::Center;
            auto spr = flags->IsWireframe() ? sprPolyLine : sprPolygon;

            mesh.Attributes[attributeIndex] = SRL::Types::Attribute(
                vis,
                sort,
                No_Texture,
                color,
                mode,
                display,
                spr,
                UseLight);
        }

        ((SRL::Types::Mesh*)this->meshes)[entryId] = std::move(mesh);
        return true;
    }

    /** @brief Load smooth mesh entry (stream) */
    bool LoadSmoothMeshStream(SRL::Cd::File& file, size_t* gouraudIterator, size_t entryId)
    {
        MeshHeader meshHeader{};
        if (file.Read(sizeof(MeshHeader), &meshHeader) <= 0) return false;
        auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
        auto ReadBE16 = [](const uint8_t* p) -> uint16_t { return uint16_t(p[0]) << 8 | uint16_t(p[1]); };
        if (this->swapDataEndian)
        {
            meshHeader.PointCount = ReadBE32((uint8_t*)&meshHeader + 0);
            meshHeader.PolygonCount = ReadBE32((uint8_t*)&meshHeader + 4);
        }
        uint16_t lastTextureIndex = SRL::VDP1::GetTextureCount();

        SRL::Types::SmoothMesh mesh;
        mesh.VertexCount = meshHeader.PointCount;
        mesh.FaceCount = meshHeader.PolygonCount;
        if (this->forceHwrAlloc)
        {
            mesh.Vertices   = cartnew SRL::Math::Types::Vector3D[meshHeader.PointCount];
            mesh.Faces      = cartnew SRL::Types::Polygon[meshHeader.PolygonCount];
            mesh.Attributes = cartnew SRL::Types::Attribute[meshHeader.PolygonCount];
            mesh.Normals    = cartnew SRL::Math::Types::Vector3D[meshHeader.PointCount];
        }
        else
        {
            mesh.Vertices   = new SRL::Math::Types::Vector3D[meshHeader.PointCount];
            mesh.Faces      = new SRL::Types::Polygon[meshHeader.PolygonCount];
            mesh.Attributes = new SRL::Types::Attribute[meshHeader.PolygonCount];
            mesh.Normals    = new SRL::Math::Types::Vector3D[meshHeader.PointCount];
        }

        if (!mesh.Vertices || !mesh.Faces || !mesh.Attributes || !mesh.Normals)
        {
            MO_LOG(1, 6, "NYA alloc fail smooth stream mesh:%lu v:%lu f:%lu",
                   (unsigned long)entryId,
                   (unsigned long)meshHeader.PointCount,
                   (unsigned long)meshHeader.PolygonCount);
            return false;
        }
        
        if (file.Read(sizeof(SRL::Math::Types::Vector3D) * meshHeader.PointCount, mesh.Vertices) <= 0) return false;
        if (this->swapDataEndian)
        {
            for (size_t v = 0; v < meshHeader.PointCount; ++v)
            {
                auto& p = mesh.Vertices[v];
                p.X = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.X)));
                p.Y = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Y)));
                p.Z = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Z)));
            }
        }
        if (file.Read(sizeof(SRL::Types::Polygon) * meshHeader.PolygonCount, mesh.Faces) <= 0) return false;
        if (this->swapDataEndian)
        {
            for (size_t f = 0; f < meshHeader.PolygonCount; ++f)
            {
                auto& poly = mesh.Faces[f];
                for (int vi = 0; vi < 4; ++vi)
                {
                    poly.Vertices[vi] = ReadBE16((uint8_t*)&poly.Vertices[vi]);
                }
                poly.Normal.X = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&poly.Normal.X)));
                poly.Normal.Y = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&poly.Normal.Y)));
                poly.Normal.Z = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&poly.Normal.Z)));
            }
        }

        std::vector<SmoothFaceFlagsRaw> rawFlags(meshHeader.PolygonCount);
        if (meshHeader.PolygonCount > 0)
        {
            const int32_t rawFlagsBytes = (int32_t)(sizeof(SmoothFaceFlagsRaw) * meshHeader.PolygonCount);
            if (file.Read(rawFlagsBytes, rawFlags.data()) != rawFlagsBytes) return false;
        }

        const SmoothFaceFlagsEncoding flagsEncoding = DetectSmoothFaceEncoding(rawFlags.data(), meshHeader.PolygonCount);

        for (size_t attributeIndex = 0; attributeIndex < meshHeader.PolygonCount; attributeIndex++)
        {
            const SmoothFaceFlagsRaw& raw = rawFlags[attributeIndex];
            const bool disk = (flagsEncoding == SmoothFaceFlagsEncoding::FaceFlagsDiskBits);

            const bool hasTexture = disk ? ((raw.Flags & 0x80) != 0) : ((raw.Flags & 0x01) != 0);
            const bool hasMeshEffect = disk ? ((raw.Flags & 0x40) != 0) : ((raw.Flags & 0x02) != 0);
            const bool isDoubleSided = disk ? ((raw.Flags & 0x20) != 0) : ((raw.Flags & 0x04) != 0);
            const bool hasTransparency = disk ? ((raw.Flags & 0x10) != 0) : ((raw.Flags & 0x08) != 0);
            const bool hasFlatShading = disk ? ((raw.Flags & 0x08) != 0) : ((raw.Flags & 0x10) != 0);
            const bool hasHalfBrightness = disk ? ((raw.Flags & 0x04) != 0) : ((raw.Flags & 0x20) != 0);
            const uint8_t sortMode = disk ? (raw.Flags & 0x03) : ((raw.Flags >> 6) & 0x03);
            const bool isWireframe = disk ? ((raw.Flags2 & 0x80) != 0) : ((raw.Flags2 & 0x01) != 0);

            const uint16_t baseColor = this->swapDataEndian ? ReadBE16((const uint8_t*)&raw.BaseColor) : raw.BaseColor;
            const int32_t textureId = this->swapDataEndian ? (int32_t)ReadBE32((const uint8_t*)&raw.TextureId) : raw.TextureId;

            uint16_t textureIndex = No_Texture;
            uint16_t color = baseColor;

            if (hasTexture && textureId >= 0)
            {
                textureIndex = lastTextureIndex + (uint16_t)textureId;
                color = No_Palet;
            }

            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wnarrowing"
            mesh.Attributes[attributeIndex] = SRL::Types::Attribute(
                isDoubleSided ? SRL::Types::Attribute::FaceVisibility::DoubleSided : SRL::Types::Attribute::FaceVisibility::SingleSided,
                (SRL::Types::Attribute::SortMode)(SRL::Types::Attribute::SortMode::Center - sortMode),
                textureIndex,
                color,
                (hasFlatShading ? CL32KRGB : *gouraudIterator),
                    CL32KRGB |
                    (hasMeshEffect ? MESHon : MESHoff) |
                    (hasFlatShading ? 0 : CL_Gouraud) |
                    (hasTransparency ? CL_Trans : 0) |
                    (hasHalfBrightness ? CL_Half : 0),
                (isWireframe ? sprPolyLine : (hasTexture ? sprNoflip : sprPolygon)),
                (hasFlatShading ? UseLight : UseGouraud));
            #pragma GCC diagnostic pop

            *gouraudIterator += 1;
        }

        if (file.Read(sizeof(SRL::Math::Types::Vector3D) * meshHeader.PointCount, mesh.Normals) <= 0) return false;
        if (this->swapDataEndian)
        {
            for (size_t n = 0; n < meshHeader.PointCount; ++n)
            {
                auto& p = mesh.Normals[n];
                p.X = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.X)));
                p.Y = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Y)));
                p.Z = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Z)));
            }
        }

        ((SRL::Types::SmoothMesh*)this->meshes)[entryId] = std::move(mesh);
        return true;
    }

    /** @brief Load smooth mesh entry usando buffer na memria */
    bool LoadSmoothMeshBuffer(char** iterator, size_t* gouraudIterator, size_t entryId)
    {
        MeshHeader* meshHeader = GetAndIterate<MeshHeader>(*iterator);
        auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
        auto ReadBE16 = [](const uint8_t* p) -> uint16_t { return uint16_t(p[0]) << 8 | uint16_t(p[1]); };
        if (this->swapDataEndian)
        {
            meshHeader->PointCount = ReadBE32((uint8_t*)meshHeader + 0);
            meshHeader->PolygonCount = ReadBE32((uint8_t*)meshHeader + 4);
        }
        uint16_t lastTextureIndex = SRL::VDP1::GetTextureCount();

        SRL::Types::SmoothMesh mesh;
        mesh.VertexCount = meshHeader->PointCount;
        mesh.FaceCount = meshHeader->PolygonCount;
        if (this->forceHwrAlloc)
        {
            mesh.Vertices   = cartnew SRL::Math::Types::Vector3D[meshHeader->PointCount];
            mesh.Faces      = cartnew SRL::Types::Polygon[meshHeader->PolygonCount];
            mesh.Attributes = cartnew SRL::Types::Attribute[meshHeader->PolygonCount];
            mesh.Normals    = cartnew SRL::Math::Types::Vector3D[meshHeader->PointCount];
        }
        else
        {
            mesh.Vertices   = new SRL::Math::Types::Vector3D[meshHeader->PointCount];
            mesh.Faces      = new SRL::Types::Polygon[meshHeader->PolygonCount];
            mesh.Attributes = new SRL::Types::Attribute[meshHeader->PolygonCount];
            mesh.Normals    = new SRL::Math::Types::Vector3D[meshHeader->PointCount];
        }
        
        if (!mesh.Vertices || !mesh.Faces || !mesh.Attributes || !mesh.Normals)
        {
            MO_LOG(1, 6, "NYA alloc fail smooth buffer mesh:%lu v:%lu f:%lu",
                   (unsigned long)entryId,
                   (unsigned long)meshHeader->PointCount,
                   (unsigned long)meshHeader->PolygonCount);
            return false;
        }

        SRL::Math::Types::Vector3D* points = GetAndIterate<SRL::Math::Types::Vector3D>(*iterator, meshHeader->PointCount);
        slDMACopy(points, mesh.Vertices, sizeof(SRL::Math::Types::Vector3D) * meshHeader->PointCount);
        if (this->swapDataEndian)
        {
            for (size_t v = 0; v < meshHeader->PointCount; ++v)
            {
                auto& p = mesh.Vertices[v];
                p.X = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.X)));
                p.Y = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Y)));
                p.Z = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Z)));
            }
        }

        SRL::Types::Polygon* faces = GetAndIterate<SRL::Types::Polygon>(*iterator, meshHeader->PolygonCount);
        slDMACopy(faces, mesh.Faces, sizeof(SRL::Types::Polygon) * meshHeader->PolygonCount);
        if (this->swapDataEndian)
        {
            auto ReadBE16 = [](const uint8_t* p) -> uint16_t { return uint16_t(p[0]) << 8 | uint16_t(p[1]); };
            auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
            for (size_t f = 0; f < meshHeader->PolygonCount; ++f)
            {
                auto& poly = mesh.Faces[f];
                for (int vi = 0; vi < 4; ++vi)
                {
                    poly.Vertices[vi] = ReadBE16((uint8_t*)&poly.Vertices[vi]);
                }
                poly.Normal.X = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&poly.Normal.X)));
                poly.Normal.Y = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&poly.Normal.Y)));
                poly.Normal.Z = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&poly.Normal.Z)));
            }
        }

        SmoothFaceFlagsRaw* rawFlags = GetAndIterate<SmoothFaceFlagsRaw>(*iterator, meshHeader->PolygonCount);
        const SmoothFaceFlagsEncoding flagsEncoding = DetectSmoothFaceEncoding(rawFlags, meshHeader->PolygonCount);

        for (size_t attributeIndex = 0; attributeIndex < meshHeader->PolygonCount; attributeIndex++)
        {
            const SmoothFaceFlagsRaw& raw = rawFlags[attributeIndex];
            const bool disk = (flagsEncoding == SmoothFaceFlagsEncoding::FaceFlagsDiskBits);

            const bool hasTexture = disk ? ((raw.Flags & 0x80) != 0) : ((raw.Flags & 0x01) != 0);
            const bool hasMeshEffect = disk ? ((raw.Flags & 0x40) != 0) : ((raw.Flags & 0x02) != 0);
            const bool isDoubleSided = disk ? ((raw.Flags & 0x20) != 0) : ((raw.Flags & 0x04) != 0);
            const bool hasTransparency = disk ? ((raw.Flags & 0x10) != 0) : ((raw.Flags & 0x08) != 0);
            const bool hasFlatShading = disk ? ((raw.Flags & 0x08) != 0) : ((raw.Flags & 0x10) != 0);
            const bool hasHalfBrightness = disk ? ((raw.Flags & 0x04) != 0) : ((raw.Flags & 0x20) != 0);
            const uint8_t sortMode = disk ? (raw.Flags & 0x03) : ((raw.Flags >> 6) & 0x03);
            const bool isWireframe = disk ? ((raw.Flags2 & 0x80) != 0) : ((raw.Flags2 & 0x01) != 0);

            const uint16_t baseColor = this->swapDataEndian ? ReadBE16((const uint8_t*)&raw.BaseColor) : raw.BaseColor;
            const int32_t textureId = this->swapDataEndian ? (int32_t)ReadBE32((const uint8_t*)&raw.TextureId) : raw.TextureId;

            uint16_t textureIndex = No_Texture;
            uint16_t color = baseColor;

            if (hasTexture && textureId >= 0)
            {
                textureIndex = lastTextureIndex + (uint16_t)textureId;
                color = No_Palet;
            }

            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wnarrowing"
            mesh.Attributes[attributeIndex] = SRL::Types::Attribute(
                isDoubleSided ? SRL::Types::Attribute::FaceVisibility::DoubleSided : SRL::Types::Attribute::FaceVisibility::SingleSided,
                (SRL::Types::Attribute::SortMode)(SRL::Types::Attribute::SortMode::Center - sortMode),
                textureIndex,
                color,
                (hasFlatShading ? CL32KRGB : *gouraudIterator),
                    CL32KRGB |
                    (hasMeshEffect ? MESHon : MESHoff) |
                    (hasFlatShading ? 0 : CL_Gouraud) |
                    (hasTransparency ? CL_Trans : 0) |
                    (hasHalfBrightness ? CL_Half : 0),
                (isWireframe ? sprPolyLine : (hasTexture ? sprNoflip : sprPolygon)),
                (hasFlatShading ? UseLight : UseGouraud));
            #pragma GCC diagnostic pop

            *gouraudIterator += 1;
        }

        SRL::Math::Types::Vector3D* vertexNormals = GetAndIterate<SRL::Math::Types::Vector3D>(*iterator, meshHeader->PointCount);
        slDMACopy(vertexNormals, mesh.Normals, sizeof(SRL::Math::Types::Vector3D) * meshHeader->PointCount);
        if (this->swapDataEndian)
        {
            for (size_t n = 0; n < meshHeader->PointCount; ++n)
            {
                auto& p = mesh.Normals[n];
                p.X = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.X)));
                p.Y = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Y)));
                p.Z = SRL::Math::Types::Fxp::BuildRaw(int32_t((uint32_t)ReadBE32((uint8_t*)&p.Z)));
            }
        }

        ((SRL::Types::SmoothMesh*)this->meshes)[entryId] = std::move(mesh);
        return true;
    }

    void SkipSmoothMeshBuffer(char** iterator, size_t* gouraudIterator)
    {
        MeshHeader* meshHeader = GetAndIterate<MeshHeader>(*iterator);
        auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
        if (this->swapDataEndian)
        {
            meshHeader->PointCount = ReadBE32((uint8_t*)meshHeader + 0);
            meshHeader->PolygonCount = ReadBE32((uint8_t*)meshHeader + 4);
        }
        GetAndIterate<SRL::Math::Types::Vector3D>(*iterator, meshHeader->PointCount);
        GetAndIterate<SRL::Types::Polygon>(*iterator, meshHeader->PolygonCount);
        GetAndIterate<Attribute>(*iterator, meshHeader->PolygonCount);
        GetAndIterate<SRL::Math::Types::Vector3D>(*iterator, meshHeader->PointCount);
        *gouraudIterator += meshHeader->PolygonCount;
    }

public:

    /** @brief Initializes a new model object from a file
     * @param modelFile Model file
     * @param gouraudTableStart Offset in gouraud table (used only with smooth meshes)
     */
    ModelObject(const char* modelFile, size_t gouraudTableStart = 0, bool firstMeshOnly = false, size_t maxMeshes = 0, bool forceBE = false, bool forceHwrAlloc = false, bool streamOnly = false)
    {
        this->firstMeshOnly = firstMeshOnly;
        this->forceBigEndian = forceBE;
        this->maxMeshesToLoad = maxMeshes;
        this->forceHwrAlloc = forceHwrAlloc;
        this->streamOnly = streamOnly;
        this->swapDataEndian = false;
        this->meshes = nullptr;
        this->meshCount = 0;
        this->textureCount = 0;
        this->type = 0;
        this->gouraudOffset = 0;
        this->startTextureIndex = -1;

        SRL::Cd::File file = SRL::Cd::File(modelFile);
        if (!file.Exists() || file.Size.Bytes <= 0)
        {
            MO_LOG(1, 6, "NYA not found: %s", modelFile);
            return;
        }

        // Tente primeiro via buffer (mais robusto). Se falhar ou se streamOnly, cai para streaming.
        if (!this->streamOnly)
        {
            if (this->LoadBuffer(modelFile, gouraudTableStart))
            {
                return;
            }
        }

        MO_LOG(1, 6, "NYA load strm: %s sz:%lu", modelFile, (unsigned long)file.Size.Bytes);
        this->LoadStreaming(modelFile, gouraudTableStart);
        // Se streaming falhou ou no produziu faces, tenta buffer (ainda alocando no cart se forceHwrAlloc)
        if (this->meshes == nullptr || this->meshCount == 0 || this->GetFaceCount() == 0)
        {
            // limpa estado mnimo
            this->meshes = nullptr;
            this->meshCount = 0;
            this->textureCount = 0;
            this->type = 0;
            this->gouraudOffset = 0;
            this->startTextureIndex = -1;
            this->LoadBuffer(modelFile, gouraudTableStart);
        }

    }

    bool LoadFromMemory(const void* buffer, size_t size, size_t gouraudTableStart = 0, bool firstMeshOnly = false, size_t maxMeshes = 0, bool forceBE = false, bool forceHwrAlloc = false)
    {
        if (!buffer || size == 0) return false;
        this->firstMeshOnly = firstMeshOnly;
        this->forceBigEndian = forceBE;
        this->maxMeshesToLoad = maxMeshes;
        this->forceHwrAlloc = forceHwrAlloc;
        this->streamOnly = false;
        this->swapDataEndian = false;
        this->meshes = nullptr;
        this->meshCount = 0;
        this->textureCount = 0;
        this->type = 0;
        this->gouraudOffset = 0;
        this->startTextureIndex = -1;

        return this->ParseBuffer(static_cast<const char*>(buffer), size, gouraudTableStart);
    }

private:

    /** @brief Carregamento compatvel (buffer completo) para modelos completos */
    bool LoadBuffer(const char* modelFile, size_t gouraudTableStart)
    {
        SRL::Cd::File f(modelFile);
        if (!f.Exists() || f.Size.Bytes <= 0) return false;
        bool bufInHwr = false;
        bool bufInCart = false;
        char* buf = nullptr;
        const bool forceCartMode = this->forceHwrAlloc || this->firstMeshOnly || this->maxMeshesToLoad > 0;
        if (forceCartMode)
        {
            if (void* c = SRL::Memory::CartRam::Malloc(f.Size.Bytes))
            {
                bufInCart = true;
                buf = static_cast<char*>(c);
                MO_LOG(1, 6, "NYA cart buffer alloc sz:%d ptr:%08lx", f.Size.Bytes, (unsigned long)buf);
            }
        }
        if (!buf)
        {
            if (void* hwr = SRL::Memory::HighWorkRam::Malloc(f.Size.Bytes))
            {
                bufInHwr = true;
                buf = static_cast<char*>(hwr);
            }
        }
        if (!buf)
        {
            buf = new char[f.Size.Bytes];
        }

        int32_t read = f.LoadBytes(0, f.Size.Bytes, buf);
        if (read != f.Size.Bytes)
        {
            MO_LOG(1, 6, "NYA buffer read fail: %s read:%d size:%d", modelFile, read, f.Size.Bytes);
            if (bufInHwr) SRL::Memory::HighWorkRam::Free(buf);
            else if (bufInCart) SRL::Memory::CartRam::Free(buf);
            else delete[] buf;
            return false;
        }

        bool ok = this->ParseBuffer(buf, f.Size.Bytes, gouraudTableStart);
        if (bufInHwr) SRL::Memory::HighWorkRam::Free(buf);
        else if (!bufInCart) delete[] buf; // mantm buffer no cart

        if (ok)
        {
            MO_LOG(1, 6, "NYA buffer ok: %s meshes:%lu tex:%lu type:%lu", modelFile,
                              (unsigned long)this->meshCount, (unsigned long)this->textureCount, (unsigned long)this->type);
        }
        else
        {
            if (bufInCart) SRL::Memory::CartRam::Free(buf);
            MO_LOG(1, 6, "NYA buffer parse fail: %s", modelFile);
        }
        return ok;
    }

    /** @brief Fallback de carregamento em streaming (leitura sequencial) */
    void LoadStreaming(const char* modelFile, size_t gouraudTableStart)
    {
        SRL::Cd::File file(modelFile);
        if (!file.Open())
        {
            MO_LOG(1, 6, "NYA open fail(stream): %s", modelFile);
            this->meshes = nullptr;
            this->meshCount = 0;
            this->textureCount = 0;
            this->type = 0;
            this->gouraudOffset = 0;
            this->startTextureIndex = -1;
            return;
        }

        ModelHeader header{};
        int32_t hdrRead = file.Read(sizeof(ModelHeader), &header);
        if (hdrRead != sizeof(ModelHeader))
        {
            MO_LOG(1, 6, "NYA hdr read fail(stream): %s read:%d", modelFile, hdrRead);
            file.Close();
            this->meshes = nullptr;
            this->meshCount = 0;
            this->textureCount = 0;
            this->type = 0;
            this->gouraudOffset = 0;
            this->startTextureIndex = -1;
            return;
        }

        auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
        auto ReadBE16 = [](const uint8_t* p) -> uint16_t { return uint16_t(p[0]) << 8 | uint16_t(p[1]); };
        uint8_t rawHdr[12];
        // Rewind and read raw header bytes for diagnstico
        file.Seek(0);
        file.Read(sizeof(rawHdr), rawHdr);
        uint32_t dbgTypeBE = ReadBE32(rawHdr + 0);
        uint32_t dbgMeshBE = ReadBE32(rawHdr + 4);
        uint32_t dbgTexBE  = ReadBE32(rawHdr + 8);
        this->swapDataEndian = (this->firstMeshOnly || this->forceBigEndian);
        if (this->firstMeshOnly)
        {
            // Pista: usa big-endian
            header.Type = dbgTypeBE;
            header.MeshCount = dbgMeshBE;
            header.TextureCount = dbgTexBE;
        }
        MO_LOG(1, 7, "NYA hdr BE strm type:%lu meshes:%lu tex:%lu",
                          (unsigned long)dbgTypeBE, (unsigned long)dbgMeshBE, (unsigned long)dbgTexBE);

        // Ajusta contagem conforme modo (pista ou carro)
        this->startTextureIndex = -1;
        this->textureCount = this->firstMeshOnly ? 0 : header.TextureCount;
        size_t targetMeshes = header.MeshCount;
        if (this->firstMeshOnly && targetMeshes > 1) targetMeshes = 1;
        if (this->maxMeshesToLoad > 0 && targetMeshes > this->maxMeshesToLoad) targetMeshes = this->maxMeshesToLoad;
        this->meshCount = targetMeshes;
        this->type = header.Type;
        this->gouraudOffset = gouraudTableStart;
        MO_LOG(1, 8, "NYA hdr final strm type:%lu meshes:%lu tex:%lu",
                          (unsigned long)this->type,
                          (unsigned long)this->meshCount,
                          (unsigned long)this->textureCount);
        // Reserve texture slot 0 before building face attributes.
        // Faces using tid=0 must map to a real texture index, not No_Texture.
        if (!this->firstMeshOnly && this->textureCount > 0)
        {
            EnsureTextureZeroReserved();
        }
        // Validao para evitar crash com headers invlidos
        const uint32_t texLimit = this->firstMeshOnly ? 200u : 1200u;
        if (this->firstMeshOnly)
        {
            const uint32_t texLimit = 200u;
            if (this->type > 1 || this->meshCount == 0 || this->meshCount > 400 || this->textureCount > texLimit)
            {
                MO_LOG(1, 6, "NYA invalid hdr(stream): type:%lu meshes:%lu textures:%lu",
                                  (unsigned long)this->type,
                                  (unsigned long)this->meshCount,
                                  (unsigned long)this->textureCount);
                file.Close();
                this->meshes = nullptr;
                this->meshCount = 0;
                this->textureCount = 0;
                this->type = 0;
                this->gouraudOffset = 0;
                this->startTextureIndex = -1;
                return;
            }
        }

        if (!this->firstMeshOnly)
        {
            bool invalid = (this->type > 1) || (this->meshCount == 0) || (this->meshCount > 400) || (this->textureCount > texLimit);
            if (invalid)
            {
                bool beValid = (dbgTypeBE <= 1) && (dbgMeshBE > 0 && dbgMeshBE <= 400) && (dbgTexBE <= texLimit);
                if (beValid)
                {
                    this->type = dbgTypeBE;
                    this->meshCount = dbgMeshBE;
                    this->textureCount = dbgTexBE;
                    this->swapDataEndian = true;
                }
                else
                {
                    MO_LOG(1, 6, "NYA invalid hdr(stream): type:%lu meshes:%lu textures:%lu",
                                      (unsigned long)this->type,
                                      (unsigned long)this->meshCount,
                                      (unsigned long)this->textureCount);
                    file.Close();
                    this->meshes = nullptr;
                    this->meshCount = 0;
                    this->textureCount = 0;
                    this->type = 0;
                    this->gouraudOffset = 0;
                    this->startTextureIndex = -1;
                    return;
                }
            }
        }
        size_t gouraudIterator = 0xe000 + this->gouraudOffset;

        const bool forceCart = this->forceHwrAlloc || this->firstMeshOnly || this->maxMeshesToLoad > 0;
        if (forceCart)
        {
            auto crep = SRL::Memory::CartRam::GetReport();
            size_t need = this->meshCount * (header.Type == 1 ? sizeof(SRL::Types::SmoothMesh) : sizeof(SRL::Types::Mesh));
            this->meshes = header.Type == 1
                ? (void*)cartnew SRL::Types::SmoothMesh[this->meshCount]
                : (void*)cartnew SRL::Types::Mesh[this->meshCount];
            MO_LOG(0, 20, "CRT m:%lu need:%lu free:%lu ptr:%08lx",
                              (unsigned long)this->meshCount, (unsigned long)need, (unsigned long)crep.FreeSize, (unsigned long)this->meshes);
        }
        else
        {
            this->meshes = header.Type == 1
                ? (void*)new SRL::Types::SmoothMesh[this->meshCount]
                : (void*)new SRL::Types::Mesh[this->meshCount];
        }

        bool ok = true;
        if (header.Type == 1)
        {
            for (size_t meshIndex = 0; meshIndex < this->meshCount && ok; meshIndex++)
            {
                ok = this->LoadSmoothMeshStream(file, &gouraudIterator, meshIndex);
            }
        }
        else
        {
            for (size_t meshIndex = 0; meshIndex < this->meshCount && ok; meshIndex++)
            {
                ok = this->LoadFlatMeshStream(file, meshIndex);
            }
        }

        if (!this->firstMeshOnly)
        {
            EnsureTextureZeroReserved();
            const size_t textureBase = SRL::VDP1::GetTextureCount();
            if (this->textureCount > 0)
            {
                this->startTextureIndex = static_cast<int32_t>(textureBase);
            }
            std::vector<int32_t> textureRemap(this->textureCount, -1);
            size_t uploadedRgb = 0;
            size_t uploadedPaletted = 0;
            size_t uploadedBytes = 0;
            for (size_t textureIndex = 0; ok && textureIndex < this->textureCount; textureIndex++)
            {
                TextureHeader textureHeader{};
                if (file.Read(sizeof(TextureHeader), &textureHeader) != sizeof(TextureHeader))
                {
                    ok = false;
                    break;
                }
                if (this->swapDataEndian)
                {
                    textureHeader.Width = ReadBE16((const uint8_t*)&textureHeader.Width);
                    textureHeader.Height = ReadBE16((const uint8_t*)&textureHeader.Height);
                }
                SRL::CRAM::TextureColorMode colorMode = SRL::CRAM::TextureColorMode::RGB555;
                uint16_t paletteId = 0;
                std::vector<uint8_t> texData;
                std::vector<SRL::Types::HighColor> paletteData;

                const size_t legacyBytes = TextureDataByteSize(textureHeader.Width, textureHeader.Height, SRL::CRAM::TextureColorMode::RGB555);
                uint8_t probe[4] = {0, 0, 0, 0};
                bool readProbe = false;
                if (legacyBytes >= 4)
                {
                    if (file.Read(4, probe) != 4)
                    {
                        ok = false;
                        break;
                    }
                    readProbe = true;
                }

                bool isV2Header = false;
                if (readProbe)
                {
                    const uint32_t magic =
                        (uint32_t(probe[0]) << 24) |
                        (uint32_t(probe[1]) << 16) |
                        (uint32_t(probe[2]) << 8) |
                        uint32_t(probe[3]);
                    isV2Header = (magic == kTextureHeaderV2Magic);
                }

                if (isV2Header)
                {
                    uint8_t rawV2[8]{};
                    if (file.Read(8, rawV2) != 8)
                    {
                        ok = false;
                        break;
                    }

                    TextureHeaderV2 headerV2 = DecodeTextureHeaderV2Raw(rawV2);
                    colorMode = DecodeTextureColorMode(headerV2.ColorMode);
                    uint16_t paletteColorCount = headerV2.PaletteColorCount;
                    paletteId = headerV2.PaletteId;

                    if (paletteColorCount > 0)
                    {
                        paletteData.resize(paletteColorCount);
                        const size_t paletteBytes = size_t(paletteColorCount) * sizeof(SRL::Types::HighColor);
                        if (file.Read((int32_t)paletteBytes, paletteData.data()) != (int32_t)paletteBytes)
                        {
                            ok = false;
                            break;
                        }
                    }

                    const size_t texBytes = TextureDataByteSize(textureHeader.Width, textureHeader.Height, colorMode);
                    texData.resize(texBytes);
                    if (texBytes > 0 && file.Read((int32_t)texBytes, texData.data()) != (int32_t)texBytes)
                    {
                        ok = false;
                        break;
                    }
                }
                else
                {
                    colorMode = SRL::CRAM::TextureColorMode::RGB555;
                    texData.resize(legacyBytes);
                    if (legacyBytes > 0)
                    {
                        size_t copied = 0;
                        if (readProbe)
                        {
                            copied = std::min<size_t>(4, legacyBytes);
                            for (size_t i = 0; i < copied; ++i) texData[i] = probe[i];
                        }
                        const size_t remain = legacyBytes - copied;
                        if (remain > 0 && file.Read((int32_t)remain, texData.data() + copied) != (int32_t)remain)
                        {
                            ok = false;
                            break;
                        }
                    }
                }

                if (IsPalettedMode(colorMode) && !paletteData.empty())
                {
                    int32_t bankId = AllocatePaletteBank(colorMode);
                    if (bankId >= 0)
                    {
                        SRL::CRAM::Palette palette(colorMode, static_cast<uint16_t>(bankId));
                        palette.Load(paletteData.data(), static_cast<int16_t>(paletteData.size()));
                        paletteId = static_cast<uint16_t>(bankId);
                    }
                    else
                    {
                        ok = false;
                        break;
                    }
                }

                textureRemap[textureIndex] = SRL::VDP1::TryLoadTexture(
                    textureHeader.Width,
                    textureHeader.Height,
                    colorMode,
                    paletteId,
                    texData.data());

                if (textureRemap[textureIndex] >= 0)
                {
                    uploadedBytes += texData.size();
                    if (IsPalettedMode(colorMode)) ++uploadedPaletted;
                    else ++uploadedRgb;
                }
                else
                {
                    MO_LOG(1, 6, "NYA tex upload fail(stream) tid:%lu %ux%u mode:%u pal:%u bytes:%lu",
                          (unsigned long)textureIndex,
                          textureHeader.Width,
                          textureHeader.Height,
                          (unsigned)colorMode,
                          (unsigned)paletteId,
                          (unsigned long)texData.size());
                }
            }
            if (ok && this->textureCount > 0)
            {
                this->RemapLoadedTextureIndices(textureBase, textureRemap);
                MO_LOG(1, 5, "NYA tex upload rgb:%lu pal:%lu bytes:%lu",
                      (unsigned long)uploadedRgb,
                      (unsigned long)uploadedPaletted,
                      (unsigned long)uploadedBytes);
            }
        }

        file.Close();

        if (!ok)
        {
            MO_LOG(1, 6, "NYA parse fail(stream): %s", modelFile);
            if (this->meshes)
            {
                if (this->type == 0) delete[] (SRL::Types::Mesh*)this->meshes;
                else delete[] (SRL::Types::SmoothMesh*)this->meshes;
            }
            this->meshes = nullptr;
            this->meshCount = 0;
            this->textureCount = 0;
            this->type = 0;
            this->gouraudOffset = 0;
            this->startTextureIndex = -1;
        }
        else
        {
            MO_LOG(1, 6, "NYA stream ok: %s meshes:%lu tex:%lu type:%lu", modelFile,
                              (unsigned long)this->meshCount, (unsigned long)this->textureCount, (unsigned long)this->type);
        }

        // Fallback: se no  modo firstMeshOnly e nada carregou, tenta leitura via buffer (compatvel com CAR1)
        if (!this->firstMeshOnly && (this->meshCount == 0 || this->meshes == nullptr))
        {
            MO_LOG(1, 6, "NYA fallback buffer load: %s", modelFile);
            this->LoadBuffer(modelFile, gouraudTableStart);
        }
    }

    bool ParseBuffer(const char* buf, size_t bufSize, size_t gouraudTableStart = 0)
    {
        if (!buf || bufSize == 0)
        {
            MO_LOG(1, 6, "NYA parse fail buffer: tamanho nulo");
            return false;
        }
        auto logFailure = [&](const char* reason)
        {
            MO_LOG(1, 6, "NYA parse fail buffer: %s", reason);
        };
        char* it = const_cast<char*>(buf);
        auto ReadBE32 = [](const uint8_t* p) -> uint32_t { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); };
        ModelHeader* header = GetAndIterate<ModelHeader>(it);
        this->swapDataEndian = (this->firstMeshOnly || this->forceBigEndian);
        if (this->firstMeshOnly || this->forceBigEndian)
        {
            header->Type = ReadBE32((const uint8_t*)buf + 0);
            header->MeshCount = ReadBE32((const uint8_t*)buf + 4);
            header->TextureCount = ReadBE32((const uint8_t*)buf + 8);
        }
        const uint32_t texLimit = this->firstMeshOnly ? 200u : 5000u;
        if (header->Type > 1 || header->MeshCount == 0 || header->MeshCount > 400 || header->TextureCount > texLimit)
        {
            uint32_t beType = ReadBE32((const uint8_t*)buf + 0);
            uint32_t beMesh = ReadBE32((const uint8_t*)buf + 4);
            uint32_t beTex  = ReadBE32((const uint8_t*)buf + 8);
            bool beValid = (beType <= 1) && (beMesh > 0 && beMesh <= 400) && (beTex <= texLimit);
            if (beValid)
            {
                header->Type = beType;
                header->MeshCount = beMesh;
                header->TextureCount = beTex;
                this->swapDataEndian = true;
            }
            else
            {
                logFailure("cabecalho invalidado (meshes/texturas fora do esperado)");
                return false;
            }
        }

        this->startTextureIndex = -1;
        this->textureCount = this->firstMeshOnly ? 0 : header->TextureCount;
        size_t originalMeshCount = header->MeshCount;
        size_t targetMeshCount = originalMeshCount;
        if (this->firstMeshOnly && targetMeshCount > 1) targetMeshCount = 1;
        if (this->maxMeshesToLoad > 0 && targetMeshCount > this->maxMeshesToLoad) targetMeshCount = this->maxMeshesToLoad;
        this->meshCount = targetMeshCount;
        this->type = header->Type;
        this->gouraudOffset = gouraudTableStart;
        size_t gouraudIterator = 0xe000 + this->gouraudOffset;
        // Reserve texture slot 0 before building face attributes.
        if (!this->firstMeshOnly && this->textureCount > 0)
        {
            EnsureTextureZeroReserved();
        }

        if (this->meshCount == 0)
        {
            logFailure("modelo nao possui meshes apos aplicacao de limite");
            return false;
        }

        const bool forceCart = this->forceHwrAlloc || this->firstMeshOnly || this->maxMeshesToLoad > 0;
        if (forceCart)
        {
            auto crep = SRL::Memory::CartRam::GetReport();
            size_t need = this->meshCount * (header->Type == 1 ? sizeof(SRL::Types::SmoothMesh) : sizeof(SRL::Types::Mesh));
            MO_LOG(1, 6, "NYA cart alloc m:%lu need:%lu free:%lu total:%lu",
                              (unsigned long)this->meshCount, (unsigned long)need, (unsigned long)crep.FreeSize, (unsigned long)crep.TotalSize);
            this->meshes = header->Type == 1
                ? (void*)cartnew SRL::Types::SmoothMesh[this->meshCount]
                : (void*)cartnew SRL::Types::Mesh[this->meshCount];
            if (!this->meshes)
            {
                logFailure("allocacao no cart 4MB falhou");
                return false;
            }
            MO_LOG(1, 6, "NYA cart alloc ptr:%08lx", (unsigned long)this->meshes);
        }
        else
        {
            this->meshes = header->Type == 1
                ? (void*)new SRL::Types::SmoothMesh[this->meshCount]
                : (void*)new SRL::Types::Mesh[this->meshCount];
        }

        bool ok = true;
        if (header->Type == 1)
        {
            for (size_t mi = 0; mi < originalMeshCount && ok; ++mi)
            {
                if (mi < this->meshCount)
                    ok = this->LoadSmoothMeshBuffer(&it, &gouraudIterator, mi);
                else
                    this->SkipSmoothMeshBuffer(&it, &gouraudIterator);
            }
        }
        else
        {
            for (size_t mi = 0; mi < originalMeshCount && ok; ++mi)
            {
                if (mi < this->meshCount)
                    ok = this->LoadFlatMeshBuffer(&it, mi);
                else
                    this->SkipFlatMeshBuffer(&it);
            }
        }

        EnsureTextureZeroReserved();
        size_t textureBase = SRL::VDP1::GetTextureCount();
        if (this->textureCount > 0)
        {
            this->startTextureIndex = static_cast<int32_t>(textureBase);
        }
        std::vector<int32_t> textureRemap(this->textureCount, -1);
        size_t uploadedRgb = 0;
        size_t uploadedPaletted = 0;
        size_t uploadedBytes = 0;
        for (size_t ti = 0; ok && ti < this->textureCount; ++ti)
        {
            if (it + sizeof(TextureHeader) > (buf + bufSize))
            {
                ok = false;
                break;
            }

            const uint16_t texW = uint16_t((uint8_t)it[0] << 8 | (uint8_t)it[1]);
            const uint16_t texH = uint16_t((uint8_t)it[2] << 8 | (uint8_t)it[3]);
            it += sizeof(TextureHeader);

            SRL::CRAM::TextureColorMode colorMode = SRL::CRAM::TextureColorMode::RGB555;
            uint16_t paletteId = 0;
            std::vector<SRL::Types::HighColor> paletteData;

            bool isV2Header = false;
            if (it + 4 <= (buf + bufSize))
            {
                const uint32_t magic =
                    (uint32_t((uint8_t)it[0]) << 24) |
                    (uint32_t((uint8_t)it[1]) << 16) |
                    (uint32_t((uint8_t)it[2]) << 8) |
                    uint32_t((uint8_t)it[3]);
                isV2Header = (magic == kTextureHeaderV2Magic);
            }

            if (isV2Header)
            {
                if (it + 12 > (buf + bufSize))
                {
                    ok = false;
                    break;
                }

                // skip magic
                it += 4;
                TextureHeaderV2 headerV2 = DecodeTextureHeaderV2Raw((const uint8_t*)it);
                colorMode = DecodeTextureColorMode(headerV2.ColorMode);
                uint16_t palCount = headerV2.PaletteColorCount;
                paletteId = headerV2.PaletteId;
                it += 8;

                if (palCount > 0)
                {
                    const size_t palBytes = size_t(palCount) * sizeof(SRL::Types::HighColor);
                    if (it + palBytes > (buf + bufSize))
                    {
                        ok = false;
                        break;
                    }
                    paletteData.resize(palCount);
                    for (size_t p = 0; p < palCount; ++p)
                    {
                        const uint8_t hi = (uint8_t)it[(p * 2) + 0];
                        const uint8_t lo = (uint8_t)it[(p * 2) + 1];
                        const uint16_t raw = uint16_t((hi << 8) | lo);
                        paletteData[p] = raw;
                    }
                    it += palBytes;
                }
            }

            if (IsPalettedMode(colorMode) && !paletteData.empty())
            {
                int32_t bankId = AllocatePaletteBank(colorMode);
                if (bankId >= 0)
                {
                    SRL::CRAM::Palette palette(colorMode, static_cast<uint16_t>(bankId));
                    palette.Load(paletteData.data(), static_cast<int16_t>(paletteData.size()));
                    paletteId = static_cast<uint16_t>(bankId);
                }
                else
                {
                    ok = false;
                    break;
                }
            }

            const size_t texBytes = TextureDataByteSize(texW, texH, colorMode);
            if (it + texBytes > (buf + bufSize))
            {
                ok = false;
                break;
            }
            textureRemap[ti] = SRL::VDP1::TryLoadTexture(
                texW,
                texH,
                colorMode,
                paletteId,
                (void*)it);
            it += texBytes;

            if (textureRemap[ti] >= 0)
            {
                uploadedBytes += texBytes;
                if (IsPalettedMode(colorMode)) ++uploadedPaletted;
                else ++uploadedRgb;
            }
            else
            {
                MO_LOG(1, 6, "NYA tex upload fail(buffer) tid:%lu %ux%u mode:%u pal:%u bytes:%lu",
                      (unsigned long)ti,
                      texW,
                      texH,
                      (unsigned)colorMode,
                      (unsigned)paletteId,
                      (unsigned long)texBytes);
            }
        }
        if (ok && this->textureCount > 0)
        {
            this->RemapLoadedTextureIndices(textureBase, textureRemap);
            MO_LOG(1, 5, "NYA tex upload rgb:%lu pal:%lu bytes:%lu",
                  (unsigned long)uploadedRgb,
                  (unsigned long)uploadedPaletted,
                  (unsigned long)uploadedBytes);
        }
        return ok;
    }

public:

    /** @brief Destroy the Model object and free its resources, textures must be freed separately
     */
    ~ModelObject()
    {
        if (this->type == 0)
        {
            delete[] (SRL::Types::Mesh*)this->meshes;
        }
        else
        {
            delete[] (SRL::Types::SmoothMesh*)this->meshes;
        }

        this->meshCount = 0;
    }

    /** @brief Draw specified mesh
     * @note Used only with flat type mesh data
     * @param mesh Mesh index
     */
    void Draw(size_t mesh)
    {
        if (mesh < this->meshCount && this->type == 0)
        {
            SRL::Scene3D::DrawMesh(((SRL::Types::Mesh*)this->meshes)[mesh]);
        }
    }

    /** @brief Draw specified mesh
     * @note Used only with smooth type mesh data
     * @param mesh Mesh index
     * @param light Light direction, used only with smooth type mesh data
     */
    void Draw(size_t mesh, SRL::Math::Types::Vector3D& light)
    {
        if (mesh < this->meshCount && this->type == 1)
        {
            SRL::Scene3D::DrawSmoothMesh(((SRL::Types::SmoothMesh*)this->meshes)[mesh], light);
        }
    }

    /** @brief Draw all loaded meshes
     * @note Used only with flat type mesh data
     */
    void Draw()
    {
        if (this->type == 0)
        {
            for (size_t mesh = 0; mesh < this->meshCount; mesh++)
            {
                SRL::Scene3D::DrawMesh(((SRL::Types::Mesh*)this->meshes)[mesh]);
            }
        }
    }

    /** @brief Draw all loaded meshes
     * @note Used only with smooth type mesh data
     * @param light Light direction
     */
    void Draw(SRL::Math::Types::Vector3D& light)
    {
        if (this->type == 1)
        {
            for (size_t mesh = 0; mesh < this->meshCount; mesh++)
            {
                SRL::Scene3D::DrawSmoothMesh(((SRL::Types::SmoothMesh*)this->meshes)[mesh], light);
            }
        }
    }

    /** @brief Gets number of loaded mesh faces
     * @return Number of loaded mesh faces
     */
    size_t GetFaceCount()
    {
        size_t result = 0;

        if (this->type == 1)
        {
            for (size_t mesh = 0; mesh < this->meshCount; mesh++)
            {
                result += ((SRL::Types::SmoothMesh*)this->meshes)[mesh].FaceCount;
            }
        }
        else if (this->type == 0)
        {
            for (size_t mesh = 0; mesh < this->meshCount; mesh++)
            {
                result += ((SRL::Types::Mesh*)this->meshes)[mesh].FaceCount;
            }
        }
        return result;
    }

    /** @brief Get index of the first texture loaded
     * @return Index of first texture or -1 if model has no textures
     */
    constexpr int32_t GetFirstTextureIndex()
    {
        return this->startTextureIndex;
    }

    /** @brief Ponteiro bruto para o bloco de meshes (para depuraSo/diagn!stico) */
    void* RawMeshesPtr() const { return this->meshes; }

    /** @brief Get the mesh data
     * @tparam ReturnValue SRL::Types::Mesh or SRL::Types::SmoothMesh
     * @param id Mesh id
     * @return Pointer to mesh data in specified type
     */
    template<typename ReturnValue>
    ReturnValue* GetMesh(size_t id)
    {
        static_assert(std::is_base_of<SRL::Types::SmoothMesh, ReturnValue>::value || std::is_base_of<SRL::Types::Mesh, ReturnValue>::value, "ReturnValue must inherit from SmoothMesh or Mesh");
        return &((ReturnValue*)this->meshes)[id];
    }

    /** @brief Gets number of loaded meshes
     * @return Number of loaded meshes
     */
    constexpr size_t GetMeshCount()
    {
        return this->meshCount;
    }
    
    /** @brief Gets number of loaded mesh vertices
     * @return Number of loaded mesh vertices
     */
    size_t GetVertexCount()
    {
        size_t result = 0;

        if (this->type == 1)
        {
            for (size_t mesh = 0; mesh < this->meshCount; mesh++)
            {
                result += ((SRL::Types::SmoothMesh*)this->meshes)[mesh].VertexCount;
            }
        }
        else if (this->type == 0)
        {
            for (size_t mesh = 0; mesh < this->meshCount; mesh++)
            {
                result += ((SRL::Types::Mesh*)this->meshes)[mesh].VertexCount;
            }
        }
        return result;
    }

    /** @brief Gets number of loaded textures */
    size_t GetTextureCount() const { return this->textureCount; }

    /** @brief Get a value indicating whether we are dealing with smooth mesh
     * @return true if its a smooth mesh
     */
    bool IsSmooth()
    {
        return this->type == 1;
    }

    /** @brief Fora todas as faces a usarem uma cor slida (sem textura)
     * @param color Cor desejada
     */
    void ForceSolidColor(const SRL::Types::HighColor& color)
    {
        if (!this->meshes) return;

        auto applyAttr = [&](SRL::Types::Attribute& attr)
        {
            attr.Texture = No_Texture;
            attr.ColorMode = color;
            // Usa cor slida, sem gouraud
            attr.Gouraud = CL32KRGB;
            // Mantm somente flags de transparncia/meia-luz/mesh, remove gouraud
            uint16_t keep = attr.Display & (CL_Trans | CL_Half | MESHon | MESHoff);
            attr.Display = CL32KRGB | keep;
            // Direo: fora clculo de luz simples
            attr.Direction = UseLight;
        };

        if (this->type == 0)
        {
            SRL::Types::Mesh* m = (SRL::Types::Mesh*)this->meshes;
            for (size_t mi = 0; mi < this->meshCount; ++mi)
            {
                for (size_t fi = 0; fi < m[mi].FaceCount; ++fi)
                {
                    applyAttr(m[mi].Attributes[fi]);
                }
            }
        }
        else
        {
            SRL::Types::SmoothMesh* m = (SRL::Types::SmoothMesh*)this->meshes;
            for (size_t mi = 0; mi < this->meshCount; ++mi)
            {
                for (size_t fi = 0; fi < m[mi].FaceCount; ++fi)
                {
                    applyAttr(m[mi].Attributes[fi]);
                }
            }
        }
    }

    /** @brief Variante que preserva flags de desenho e apenas remove Gouraud */
    void ForceSolidColorPreserveDisplay(const SRL::Types::HighColor& color)
    {
        if (!this->meshes) return;

        auto applyAttr = [&](SRL::Types::Attribute& attr)
        {
            attr.Texture = No_Texture;
            attr.ColorMode = color;
            attr.Gouraud = CL32KRGB;
            attr.Display = (attr.Display & ~CL_Gouraud) | CL32KRGB;
            attr.Direction = UseLight;
        };

        if (this->type == 0)
        {
            SRL::Types::Mesh* m = (SRL::Types::Mesh*)this->meshes;
            for (size_t mi = 0; mi < this->meshCount; ++mi)
            {
                for (size_t fi = 0; fi < m[mi].FaceCount; ++fi)
                {
                    applyAttr(m[mi].Attributes[fi]);
                }
            }
        }
        else
        {
            SRL::Types::SmoothMesh* m = (SRL::Types::SmoothMesh*)this->meshes;
            for (size_t mi = 0; mi < this->meshCount; ++mi)
            {
                for (size_t fi = 0; fi < m[mi].FaceCount; ++fi)
                {
                    applyAttr(m[mi].Attributes[fi]);
                }
            }
        }
    }

    /** @brief Force all mesh faces to be double sided */
    void ForceDoubleSided()
    {
        if (!this->meshes) return;

        auto applyAttr = [&](SRL::Types::Attribute& attr)
        {
            attr.Visibility = SRL::Types::Attribute::FaceVisibility::DoubleSided;
        };

        if (this->type == 0)
        {
            SRL::Types::Mesh* m = (SRL::Types::Mesh*)this->meshes;
            for (size_t mi = 0; mi < this->meshCount; ++mi)
            {
                if (!m[mi].Attributes) continue;
                for (size_t fi = 0; fi < m[mi].FaceCount; ++fi)
                {
                    applyAttr(m[mi].Attributes[fi]);
                }
            }
        }
        else
        {
            SRL::Types::SmoothMesh* m = (SRL::Types::SmoothMesh*)this->meshes;
            for (size_t mi = 0; mi < this->meshCount; ++mi)
            {
                if (!m[mi].Attributes) continue;
                for (size_t fi = 0; fi < m[mi].FaceCount; ++fi)
                {
                    applyAttr(m[mi].Attributes[fi]);
                }
            }
        }
    }
};
