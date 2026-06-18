#pragma once

#include <cstddef>
#include <cstdint>

#include <srl.hpp>

namespace Game
{

class MemoryBudgetSystem final
{
public:
    enum class ConsumerCategory : uint8_t
    {
        TrackRender = 0,
        CarRender,
        AudioPcm,
        Hud,
        CdStaging,
        DebugTransient
    };

    enum class AllocationPool : uint8_t
    {
        LowWork = 0,
        HighWork,
        Cart
    };

    struct Snapshot
    {
        uint32_t highWorkFree = 0u;
        uint32_t highWorkTotal = 0u;
        uint32_t lowWorkFree = 0u;
        uint32_t lowWorkTotal = 0u;
        uint32_t cartFree = 0u;
        uint32_t cartTotal = 0u;
    };

    static Snapshot CaptureSnapshot()
    {
        const auto highWork = SRL::Memory::HighWorkRam::GetReport();
        const auto lowWork = SRL::Memory::LowWorkRam::GetReport();
        const auto cart = SRL::Memory::CartRam::GetReport();

        Snapshot snapshot{};
        snapshot.highWorkFree = highWork.FreeSize;
        snapshot.highWorkTotal = highWork.TotalSize;
        snapshot.lowWorkFree = lowWork.FreeSize;
        snapshot.lowWorkTotal = lowWork.TotalSize;
        snapshot.cartFree = cart.FreeSize;
        snapshot.cartTotal = cart.TotalSize;
        return snapshot;
    }

    static bool HasCartRam()
    {
        return SRL::Memory::CartRam::GetReport().TotalSize > 0u;
    }

    static uint32_t HighWorkLargestFreeBlock()
    {
        return SRL::Memory::HighWorkRam::GetLargestFreeBlockSize();
    }

    static int32_t HighWorkFreeSpace()
    {
        return SRL::Memory::HighWorkRam::GetFreeSpace();
    }

    static int32_t CartFreeSpace()
    {
        return SRL::Memory::CartRam::GetFreeSpace();
    }

    static void* AllocateCartBytes(const size_t byteCount)
    {
        return SRL::Memory::CartRam::Malloc(byteCount);
    }

    static AllocationPool ResolvePreferredPool(const ConsumerCategory category)
    {
        switch (category)
        {
        case ConsumerCategory::AudioPcm:
            return ResolveAudioPcmPool();
        case ConsumerCategory::CdStaging:
            return HasCartRam() ? AllocationPool::Cart : AllocationPool::HighWork;
        case ConsumerCategory::TrackRender:
        case ConsumerCategory::CarRender:
            return AllocationPool::HighWork;
        case ConsumerCategory::Hud:
        case ConsumerCategory::DebugTransient:
        default:
            return AllocationPool::LowWork;
        }
    }

    static void ConfigurePcmStreamingBudget()
    {
        const AllocationPool pool = ResolvePreferredPool(ConsumerCategory::AudioPcm);
        SRL::Sound::Pcm::SetMemAllocationBehaviour(
            SRL::Sound::Pcm::PcmMalloc::LwRam,
            ToPcmMalloc(pool));
    }

private:
    static AllocationPool ResolveAudioPcmPool()
    {
        return (HighWorkLargestFreeBlock() >= 192u * 1024u)
            ? AllocationPool::HighWork
            : (HasCartRam() ? AllocationPool::Cart : AllocationPool::HighWork);
    }

    static SRL::Sound::Pcm::PcmMalloc ToPcmMalloc(const AllocationPool pool)
    {
        switch (pool)
        {
        case AllocationPool::HighWork:
            return SRL::Sound::Pcm::PcmMalloc::HwRam;
        case AllocationPool::Cart:
            return SRL::Sound::Pcm::PcmMalloc::CartRam;
        case AllocationPool::LowWork:
        default:
            return SRL::Sound::Pcm::PcmMalloc::LwRam;
        }
    }
};

} // namespace Game
