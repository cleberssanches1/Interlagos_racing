#pragma once

#include <vector>

#include <srl.hpp>

template <typename T, SRL::Memory::Zone ZoneValue>
struct TrackZoneAllocator
{
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

    TrackZoneAllocator() noexcept = default;

    template <typename U>
    TrackZoneAllocator(const TrackZoneAllocator<U, ZoneValue>&) noexcept {}

    T* allocate(std::size_t n)
    {
        if (n == 0) return nullptr;
        return static_cast<T*>(SRL::Memory::Malloc(n * sizeof(T), ZoneValue));
    }

    void deallocate(T* p, std::size_t) noexcept
    {
        SRL::Memory::Free(p);
    }

    template <typename U>
    struct rebind
    {
        using other = TrackZoneAllocator<U, ZoneValue>;
    };
};

template <typename T, typename U, SRL::Memory::Zone ZoneValue>
inline bool operator==(const TrackZoneAllocator<T, ZoneValue>&,
                       const TrackZoneAllocator<U, ZoneValue>&) noexcept
{
    return true;
}

template <typename T, typename U, SRL::Memory::Zone ZoneValue>
inline bool operator!=(const TrackZoneAllocator<T, ZoneValue>&,
                       const TrackZoneAllocator<U, ZoneValue>&) noexcept
{
    return false;
}

template <typename T>
using TrackLowWorkVectorBase =
    std::vector<T, TrackZoneAllocator<T, SRL::Memory::Zone::LWRam>>;

template <typename T>
using TrackHighWorkVectorBase =
    std::vector<T, TrackZoneAllocator<T, SRL::Memory::Zone::HWRam>>;
