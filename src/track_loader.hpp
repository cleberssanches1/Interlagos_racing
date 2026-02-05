#pragma once

#include <cstddef>

#include <srl.hpp>

#include "resource_loader.hpp"

namespace Game
{
class TrackLoader
{
public:
    struct Result
    {
        bool valid{false};
        TrackSerializedCopy serialized;
    };

    TrackLoader(const char* const* paths, size_t count)
        : paths_(paths)
        , pathCount_(count)
    {}

    Result Load()
    {
        Result result{};
        const char* trackPath = FindExistingPath();
        if (!trackPath)
        {
            return result;
        }
        result.serialized = SerializeTrackToCart(trackPath);
        result.valid = result.serialized.Valid();
        return result;
    }

private:
    const char* const* paths_;
    size_t pathCount_;

    const char* FindExistingPath() const
    {
        for (size_t i = 0; i < pathCount_; ++i)
        {
            SRL::Cd::File f(paths_[i]);
            if (f.Exists() && f.Size.Bytes > 0)
            {
                return paths_[i];
            }
        }
        return nullptr;
    }
};
} // namespace Game
