#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

template <size_t Capacity, typename T>
class TrackSegmentPool
{
public:
    struct Handle
    {
        uint16_t slot = 0xFFFF;
        uint16_t generation = 0;
    };

    struct Slot
    {
        bool occupied = false;
        uint16_t generation = 0;
        T* value = nullptr;
    };

    void Reset()
    {
        size_ = 0;
        for (auto& slot : slots_)
        {
            slot.occupied = false;
            slot.value = nullptr;
            ++slot.generation;
        }
    }

    Handle Add(T* value)
    {
        if (!value) return {};
        for (size_t i = 0; i < Capacity; ++i)
        {
            auto& slot = slots_[i];
            if (slot.occupied) continue;
            slot.occupied = true;
            slot.value = value;
            ++size_;
            return Handle{ static_cast<uint16_t>(i), slot.generation };
        }
        return {};
    }

    T* Resolve(Handle handle)
    {
        if (handle.slot >= Capacity) return nullptr;
        auto& slot = slots_[handle.slot];
        if (!slot.occupied) return nullptr;
        if (slot.generation != handle.generation) return nullptr;
        return slot.value;
    }

    const T* Resolve(Handle handle) const
    {
        if (handle.slot >= Capacity) return nullptr;
        const auto& slot = slots_[handle.slot];
        if (!slot.occupied) return nullptr;
        if (slot.generation != handle.generation) return nullptr;
        return slot.value;
    }

    size_t Size() const { return size_; }

private:
    std::array<Slot, Capacity> slots_{};
    size_t size_ = 0;
};
