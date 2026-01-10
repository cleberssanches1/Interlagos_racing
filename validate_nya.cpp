#include <cstdint>
#include <cstdio>
#include <vector>
#include <string_view>

// Estruturas mínimas, espelhando o loader (srL) para estimar stride real
struct Vec3 { int32_t x, y, z; }; // Fxp (32-bit)
struct Poly { Vec3 normal; uint16_t v[4]; }; // ~20 bytes
// Attribute real (FaceFlags): Flags (1) + Flags2 (1) + BaseColor (2) + TextureId (4) = 8 bytes
constexpr size_t kAttrSize = 8;

static uint32_t ReadBE32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }
static uint32_t ReadLE32(const uint8_t* p) { return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[0]); }

static void Validate(const char* path)
{
    std::FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("%s : not found\n", path); return; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(size);
    if (std::fread(data.data(), 1, data.size(), f) != data.size()) { std::printf("%s : read fail\n", path); std::fclose(f); return; }
    std::fclose(f);

    if (data.size() < 12) { std::printf("%s : too small\n", path); return; }

    uint32_t typeBE = ReadBE32(&data[0]);
    uint32_t meshBE = ReadBE32(&data[4]);
    uint32_t texBE  = ReadBE32(&data[8]);
    uint32_t typeLE = ReadLE32(&data[0]);
    uint32_t meshLE = ReadLE32(&data[4]);
    uint32_t texLE  = ReadLE32(&data[8]);

    std::printf("%s\n", path);
    std::printf("  size: %ld bytes\n", size);
    std::printf("  header BE type=%u meshes=%u textures=%u\n", typeBE, meshBE, texBE);
    std::printf("  header LE type=%u meshes=%u textures=%u\n", typeLE, meshLE, texLE);

    uint32_t type = typeBE;
    uint32_t meshCount = meshBE;
    uint32_t texCount = texBE;
    auto plausible = [](uint32_t t, uint32_t m, uint32_t tex) {
        return (t <= 1) && (m > 0 && m <= 400) && (tex <= 2000);
    };
    if (!plausible(typeBE, meshBE, texBE) && plausible(typeLE, meshLE, texLE)) {
        type = typeLE; meshCount = meshLE; texCount = texLE;
        std::printf("  using LE header (BE implausível)\n");
    }

    auto calc_need = [&](uint32_t pts, uint32_t polys) -> size_t {
        size_t needVerts = size_t(pts) * sizeof(Vec3);          // pontos
        size_t needPolys = size_t(polys) * sizeof(Poly);        // faces
        size_t needAttr  = size_t(polys) * kAttrSize;           // FaceFlags
        size_t needNorms = (type == 1) ? size_t(pts) * sizeof(Vec3) : 0; // smooth tem normals de vértice
        return needVerts + needPolys + needAttr + needNorms;
    };

    size_t off = 12;
    size_t parsed = 0;
    const size_t maxCheck = 32;
    for (uint32_t mi = 0; mi < meshCount && parsed < maxCheck; ++mi, ++parsed)
    {
        if (off + 8 > data.size()) { std::printf("  mesh %u header OOB @%zu\n", mi, off); break; }
        uint32_t ptsBE = ReadBE32(&data[off + 0]);
        uint32_t polysBE  = ReadBE32(&data[off + 4]);
        uint32_t ptsLE = ReadLE32(&data[off + 0]);
        uint32_t polysLE  = ReadLE32(&data[off + 4]);

        struct Cand { uint32_t pts; uint32_t polys; size_t need; const char* endian; };
        std::vector<Cand> cands;
        auto add = [&](uint32_t pts, uint32_t polys, const char* e)
        {
            if (pts == 0 || polys == 0) return;
            if (pts > 1000000 || polys > 1000000) return;
            size_t need = calc_need(pts, polys);
            if (off + 8 + need > data.size()) return;
            cands.push_back({pts, polys, need, e});
        };

        // Prioriza counts na mesma endianness do header
        if (type == typeBE) { add(ptsBE, polysBE, "BE"); add(ptsLE, polysLE, "LE"); }
        else { add(ptsLE, polysLE, "LE"); add(ptsBE, polysBE, "BE"); }

        if (cands.empty())
        {
            std::printf("  mesh %u truncado/absurdo: BE pts=%u polys=%u LE pts=%u polys=%u off=%zu\n",
                        mi, ptsBE, polysBE, ptsLE, polysLE, off);
            break;
        }

        const auto& chosen = cands.front();
        std::printf("  mesh %u: %s pts=%u polys=%u need=%zu off=%zu\n",
                    mi, chosen.endian, chosen.pts, chosen.polys, chosen.need, off);
        off += 8;
        off += chosen.need;
    }
    std::printf("  parsed %zu meshes, final off=%zu of %zu bytes\n\n", parsed, off, data.size());
}

int main(int argc, char** argv)
{
    if (argc <= 1) {
        Validate("cd/data/INTLAGOS_PDATA.NYA");
    } else {
        for (int i = 1; i < argc; ++i) Validate(argv[i]);
    }
    return 0;
}
