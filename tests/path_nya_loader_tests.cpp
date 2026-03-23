#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "path_nya_loader.hpp"

namespace
{
struct TestContext
{
    int failures = 0;

    void Fail(const char* expr,
              const char* file,
              int line,
              const std::string& detail = std::string())
    {
        std::cerr << file << ":" << line << " failure: " << expr;
        if (!detail.empty()) std::cerr << " [" << detail << "]";
        std::cerr << "\n";
        ++failures;
    }
};

template <typename T>
std::string ToString(const T& value)
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

#define EXPECT_TRUE(ctx, expr) \
    do \
    { \
        if (!(expr)) (ctx).Fail(#expr, __FILE__, __LINE__); \
    } while (0)

#define EXPECT_EQ(ctx, actual, expected) \
    do \
    { \
        const auto actualValue = (actual); \
        const auto expectedValue = (expected); \
        if (!(actualValue == expectedValue)) \
        { \
            (ctx).Fail(#actual " == " #expected, \
                       __FILE__, \
                       __LINE__, \
                       std::string("actual=") + ToString(actualValue) + \
                       " expected=" + ToString(expectedValue)); \
        } \
    } while (0)

void AppendBe32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
}

void AppendPointBe(std::vector<uint8_t>& bytes, int32_t xRaw, int32_t yRaw, int32_t zRaw)
{
    AppendBe32(bytes, static_cast<uint32_t>(xRaw));
    AppendBe32(bytes, static_cast<uint32_t>(yRaw));
    AppendBe32(bytes, static_cast<uint32_t>(zRaw));
}

void TestParseBigEndianThreeLines(TestContext& ctx)
{
    static constexpr int32_t kRideHeightRaw = -(3 * (1 << 16));
    std::vector<uint8_t> bytes{};
    AppendBe32(bytes, 1u);
    AppendBe32(bytes, 3u);
    AppendBe32(bytes, 1u);
    AppendBe32(bytes, 32u);
    AppendBe32(bytes, 2u);
    AppendBe32(bytes, 44u);
    AppendBe32(bytes, 1u);
    AppendBe32(bytes, 68u);

    AppendPointBe(bytes, 10 << 16, kRideHeightRaw, 20 << 16);
    AppendPointBe(bytes, 30 << 16, kRideHeightRaw, 40 << 16);
    AppendPointBe(bytes, 31 << 16, kRideHeightRaw, 41 << 16);
    AppendPointBe(bytes, 50 << 16, kRideHeightRaw, 60 << 16);

    PathNya::ParseResult parsed{};
    EXPECT_TRUE(ctx, PathNya::Parse(bytes.data(), bytes.size(), parsed));
    EXPECT_EQ(ctx, parsed.version, 1u);
    EXPECT_EQ(ctx, parsed.lineCount, 3u);
    EXPECT_TRUE(ctx, parsed.bigEndian);
    EXPECT_EQ(ctx, parsed.lines[0].size(), static_cast<size_t>(1u));
    EXPECT_EQ(ctx, parsed.lines[1].size(), static_cast<size_t>(2u));
    EXPECT_EQ(ctx, parsed.lines[2].size(), static_cast<size_t>(1u));
    EXPECT_EQ(ctx, parsed.lines[1][0].xRaw, 30 << 16);
    EXPECT_EQ(ctx, parsed.lines[1][1].zRaw, 41 << 16);
    EXPECT_TRUE(ctx, PathNya::HasUsableLine(parsed, 1u));
}

void TestParseEmptyHeaderOnlyFile(TestContext& ctx)
{
    const std::vector<uint8_t> bytes{
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    PathNya::ParseResult parsed{};
    EXPECT_TRUE(ctx, PathNya::Parse(bytes.data(), bytes.size(), parsed));
    EXPECT_EQ(ctx, parsed.version, 1u);
    EXPECT_EQ(ctx, parsed.lineCount, 3u);
    EXPECT_EQ(ctx, parsed.lines[0].size(), static_cast<size_t>(0u));
    EXPECT_EQ(ctx, parsed.lines[1].size(), static_cast<size_t>(0u));
    EXPECT_EQ(ctx, parsed.lines[2].size(), static_cast<size_t>(0u));
    EXPECT_TRUE(ctx, !PathNya::HasUsableLine(parsed, 1u));
}

struct TestCase
{
    const char* name = "";
    void (*fn)(TestContext&) = nullptr;
};
}

int main()
{
    const std::vector<TestCase> tests{
        {"ParseBigEndianThreeLines", &TestParseBigEndianThreeLines},
        {"ParseEmptyHeaderOnlyFile", &TestParseEmptyHeaderOnlyFile},
    };

    TestContext ctx{};
    size_t passed = 0u;
    for (size_t i = 0; i < tests.size(); ++i)
    {
        try
        {
            tests[i].fn(ctx);
            if (ctx.failures == 0)
            {
                ++passed;
                std::cout << "[PASS] " << tests[i].name << "\n";
            }
            else
            {
                std::cout << "[FAIL] " << tests[i].name << "\n";
                return 1;
            }
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[EXCEPTION] " << tests[i].name << ": " << ex.what() << "\n";
            return 1;
        }
        catch (...)
        {
            std::cerr << "[EXCEPTION] " << tests[i].name << ": unknown\n";
            return 1;
        }
    }

    std::cout << "Passed " << passed << " test(s)\n";
    return 0;
}
