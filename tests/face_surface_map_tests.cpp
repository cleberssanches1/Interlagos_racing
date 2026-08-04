#include <cstdint>
#include <iostream>
#include <vector>

#include "face_surface_map.hpp"

namespace
{
int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { \
    std::cerr << __LINE__ << ": expected " #expr "\n"; ++failures; } } while (0)

void Put16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8u);
}

void Put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8u));
}

std::vector<uint8_t> MakeMap()
{
    std::vector<uint8_t> bytes(20u + 24u + 12u, 0u);
    Put32(bytes, 0u, FaceSurfaceMap::kMagic);
    Put16(bytes, 4u, FaceSurfaceMap::kVersion);
    Put16(bytes, 6u, FaceSurfaceMap::kHeaderSize);
    Put16(bytes, 8u, 1u);
    Put16(bytes, 10u, FaceSurfaceMap::kRecordSize);
    Put32(bytes, 12u, 1u);
    Put32(bytes, 16u, 20u);

    Put16(bytes, 20u, 22u);
    Put16(bytes, 22u, 73u);
    Put16(bytes, 24u, 1u);
    bytes[26u] = 12u;
    bytes[27u] = 1u;
    Put32(bytes, 28u, static_cast<uint32_t>(-1000));
    Put32(bytes, 32u, 2000u);
    Put32(bytes, 36u, 44u);

    Put16(bytes, 44u, 17u);
    Put16(bytes, 46u, static_cast<uint16_t>(-2));
    Put16(bytes, 48u, 3u);
    Put16(bytes, 50u, static_cast<uint16_t>(-1));
    Put16(bytes, 52u, 4u);
    bytes[54u] = 1u;
    bytes[55u] = 0x03u;
    return bytes;
}

void TestParseLookupAndBounds()
{
    const std::vector<uint8_t> bytes = MakeMap();
    const FaceSurfaceMap::View view(bytes.data(), bytes.size());
    EXPECT_TRUE(view.Valid());
    FaceSurfaceMap::SegmentView segment{};
    EXPECT_TRUE(view.FindSegment(22u, segment));
    EXPECT_TRUE(segment.faceCount == 73u);
    EXPECT_TRUE(!view.FindSegment(23u, segment));
    EXPECT_TRUE(view.FindSegment(22u, segment));
    FaceSurfaceMap::FaceRecord face{};
    EXPECT_TRUE(view.ReadRecord(segment, 0u, face));
    EXPECT_TRUE(face.faceIndex == 17u);
    EXPECT_TRUE(face.surfaceType == 1u);
    EXPECT_TRUE(FaceSurfaceMap::View::ContainsXZ(segment, face, -1000, 2000));
    EXPECT_TRUE(!FaceSurfaceMap::View::ContainsXZ(segment, face, 20000, 2000));
    EXPECT_TRUE(!view.ReadRecord(segment, 1u, face));
}

void TestRejectsTruncatedMap()
{
    std::vector<uint8_t> bytes = MakeMap();
    bytes.resize(44u);
    const FaceSurfaceMap::View view(bytes.data(), bytes.size());
    EXPECT_TRUE(view.Valid());
    FaceSurfaceMap::SegmentView segment{};
    EXPECT_TRUE(!view.FindSegment(22u, segment));
}
} // namespace

int main()
{
    TestParseLookupAndBounds();
    TestRejectsTruncatedMap();
    if (failures != 0)
    {
        std::cerr << failures << " face surface map test(s) failed\n";
        return 1;
    }
    std::cout << "face surface map tests passed\n";
    return 0;
}
