#pragma once

#include <cstddef>
#include <cstdint>

#include <srl.hpp>

namespace Game
{

class MemoryBudgetSystem final
{
public:
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

    static void ConfigurePcmStreamingBudget()
    {
        const bool highWorkHasRoom = HighWorkLargestFreeBlock() >= 192u * 1024u;
        const bool cartAvailable = HasCartRam();
        SRL::Sound::Pcm::SetMemAllocationBehaviour(
            SRL::Sound::Pcm::PcmMalloc::LwRam,
            highWorkHasRoom
                ? SRL::Sound::Pcm::PcmMalloc::HwRam
                : (cartAvailable
                    ? SRL::Sound::Pcm::PcmMalloc::CartRam
                    : SRL::Sound::Pcm::PcmMalloc::HwRam));
    }
};

} // namespace Game
