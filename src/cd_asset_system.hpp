#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <srl.hpp>

namespace Game
{

class CdAssetSystem final
{
public:
    struct CarAnchorPoints
    {
        bool valid = false;
        SRL::Math::Types::Vector3D front{0.0, 0.0, 0.0};
        SRL::Math::Types::Vector3D rear{0.0, 0.0, 0.0};
    };

    static const char* FindExistingPath(const char* const* paths, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::File file(paths[i]);
            if (file.Exists() && file.Size.Bytes > 0)
            {
                return paths[i];
            }
        }
        return nullptr;
    }

    static bool ReadBinaryFileSimple(const char* path, std::vector<uint8_t>& outBytes)
    {
        outBytes.clear();
        if (!path || path[0] == '\0')
        {
            return false;
        }

        SRL::Cd::File file(path);
        if (!file.Exists() || file.Size.Bytes <= 0)
        {
            return false;
        }
        if (!file.Open())
        {
            return false;
        }

        const size_t size = static_cast<size_t>(file.Size.Bytes);
        if (size == 0u)
        {
            return false;
        }

        outBytes.resize(size);
        size_t totalRead = 0u;
        while (totalRead < size)
        {
            const int32_t want =
                static_cast<int32_t>(std::min<size_t>(2048u, size - totalRead));
            const int32_t got = file.Read(want, outBytes.data() + totalRead);
            if (got <= 0)
            {
                break;
            }
            totalRead += static_cast<size_t>(got);
            if (got < want)
            {
                break;
            }
        }

        if (totalRead != size)
        {
            outBytes.clear();
            return false;
        }
        return true;
    }

    static CarAnchorPoints LoadCarAnchorPoints()
    {
        const char* candidates[] = {
            "CD/DATA/CAR1_ANCHORS.JSON;1",
            "CD/DATA/CAR1_ANCHORS.JSON",
            "DATA/CAR1_ANCHORS.JSON;1",
            "DATA/CAR1_ANCHORS.JSON",
            "CAR1_ANCHORS.JSON;1",
            "CAR1_ANCHORS.JSON",
            "cd/data/CAR1_ANCHORS.JSON",
            "cd/data/CAR1_ANCHORS.json",
            "data/CAR1_ANCHORS.JSON",
            "data/CAR1_ANCHORS.json",
            "CAR1_ANCHORS.json"
        };

        std::vector<uint8_t> bytes{};
        for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i)
        {
            if (!ReadBinaryFileSimple(candidates[i], bytes))
            {
                continue;
            }

            std::vector<char> text(bytes.begin(), bytes.end());
            text.push_back('\0');

            CarAnchorPoints anchors{};
            if (!ParseJsonVec3ByKey(text.data(), "\"front\"", anchors.front))
            {
                continue;
            }
            if (!ParseJsonVec3ByKey(text.data(), "\"rear\"", anchors.rear))
            {
                continue;
            }
            anchors.valid = true;
            return anchors;
        }

        return {};
    }

    static const char* FindSbaShadowModelPath()
    {
        const char* candidates[] = {
            "CD/DATA/SBA.NYA;1",
            "CD/DATA/SBA.NYA",
            "DATA/SBA.NYA;1",
            "DATA/SBA.NYA",
            "SBA.NYA;1",
            "SBA.NYA",
            "cd/data/sba.nya",
            "data/sba.nya"
        };
        return FindExistingPath(candidates, sizeof(candidates) / sizeof(candidates[0]));
    }

private:
    static const char* SkipWs(const char* p)
    {
        while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        {
            ++p;
        }
        return p;
    }

    static bool ParseJsonVec3ByKey(const char* json,
                                   const char* key,
                                   SRL::Math::Types::Vector3D& outVec)
    {
        if (!json || !key)
        {
            return false;
        }

        const char* foundKey = ::strstr(json, key);
        if (!foundKey)
        {
            return false;
        }
        const char* leftBracket = ::strchr(foundKey, '[');
        const char* rightBracket = leftBracket ? ::strchr(leftBracket, ']') : nullptr;
        if (!leftBracket || !rightBracket || rightBracket <= leftBracket)
        {
            return false;
        }

        const char* p = SkipWs(leftBracket + 1);
        char* end = nullptr;
        const float x = std::strtof(p, &end);
        if (end == p)
        {
            return false;
        }
        p = SkipWs(end);
        if (*p == ',')
        {
            ++p;
        }

        p = SkipWs(p);
        const float y = std::strtof(p, &end);
        if (end == p)
        {
            return false;
        }
        p = SkipWs(end);
        if (*p == ',')
        {
            ++p;
        }

        p = SkipWs(p);
        const float z = std::strtof(p, &end);
        if (end == p)
        {
            return false;
        }

        auto toRaw = [](float value) -> int32_t
        {
            const float scaled = value * 65536.0f;
            return static_cast<int32_t>(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
        };

        outVec = SRL::Math::Types::Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(toRaw(x)),
            SRL::Math::Types::Fxp::BuildRaw(toRaw(y)),
            SRL::Math::Types::Fxp::BuildRaw(toRaw(z)));
        return true;
    }
};

} // namespace Game
