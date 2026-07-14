#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

template <typename Handle, size_t Capacity>
struct TrackDrawList
{
    std::array<Handle, Capacity> items{};
    uint16_t count = 0;

    void Clear()
    {
        count = 0;
    }

    bool Push(Handle handle)
    {
        if (count >= Capacity) return false;
        items[count++] = handle;
        return true;
    }
};

template <typename Handle, size_t Capacity>
class TrackDrawListAB
{
public:
    void BuildWriteList(const std::vector<Handle>& handles, size_t limit)
    {
        BuildWriteList(handles.data(), handles.size(), limit);
    }

    void BuildWriteList(const Handle* handles, size_t count, size_t limit)
    {
        auto& writeList = lists_[writeIdx_];
        writeList.Clear();
        if (!handles || count == 0u)
        {
            return;
        }
        const size_t maxCount = std::min(limit, count);
        for (size_t i = 0; i < maxCount; ++i)
        {
            if (!writeList.Push(handles[i])) break;
        }
    }

    void Publish()
    {
        const uint8_t prevRead = readIdx_;
        readIdx_ = writeIdx_;
        writeIdx_ = prevRead;
    }

    const TrackDrawList<Handle, Capacity>& ReadList() const
    {
        return lists_[readIdx_];
    }

private:
    TrackDrawList<Handle, Capacity> lists_[2]{};
    uint8_t readIdx_ = 0;
    uint8_t writeIdx_ = 1;
};
