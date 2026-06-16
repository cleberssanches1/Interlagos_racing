#pragma once

#include <cstdint>

#include "memory_budget_system.hpp"

namespace MemoryBudgetDomain
{

enum class Stage : uint8_t
{
    SnapshotAssembly = 0,
    PressureAssembly,
    PolicyAssembly,
    TelemetryAssembly
};

enum class PressureLevel : uint8_t
{
    Normal = 0,
    Soft,
    Pressure,
    Critical,
    Catastrophic
};

struct Thresholds
{
    uint32_t highWorkSoftFloor = 0u;
    uint32_t highWorkHardFloor = 0u;
    uint32_t highWorkCatastrophicFloor = 0u;
    uint32_t lowWorkSoftFloor = 0u;
    uint32_t lowWorkHardFloor = 0u;
    uint32_t pcmPreferredHighWorkBlock = 192u * 1024u;
};

struct MemorySnapshotPacket
{
    bool valid = false;
    Game::MemoryBudgetSystem::Snapshot snapshot{};
    uint32_t highWorkLargestFreeBlock = 0u;
    int32_t highWorkFreeSpace = 0;
    int32_t cartFreeSpace = 0;
    bool cartAvailable = false;
};

struct MemoryPressurePacket
{
    bool valid = false;
    PressureLevel highWorkPressure = PressureLevel::Normal;
    PressureLevel lowWorkPressure = PressureLevel::Normal;
    bool highWorkHasSoftPressure = false;
    bool highWorkHasHardPressure = false;
    bool highWorkCatastrophic = false;
    bool lowWorkHasSoftPressure = false;
    bool lowWorkHasHardPressure = false;
};

struct MemoryBudgetPolicyPacket
{
    bool valid = false;
    bool preferHighWorkForPcm = false;
    bool preferCartForPcm = false;
    bool shouldReduceStreamingPressure = false;
    bool shouldAvoidOptionalAllocations = false;
    bool cartAvailable = false;
};

struct MemoryTelemetryPacket
{
    bool valid = false;
    uint32_t highWorkFree = 0u;
    uint32_t lowWorkFree = 0u;
    uint32_t cartFree = 0u;
    uint32_t highWorkLargestFreeBlock = 0u;
    PressureLevel highWorkPressure = PressureLevel::Normal;
    PressureLevel lowWorkPressure = PressureLevel::Normal;
};

} // namespace MemoryBudgetDomain
