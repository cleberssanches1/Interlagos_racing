#pragma once

#include "game_loop_presentation_debug_contracts.hpp"

namespace GameLoopRuntime
{

struct PeriodicHudStatsInputs
{
    uint32_t frameCounter = 0u;
    bool enableRuntimeStatsLogs = false;
    bool logTrack = false;
    bool logCar = false;
    uint32_t faceCount = 0u;
    uint32_t vertexCount = 0u;
    uint32_t submittedTrackFaces = 0u;
    uint32_t submittedCarFaces = 0u;
    int32_t hwrFree = 0;
    int32_t hwrTotal = 0;
    uint32_t vdp1TexCount = 0u;
    uint32_t vdp1HeapPctFiltered = 0u;
    uint64_t accumSubmittedFaces = 0u;
    uint32_t accumSamples = 0u;
    uint32_t peakVdp1Used = 0u;
};

inline void SeedDrivingHudTextPacket(const Game::CarSystem::DrivetrainDebugSnapshot& drivetrain,
                                     DrivingHudTextPacket& outPacket)
{
    outPacket.valid = true;
    outPacket.speedKmh = drivetrain.speedKmh;
    outPacket.gearChar = drivetrain.gearChar;
    outPacket.engineRpm = drivetrain.engineRpm;
    outPacket.shiftFrames = drivetrain.shiftFrames;
    outPacket.shiftRpmBefore = drivetrain.shiftRpmBefore;
    outPacket.shiftRpmAfter = drivetrain.shiftRpmAfter;
}

inline DrivingHudTextPacket BuildDrivingHudTextPacket(
    const Game::CarSystem::DrivetrainDebugSnapshot& drivetrain)
{
    DrivingHudTextPacket packet{};
    SeedDrivingHudTextPacket(drivetrain, packet);
    return packet;
}

inline void SeedPeriodicHudStatsPacket(const PeriodicHudStatsInputs& inputs,
                                       PeriodicHudStatsPacket& outPacket)
{
    outPacket.valid = inputs.enableRuntimeStatsLogs;
    outPacket.frameCounter = inputs.frameCounter;
    outPacket.logTrack = inputs.logTrack;
    outPacket.logCar = inputs.logCar;
    outPacket.faceCount = inputs.faceCount;
    outPacket.vertexCount = inputs.vertexCount;
    outPacket.submittedTrackFaces = inputs.submittedTrackFaces;
    outPacket.submittedCarFaces = inputs.submittedCarFaces;
    outPacket.submittedFacesNow = inputs.submittedTrackFaces + inputs.submittedCarFaces;

    constexpr uint32_t kVdp1FaceCostBytes = 64u;
    constexpr uint32_t kVdp1FrameBudgetBytes = 512u * 1024u;
    const uint32_t avgFaces =
        (inputs.accumSamples > 0u)
            ? static_cast<uint32_t>(inputs.accumSubmittedFaces / inputs.accumSamples)
            : 0u;
    const uint32_t avgUsed = avgFaces * kVdp1FaceCostBytes;
    outPacket.avgFaces = avgFaces;
    outPacket.vdp1Used =
        (avgUsed > kVdp1FrameBudgetBytes) ? kVdp1FrameBudgetBytes : avgUsed;
    outPacket.peakVdp1Used = inputs.peakVdp1Used;
    outPacket.vdp1Free =
        (kVdp1FrameBudgetBytes > outPacket.vdp1Used)
            ? (kVdp1FrameBudgetBytes - outPacket.vdp1Used)
            : 0u;
    outPacket.vdp1Pct =
        (kVdp1FrameBudgetBytes > 0u)
            ? static_cast<uint32_t>((outPacket.vdp1Used * 100u) / kVdp1FrameBudgetBytes)
            : 0u;
    outPacket.peakPct =
        (kVdp1FrameBudgetBytes > 0u)
            ? static_cast<uint32_t>((inputs.peakVdp1Used * 100u) / kVdp1FrameBudgetBytes)
            : 0u;
    outPacket.vdp1HeapPctFiltered = inputs.vdp1HeapPctFiltered;
    outPacket.vdp1TexCount = static_cast<uint16_t>(inputs.vdp1TexCount);
    outPacket.hwrFree = inputs.hwrFree;
    outPacket.hwrUsed = inputs.hwrTotal - inputs.hwrFree;
    outPacket.hwrPct10 =
        (inputs.hwrTotal > 0) ? ((outPacket.hwrUsed * 1000) / inputs.hwrTotal) : 0;
}

inline PeriodicHudStatsPacket BuildPeriodicHudStatsPacket(const PeriodicHudStatsInputs& inputs)
{
    PeriodicHudStatsPacket packet{};
    SeedPeriodicHudStatsPacket(inputs, packet);
    return packet;
}

inline void SeedPresentationDebugBundle(const FramePresentationSnapshot& frame,
                                        const DrivingHudTextPacket& drivingHud,
                                        const PeriodicHudStatsPacket& periodicHud,
                                        const GameLoopTelemetryDomain::RealtimeFpsPacket& realtimeFps,
                                        PresentationDebugBundle& outBundle)
{
    (void)frame;
    (void)realtimeFps;
    outBundle.valid = true;
    outBundle.drivingHud = drivingHud;
    outBundle.periodicHud = periodicHud;
}

inline PresentationDebugBundle BuildPresentationDebugBundle(
    const FramePresentationSnapshot& frame,
    const DrivingHudTextPacket& drivingHud,
    const PeriodicHudStatsPacket& periodicHud,
    const GameLoopTelemetryDomain::RealtimeFpsPacket& realtimeFps)
{
    PresentationDebugBundle bundle{};
    SeedPresentationDebugBundle(frame, drivingHud, periodicHud, realtimeFps, bundle);
    return bundle;
}

inline PresentationDebugBundle BuildPresentationDebugBundle(
    const FramePresentationSnapshot& frame,
    const Game::CarSystem::DrivetrainDebugSnapshot& drivetrain,
    const PeriodicHudStatsInputs& periodicHud,
    const GameLoopTelemetryDomain::RealtimeFpsPacket& realtimeFps)
{
    return BuildPresentationDebugBundle(
        frame,
        BuildDrivingHudTextPacket(drivetrain),
        BuildPeriodicHudStatsPacket(periodicHud),
        realtimeFps);
}

} // namespace GameLoopRuntime
