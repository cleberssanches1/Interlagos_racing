#pragma once

#include <cstddef>

struct TrackSerializedCopy
{
    void* cartPtr = nullptr;
    size_t size = 0;
    int32_t hwrDelta = 0;

    bool Valid() const { return cartPtr != nullptr && size > 0; }
};
