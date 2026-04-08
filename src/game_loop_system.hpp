#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <srl.hpp>
#include <srl_slave.hpp>

#include "background_manager.hpp"
#include "application_state.hpp"
#include "camera_system.hpp"
#include "car_system.hpp"
#include "hud_system.hpp"
#include "interfaces.hpp"
#include "path_nya_loader.hpp"
#include "render_pipeline.hpp"
#include "track_system.hpp"

class GameLoopSystem
{
public:
    struct Context
    {
        bool* cartOkFlag = nullptr;
        bool enableBg = true;
        bool renderTrack = true;
        bool renderCar = true;
        bool renderAxes = false;
        bool trackSystemReady = false;
        bool verboseFrameLogs = false;
        bool logTrack = true;
        bool logCar = false;
        bool enableRuntimeStatsLogs = true;
        bool enableSlaveForCarPrepare = false;
        bool enableSlaveForSimulation = false;
        bool enableManualGouraudCopy = false;
        uint32_t faceCount = 0;
        uint32_t vertexCount = 0;
        SRL::Math::Types::Vector3D trackSegOffset{};
        SRL::Math::Types::Vector3D modelOffset{};
        SRL::Math::Types::Vector3D carWorldPosition{};
        SRL::Math::Types::Vector3D lightDirection{};
        BackgroundManager* bgManager = nullptr;
        CameraSystem* cameraSystem = nullptr;
        TrackSystem* trackSystem = nullptr;
        std::unique_ptr<Game::CarSystem>* carSystem = nullptr;
        RenderPipeline* renderPipeline = nullptr;
        HudSystem* hudSystem = nullptr;
        Game::ITrackCollisionQuery* trackCollision = nullptr;
        Game::ICarPhysics* carPhysics = nullptr;
        Game::IGameplayTick* gameplayTick = nullptr;
        Game::IAudioEvents* audioEvents = nullptr;
    };

    explicit GameLoopSystem(const Context& context)
        : context_(context)
    {}

    static void SetWorkRamDebugTag(SRL::Memory::DebugTag tag)
    {
        SRL::Memory::HighWorkRam::SetDebugTag(tag);
        SRL::Memory::LowWorkRam::SetDebugTag(tag);
    }

    // Run the main frame loop with fixed subsystem ordering.
    int RunForever()
    {
        while (1)
        {
            AppState::Set(AppState::Stage::LoopFrameBegin, frameCounter_);
            AppState::PresentOverlay(2);
            if (!ValidateFramePreconditions()) continue;
            hwrStageTrace_ = {};
            lwrStageTrace_ = {};
            SetWorkRamDebugTag(SRL::Memory::DebugTag::Unknown);
            hwrStageTrace_.begin = MaybeCaptureHighWorkRamSnapshot();
            lwrStageTrace_.begin = MaybeCaptureLowWorkRamSnapshot(true);

            const FrameInputState input = PollFrameInput();
            ConsumeCompletedJobs();

            SetWorkRamDebugTag(SRL::Memory::DebugTag::Gameplay);
            Game::GameplayFrameState frameState = BuildGameplayFrameState(input);
            ExecuteGameplayFrame(frameState);
            hwrStageTrace_.gameplay = MaybeCaptureHighWorkRamSnapshot();
            lwrStageTrace_.gameplay = MaybeCaptureLowWorkRamSnapshot();

            if (autoLapTestEnabled_)
            {
                SetWorkRamDebugTag(SRL::Memory::DebugTag::AutoLap);
                UpdateAutoLapRoute(context_, context_.carWorldPosition, carYawDeg_);
                if (context_.cameraSystem)
                {
                    context_.cameraSystem->SetCarYawDegrees(carYawDeg_);
                }
            }
            hwrStageTrace_.autoLap = MaybeCaptureHighWorkRamSnapshot();
            lwrStageTrace_.autoLap = MaybeCaptureLowWorkRamSnapshot();

            SetWorkRamDebugTag(SRL::Memory::DebugTag::Background);
            ScheduleCarPrepareIfEnabled();
            UpdateBackground();
            hwrStageTrace_.background = MaybeCaptureHighWorkRamSnapshot();
            lwrStageTrace_.background = MaybeCaptureLowWorkRamSnapshot();

            const CameraFrameState camera = ResolveCameraFrameState();
            SetWorkRamDebugTag(SRL::Memory::DebugTag::Hud);
            UpdateHud(camera);
            hwrStageTrace_.hud = MaybeCaptureHighWorkRamSnapshot();
            lwrStageTrace_.hud = MaybeCaptureLowWorkRamSnapshot();
            RenderFrame(camera);
            RenderAxes();

            FinishFrame();
        }
    }

private:
    static constexpr int32_t kAutoLapYawBiasDeg = 0;

    static constexpr bool kEnableDetailedWorkRamTelemetry = false;
    static constexpr bool kEnableLowWorkFreeOverlay = true;
    static constexpr uint16_t kLowWorkFreeOverlayCadenceFrames = 8u;

    struct HwrStageTrace
    {
        struct Snapshot
        {
            uint32_t freeBytes = 0;
            uint32_t usedBlocks = 0;
            uint32_t freeBlocks = 0;
            uint32_t liveBytes = 0;
            uint32_t allocCalls = 0;
            uint32_t freeCalls = 0;
            uint32_t reallocCalls = 0;
            uint32_t failedAllocCalls = 0;
        };

        Snapshot begin{};
        Snapshot gameplay{};
        Snapshot autoLap{};
        Snapshot background{};
        Snapshot hud{};
        Snapshot trackDraw{};
        Snapshot trackEnd{};
        Snapshot track{};
        Snapshot car{};
        Snapshot preFinish{};
        Snapshot preSync{};
        Snapshot postSync{};
    };

    struct LwrStageTrace
    {
        struct Snapshot
        {
            uint32_t freeBytes = 0;
            uint32_t payloadBytes = 0;
            uint32_t overheadBytes = 0;
            uint32_t freeBlocks = 0;
            uint32_t largestFreeBytes = 0;
        };

        Snapshot begin{};
        Snapshot gameplay{};
        Snapshot autoLap{};
        Snapshot background{};
        Snapshot hud{};
        Snapshot trackDraw{};
        Snapshot trackEnd{};
        Snapshot track{};
        Snapshot car{};
        Snapshot preFinish{};
        Snapshot preSync{};
        Snapshot postSync{};
    };

    struct LowWorkTagGroupOverlay
    {
        uint32_t initUnknown = 0;
        uint32_t gameplayAuto = 0;
        uint32_t ui = 0;
        uint32_t car = 0;
        uint32_t track = 0;
        uint32_t finishSync = 0;
    };

    static HwrStageTrace::Snapshot CaptureHighWorkRamSnapshot()
    {
        const auto report = SRL::Memory::HighWorkRam::GetReport();
        const auto stats = SRL::Memory::HighWorkRam::GetOpStats();
        HwrStageTrace::Snapshot snapshot{};
        snapshot.freeBytes = static_cast<uint32_t>(report.FreeSize);
        snapshot.usedBlocks = static_cast<uint32_t>(report.UsedBlocks);
        snapshot.freeBlocks = static_cast<uint32_t>(report.FreeBlocks);
        snapshot.liveBytes = static_cast<uint32_t>(stats.LiveBytes);
        snapshot.allocCalls = static_cast<uint32_t>(stats.AllocCalls);
        snapshot.freeCalls = static_cast<uint32_t>(stats.FreeCalls);
        snapshot.reallocCalls = static_cast<uint32_t>(stats.ReallocCalls);
        snapshot.failedAllocCalls = static_cast<uint32_t>(stats.FailedAllocCalls);
        return snapshot;
    }

    static LwrStageTrace::Snapshot CaptureLowWorkRamSnapshot(bool detailed = false)
    {
        const auto report = SRL::Memory::LowWorkRam::GetReport();
        LwrStageTrace::Snapshot snapshot{};
        snapshot.freeBytes = static_cast<uint32_t>(report.FreeSize);
        if (!detailed)
        {
            return snapshot;
        }

        const uint32_t usedBytes = static_cast<uint32_t>(
            (report.TotalSize >= report.FreeSize) ? (report.TotalSize - report.FreeSize) : 0u);
        const uint32_t payloadBytes = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedPayloadBytes());
        snapshot.payloadBytes = payloadBytes;
        snapshot.overheadBytes = (usedBytes >= payloadBytes) ? (usedBytes - payloadBytes) : 0u;
        snapshot.freeBlocks = static_cast<uint32_t>(report.FreeBlocks);
        snapshot.largestFreeBytes = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetLargestFreeBlockSize());
        return snapshot;
    }

    static HwrStageTrace::Snapshot MaybeCaptureHighWorkRamSnapshot()
    {
        if constexpr (!kEnableDetailedWorkRamTelemetry)
        {
            return {};
        }
        return CaptureHighWorkRamSnapshot();
    }

    static LwrStageTrace::Snapshot MaybeCaptureLowWorkRamSnapshot(bool detailed = false)
    {
        if constexpr (!kEnableDetailedWorkRamTelemetry)
        {
            (void)detailed;
            return {};
        }
        return CaptureLowWorkRamSnapshot(detailed);
    }

    static int32_t SnapshotLiveDelta(const HwrStageTrace::Snapshot& from,
                                     const HwrStageTrace::Snapshot& to)
    {
        return static_cast<int32_t>(to.liveBytes) - static_cast<int32_t>(from.liveBytes);
    }

    static int32_t SnapshotFreeDelta(const LwrStageTrace::Snapshot& from,
                                     const LwrStageTrace::Snapshot& to)
    {
        return static_cast<int32_t>(to.freeBytes) - static_cast<int32_t>(from.freeBytes);
    }

    static int32_t SnapshotPayloadDelta(const LwrStageTrace::Snapshot& from,
                                        const LwrStageTrace::Snapshot& to)
    {
        return static_cast<int32_t>(to.payloadBytes) - static_cast<int32_t>(from.payloadBytes);
    }

    static int32_t SnapshotOverheadDelta(const LwrStageTrace::Snapshot& from,
                                         const LwrStageTrace::Snapshot& to)
    {
        return static_cast<int32_t>(to.overheadBytes) - static_cast<int32_t>(from.overheadBytes);
    }

    void PrintWorkRamUsageRealtime() const
    {
        const auto hwr = SRL::Memory::HighWorkRam::GetReport();
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        const uint32_t hwrUsed = static_cast<uint32_t>(
            (hwr.TotalSize >= hwr.FreeSize) ? (hwr.TotalSize - hwr.FreeSize) : 0u);
        const uint32_t lwrUsed = static_cast<uint32_t>(
            (lwr.TotalSize >= lwr.FreeSize) ? (lwr.TotalSize - lwr.FreeSize) : 0u);
        SRL::Debug::Print(2, 14, "HWR u:%u f:%u        ",
                          static_cast<unsigned>(hwrUsed),
                          static_cast<unsigned>(hwr.FreeSize));
        SRL::Debug::Print(2, 15, "LWR u:%u f:%u        ",
                          static_cast<unsigned>(lwrUsed),
                          static_cast<unsigned>(lwr.FreeSize));
    }

    void UpdateLowWorkFreeOverlay()
    {
        if (!context_.enableRuntimeStatsLogs)
        {
            return;
        }

        if constexpr (!kEnableLowWorkFreeOverlay)
        {
            return;
        }

        const bool slideThisFrame =
            context_.trackSystem &&
            context_.trackSystemReady &&
            (context_.trackSystem->SlidesThisFrame() != 0u);

        if (!slideThisFrame)
        {
            if (lowWorkFreeOverlayCooldownFrames_ > 0u)
            {
                --lowWorkFreeOverlayCooldownFrames_;
                return;
            }
        }
        lowWorkFreeOverlayCooldownFrames_ = kLowWorkFreeOverlayCadenceFrames;

        uint32_t freeBytes = 0u;
        uint32_t highFreeBytes = 0u;
        uint8_t slides = 0u;
        int16_t slideId = -1;
        uint16_t trackStreamTicks = 0u;
        uint16_t trackMaintenanceTicks = 0u;
        uint16_t trackDrawTicks = 0u;
        uint16_t trackFrameTicks = 0u;
        uint16_t trackWindowTicks = 0u;
        uint16_t trackPrefetchTicks = 0u;
        uint16_t trackLodTicks = 0u;
        uint16_t trackWorkingSetTicks = 0u;
        TrackSystem::LowWorkCategoryBreakdown breakdown{};
        if (context_.trackSystem && context_.trackSystemReady)
        {
            freeBytes = context_.trackSystem->LowWorkEndFreeBytesThisFrame();
            slides = context_.trackSystem->SlidesThisFrame();
            slideId = context_.trackSystem->SlideSegmentIdThisFrame();
            breakdown = context_.trackSystem->LowWorkBreakdownThisFrame();
            trackStreamTicks = context_.trackSystem->StreamTicksThisFrame();
            trackMaintenanceTicks = context_.trackSystem->MaintenanceTicksThisFrame();
            trackDrawTicks = context_.trackSystem->DrawTicksThisFrame();
            trackFrameTicks = context_.trackSystem->FrameTicksThisFrame();
            trackWindowTicks = context_.trackSystem->WindowTicksThisFrame();
            trackPrefetchTicks = context_.trackSystem->PrefetchTicksThisFrame();
            trackLodTicks = context_.trackSystem->LodTicksThisFrame();
            trackWorkingSetTicks = context_.trackSystem->WorkingSetTicksThisFrame();
        }
        else
        {
            const auto lwr = SRL::Memory::LowWorkRam::GetReport();
            freeBytes = static_cast<uint32_t>(lwr.FreeSize);
        }
        {
            const auto hwr = SRL::Memory::HighWorkRam::GetReport();
            highFreeBytes = static_cast<uint32_t>(hwr.FreeSize);
        }

        const int32_t freeDelta = lowWorkFreeOverlayValid_
            ? (static_cast<int32_t>(freeBytes) - static_cast<int32_t>(lastLowWorkFreeOverlayBytes_))
            : 0;
        lastLowWorkFreeOverlayBytes_ = freeBytes;
        lowWorkFreeOverlayValid_ = true;

        SRL::Debug::Print(2, 15, "WLWR free:%u df:%d sl:%u id:%d    ",
                          static_cast<unsigned>(freeBytes),
                          static_cast<int>(freeDelta),
                          static_cast<unsigned>(slides),
                          static_cast<int>(slideId));

        const uint32_t highInitUnknown =
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Init)) +
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Unknown));
        const uint32_t highGameplayAuto =
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Gameplay)) +
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::AutoLap));
        const uint32_t highUi =
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Background)) +
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Hud));
        const uint32_t highCar =
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Car));
        const uint32_t highTrack =
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackCore)) +
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackPrepare)) +
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackLod)) +
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackTexture)) +
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackBackend));
        const uint32_t highFinishSync =
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Finish)) +
            static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Sync));

        SRL::Debug::Print(2, 12, "HWT1 iu:%u ga:%u ui:%u     ",
                          static_cast<unsigned>(highInitUnknown),
                          static_cast<unsigned>(highGameplayAuto),
                          static_cast<unsigned>(highUi));
        SRL::Debug::Print(2, 13, "HWT2 tr:%u car:%u fs:%u hf:%u",
                          static_cast<unsigned>(highTrack),
                          static_cast<unsigned>(highCar),
                          static_cast<unsigned>(highFinishSync),
                          static_cast<unsigned>(highFreeBytes));

        lastLowWorkBreakdownOverlay_ = breakdown;
        lowWorkBreakdownOverlayValid_ = true;

        SRL::Debug::Print(2, 16, "LWC1 r:%u s:%u w:%u       ",
                          static_cast<unsigned>(breakdown.renderers),
                          static_cast<unsigned>(breakdown.slotState),
                          static_cast<unsigned>(breakdown.workingSet));
        SRL::Debug::Print(2, 17, "LWC2 f:%u t:%u m:%u       ",
                          static_cast<unsigned>(breakdown.familyCache),
                          static_cast<unsigned>(breakdown.transient),
                          static_cast<unsigned>(breakdown.metadata));
        const uint32_t trackCoreBytes =
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackCore));
        const uint32_t trackPrepareBytes =
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackPrepare));
        const uint32_t trackLodBytes =
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackLod));
        const uint32_t trackTextureBytes =
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackTexture));
        const uint32_t trackBackendBytes =
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackBackend));
        SRL::Debug::Print(2, 18, "LWT1 tc:%u tp:%u tl:%u   ",
                          static_cast<unsigned>(trackCoreBytes),
                          static_cast<unsigned>(trackPrepareBytes),
                          static_cast<unsigned>(trackLodBytes));
        SRL::Debug::Print(2, 19, "LWT2 tx:%u tb:%u         ",
                          static_cast<unsigned>(trackTextureBytes),
                          static_cast<unsigned>(trackBackendBytes));

        LowWorkTagGroupOverlay tagGroups{};
        tagGroups.initUnknown =
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Init)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Unknown));
        tagGroups.gameplayAuto =
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Gameplay)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::AutoLap));
        tagGroups.ui =
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Background)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Hud));
        tagGroups.car =
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Car));
        tagGroups.track =
            trackCoreBytes +
            trackPrepareBytes +
            trackLodBytes +
            trackTextureBytes +
            trackBackendBytes;
        tagGroups.finishSync =
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Finish)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Sync));

        lastLowWorkTagGroupOverlay_ = tagGroups;
        lowWorkTagGroupOverlayValid_ = true;

        SRL::Debug::Print(2, 20, "LTX1 iu:%u ga:%u ui:%u    ",
                          static_cast<unsigned>(tagGroups.initUnknown),
                          static_cast<unsigned>(tagGroups.gameplayAuto),
                          static_cast<unsigned>(tagGroups.ui));
        SRL::Debug::Print(2, 21, "LTX2 tr:%u car:%u fs:%u   ",
                          static_cast<unsigned>(tagGroups.track),
                          static_cast<unsigned>(tagGroups.car),
                          static_cast<unsigned>(tagGroups.finishSync));

        const auto lwrReport = SRL::Memory::LowWorkRam::GetReport();
        const uint32_t usedBytes = static_cast<uint32_t>(
            (lwrReport.TotalSize >= lwrReport.FreeSize) ? (lwrReport.TotalSize - lwrReport.FreeSize) : 0u);
        const uint32_t payloadBytes = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedPayloadBytes());
        const uint32_t overheadBytes = (usedBytes >= payloadBytes) ? (usedBytes - payloadBytes) : 0u;
        const uint32_t freeBlocks = static_cast<uint32_t>(lwrReport.FreeBlocks);

        const uint32_t knownTaggedBytes =
            tagGroups.initUnknown +
            tagGroups.gameplayAuto +
            tagGroups.ui +
            tagGroups.car +
            tagGroups.track +
            tagGroups.finishSync;
        const uint32_t invalidTaggedBytes = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesWithInvalidTag());
        const uint32_t invalidTaggedBlocks = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBlockCountWithInvalidTag());

        lastLowWorkPayloadOverlayBytes_ = payloadBytes;
        lastLowWorkOverheadOverlayBytes_ = overheadBytes;
        lastLowWorkFreeBlocksOverlay_ = freeBlocks;
        lowWorkAllocatorOverlayValid_ = true;

        SRL::Debug::Print(2, 22, "LFO1 py:%u ov:%u fb:%u    ",
                          static_cast<unsigned>(payloadBytes),
                          static_cast<unsigned>(overheadBytes),
                          static_cast<unsigned>(freeBlocks));
        SRL::Debug::Print(2, 23, "LFO2 kn:%u iv:%u ib:%u   ",
                          static_cast<unsigned>(knownTaggedBytes),
                          static_cast<unsigned>(invalidTaggedBytes),
                          static_cast<unsigned>(invalidTaggedBlocks));
        SRL::Debug::Print(2, 24, "LTK1 st:%u mw:%u dr:%u fr:%u   ",
                          static_cast<unsigned>(trackStreamTicks),
                          static_cast<unsigned>(trackMaintenanceTicks),
                          static_cast<unsigned>(trackDrawTicks),
                          static_cast<unsigned>(trackFrameTicks));
        SRL::Debug::Print(2, 25, "LTK2 w:%u pf:%u ld:%u ws:%u   ",
                          static_cast<unsigned>(trackWindowTicks),
                          static_cast<unsigned>(trackPrefetchTicks),
                          static_cast<unsigned>(trackLodTicks),
                          static_cast<unsigned>(trackWorkingSetTicks));
    }

    void MaybeLogHighWorkRamTrace()
    {
        constexpr uint32_t kLowFreeThresholdBytes = 8u * 1024u;
        constexpr uint32_t kLargeDropThresholdBytes = 32u * 1024u;
        const uint32_t beginFree = hwrStageTrace_.begin.freeBytes;
        const uint32_t finishFree = hwrStageTrace_.postSync.freeBytes;
        const int32_t frameAccum = SnapshotLiveDelta(hwrStageTrace_.begin, hwrStageTrace_.postSync);
        const int32_t finishAccum = SnapshotLiveDelta(hwrStageTrace_.car, hwrStageTrace_.preSync);
        const int32_t syncAccum = SnapshotLiveDelta(hwrStageTrace_.preSync, hwrStageTrace_.postSync);
        if (hwrTraceCooldownFrames_ > 0u)
        {
            --hwrTraceCooldownFrames_;
        }

        const bool lowFree = finishFree <= kLowFreeThresholdBytes;
        const bool largeDrop = beginFree > finishFree && (beginFree - finishFree) >= kLargeDropThresholdBytes;
        const bool leakedThisFrame = frameAccum > 0;
        const bool failedAlloc = hwrStageTrace_.postSync.failedAllocCalls > hwrStageTrace_.begin.failedAllocCalls;
        if (!lowFree && !largeDrop && !leakedThisFrame && !failedAlloc) return;
        if (hwrTraceCooldownFrames_ > 0u) return;

        const uint32_t allocDelta = hwrStageTrace_.postSync.allocCalls - hwrStageTrace_.begin.allocCalls;
        const uint32_t freeDelta = hwrStageTrace_.postSync.freeCalls - hwrStageTrace_.begin.freeCalls;
        const uint32_t reallocDelta = hwrStageTrace_.postSync.reallocCalls - hwrStageTrace_.begin.reallocCalls;
        const uint32_t failedDelta = hwrStageTrace_.postSync.failedAllocCalls - hwrStageTrace_.begin.failedAllocCalls;
        const uint32_t gameplayLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Gameplay));
        const uint32_t autoLapLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::AutoLap));
        const uint32_t backgroundLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Background));
        const uint32_t hudLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Hud));
        const uint32_t trackCoreLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackCore));
        const uint32_t trackPrepareLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackPrepare));
        const uint32_t trackLodLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackLod));
        const uint32_t trackTextureLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackTexture));
        const uint32_t trackBackendLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackBackend));
        const uint32_t finishLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Finish));
        const uint32_t syncLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Sync));
        SRL::Debug::Print(2, 16, "GH1 i:%u u:%u bg:%u hd:%u     ",
                          static_cast<unsigned>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Init)),
                          static_cast<unsigned>(SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Unknown)),
                          static_cast<unsigned>(backgroundLiveBytesDirect),
                          static_cast<unsigned>(hudLiveBytesDirect));
        SRL::Debug::Print(2, 17, "GH2 gp:%u au:%u fn:%u sy:%u   ",
                          static_cast<unsigned>(gameplayLiveBytesDirect),
                          static_cast<unsigned>(autoLapLiveBytesDirect),
                          static_cast<unsigned>(finishLiveBytesDirect),
                          static_cast<unsigned>(syncLiveBytesDirect));
        SRL::Debug::Print(2, 18, "GH3 a:%u f:%u r:%u x:%u ub:%u fb:%u   ",
                          static_cast<unsigned>(allocDelta),
                          static_cast<unsigned>(freeDelta),
                          static_cast<unsigned>(reallocDelta),
                          static_cast<unsigned>(failedDelta),
                          static_cast<unsigned>(hwrStageTrace_.postSync.usedBlocks),
                          static_cast<unsigned>(hwrStageTrace_.postSync.freeBlocks));
        SRL::Debug::Print(2, 19, "GH4 fn:%d sy:%d free:%u       ",
                          finishAccum,
                          syncAccum,
                          static_cast<unsigned>(hwrStageTrace_.postSync.freeBytes));
        const auto validation = SRL::Memory::LowWorkRam::Validate();
        const auto lwrReport = SRL::Memory::LowWorkRam::GetReport();
        const uint32_t lwrUsedBytesDirect = static_cast<uint32_t>(
            (lwrReport.TotalSize >= lwrReport.FreeSize) ? (lwrReport.TotalSize - lwrReport.FreeSize) : 0u);
        const uint32_t initLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Init));
        const uint32_t unknownLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Unknown));
        const uint32_t gameplayLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Gameplay));
        const uint32_t autoLapLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::AutoLap));
        const uint32_t payloadBytesDirect = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedPayloadBytes());
        const uint32_t lwrOverheadBytesDirect =
            (lwrUsedBytesDirect >= payloadBytesDirect) ? (lwrUsedBytesDirect - payloadBytesDirect) : 0u;
        const uint32_t trackBackendLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackBackend));
        const uint32_t trackCoreLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackCore));
        const uint32_t trackPrepareLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackPrepare));
        const uint32_t trackLodLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackLod));
        const uint32_t trackTextureLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackTexture));
        SRL::Debug::Print(2, 20, "LW7 tb:%u tw:%u tl:%u tx:%u",
                          static_cast<unsigned>(trackBackendLiveBytesLwr),
                          static_cast<unsigned>(trackPrepareLiveBytesLwr),
                          static_cast<unsigned>(trackLodLiveBytesLwr),
                          static_cast<unsigned>(trackTextureLiveBytesLwr));
        SRL::Debug::Print(2, 21, "LW8 i:%u u:%u gp:%u au:%u",
                          static_cast<unsigned>(initLiveBytesDirect),
                          static_cast<unsigned>(unknownLiveBytesDirect),
                          static_cast<unsigned>(gameplayLiveBytesLwr),
                          static_cast<unsigned>(autoLapLiveBytesLwr));
        if (validation.valid)
        {
            SRL::Debug::Print(2, 22, "LW10 py:%u ov:%u lf:%u fb:%u",
                              static_cast<unsigned>(payloadBytesDirect),
                              static_cast<unsigned>(lwrOverheadBytesDirect),
                              static_cast<unsigned>(SRL::Memory::LowWorkRam::GetLargestFreeBlockSize()),
                              static_cast<unsigned>(lwrReport.FreeBlocks));
        }
        else
        {
            SRL::Debug::Print(2, 22, "LW9 bo:%u nx:%u bs:%u free:%u ",
                              static_cast<unsigned>(validation.blockOffset),
                              static_cast<unsigned>(validation.nextOffset),
                              static_cast<unsigned>(validation.blockSize),
                              static_cast<unsigned>(SRL::Memory::LowWorkRam::GetReport().FreeSize));
        }
        hwrTraceCooldownFrames_ = 15u;
    }

    void MaybeLogLowWorkRamTrace()
    {
        const int32_t gameplayFreeDelta = SnapshotFreeDelta(lwrStageTrace_.begin, lwrStageTrace_.gameplay);
        const int32_t autoLapFreeDelta = SnapshotFreeDelta(lwrStageTrace_.gameplay, lwrStageTrace_.autoLap);
        const int32_t backgroundFreeDelta = SnapshotFreeDelta(lwrStageTrace_.autoLap, lwrStageTrace_.background);
        const int32_t hudFreeDelta = SnapshotFreeDelta(lwrStageTrace_.background, lwrStageTrace_.hud);
        const int32_t trackDrawFreeDelta = SnapshotFreeDelta(lwrStageTrace_.hud, lwrStageTrace_.trackDraw);
        const int32_t trackEndFreeDelta = SnapshotFreeDelta(lwrStageTrace_.trackDraw, lwrStageTrace_.trackEnd);
        const int32_t carFreeDelta = SnapshotFreeDelta(lwrStageTrace_.trackEnd, lwrStageTrace_.car);
        const int32_t finishFreeDelta = SnapshotFreeDelta(lwrStageTrace_.car, lwrStageTrace_.preSync);
        const int32_t syncFreeDelta = SnapshotFreeDelta(lwrStageTrace_.preSync, lwrStageTrace_.postSync);
        const int32_t frameFreeDelta = SnapshotFreeDelta(lwrStageTrace_.begin, lwrStageTrace_.postSync);
        const int32_t framePayloadDelta = SnapshotPayloadDelta(lwrStageTrace_.begin, lwrStageTrace_.postSync);
        const int32_t frameOverheadDelta = SnapshotOverheadDelta(lwrStageTrace_.begin, lwrStageTrace_.postSync);
        const int32_t largestFreeDelta =
            static_cast<int32_t>(lwrStageTrace_.postSync.largestFreeBytes) -
            static_cast<int32_t>(lwrStageTrace_.begin.largestFreeBytes);
        const int32_t freeBlocksDelta =
            static_cast<int32_t>(lwrStageTrace_.postSync.freeBlocks) -
            static_cast<int32_t>(lwrStageTrace_.begin.freeBlocks);

        int32_t trackDrawPrepareDelta = 0;
        int32_t trackDrawExecuteDelta = 0;
        int32_t trackDrawOtherDelta = 0;
        int32_t trackDrawFrameDelta = 0;
        if (context_.trackSystem)
        {
            trackDrawPrepareDelta = context_.trackSystem->LowWorkDrawPrepareDeltaThisFrame();
            trackDrawExecuteDelta = context_.trackSystem->LowWorkDrawExecuteDeltaThisFrame();
            trackDrawOtherDelta = context_.trackSystem->LowWorkDrawOtherDeltaThisFrame();
            trackDrawFrameDelta = context_.trackSystem->LowWorkDrawFrameDeltaThisFrame();
        }

        SRL::Debug::Print(2, 26, "GLW1 gp:%d au:%d bg:%d hd:%d   ",
                          gameplayFreeDelta,
                          autoLapFreeDelta,
                          backgroundFreeDelta,
                          hudFreeDelta);
        SRL::Debug::Print(2, 27, "GLW2 td:%d te:%d c:%d f:%d   ",
                          trackDrawFreeDelta,
                          trackEndFreeDelta,
                          carFreeDelta,
                          finishFreeDelta);
        SRL::Debug::Print(2, 28, "GLW3 sy:%d fr:%d py:%d ov:%d ",
                          syncFreeDelta,
                          frameFreeDelta,
                          framePayloadDelta,
                          frameOverheadDelta);
        SRL::Debug::Print(2, 29, "GLW4 dp:%d dx:%d do:%d df:%d ",
                          trackDrawPrepareDelta,
                          trackDrawExecuteDelta,
                          trackDrawOtherDelta,
                          trackDrawFrameDelta);
        SRL::Debug::Print(2, 30, "GLW5 lf:%d fb:%d           ",
                          largestFreeDelta,
                          freeBlocksDelta);
    }

    // Validate world position before rendering to avoid invalid transform collapse.
    static bool IsFiniteCarPos(const SRL::Math::Types::Vector3D& p)
    {
        constexpr int32_t kRawLimit = (32767 << 16);
        const int32_t x = p.X.RawValue();
        const int32_t y = p.Y.RawValue();
        const int32_t z = p.Z.RawValue();
        if (x < -kRawLimit || x > kRawLimit) return false;
        if (y < -kRawLimit || y > kRawLimit) return false;
        if (z < -kRawLimit || z > kRawLimit) return false;
        return true;
    }

    // Validate camera vectors before releasing render for the frame.
    static bool IsFiniteCameraPoint(const SRL::Math::Types::Vector3D& p)
    {
        return IsFiniteCarPos(p);
    }

    struct FrameInputState
    {
        bool bHeld = false;
        bool cHeld = false;
        bool yHeld = false;
        bool xHeld = false;
        bool lHeld = false;
        bool rHeld = false;
        bool leftHeld = false;
        bool rightHeld = false;
    };

    struct CameraFrameState
    {
        SRL::Math::Types::Vector3D location{};
        SRL::Math::Types::Vector3D lookTarget{};
        bool ready = false;
    };

    Game::CarSystem* ActiveCarSystem() const
    {
        return (context_.carSystem && context_.carSystem->get()) ? context_.carSystem->get() : nullptr;
    }

    bool CanRenderCar() const
    {
        Game::CarSystem* car = ActiveCarSystem();
        return context_.renderCar && car && car->Valid();
    }

    bool ValidateFramePreconditions()
    {
        if (!context_.cartOkFlag || !(*context_.cartOkFlag))
        {
            AppState::Set(AppState::Stage::Fault, frameCounter_);
            SRL::Debug::Print(1, 3, "ERRO: Cartucho 4MB ausente");
            SRL::Debug::Print(1, 4, "Insira cart DRAM e reinicie");
            return false;
        }

        if (!context_.cameraSystem || !context_.trackSystem || !context_.hudSystem || !context_.renderPipeline)
        {
            AppState::Set(AppState::Stage::Fault, frameCounter_);
            SRL::Debug::Print(1, 3, "ERRO: subsistemas nao inicializados");
            return false;
        }
        return true;
    }

    FrameInputState PollFrameInput()
    {
        FrameInputState input{};
        input.bHeld = pad_.IsHeld(SRL::Input::Digital::Button::B);
        input.cHeld = pad_.IsHeld(SRL::Input::Digital::Button::C);
        input.yHeld = pad_.IsHeld(SRL::Input::Digital::Button::Y);
        input.xHeld = pad_.IsHeld(SRL::Input::Digital::Button::X);
        input.lHeld = pad_.IsHeld(SRL::Input::Digital::Button::L);
        input.rHeld = pad_.IsHeld(SRL::Input::Digital::Button::R);
        input.leftHeld = pad_.IsHeld(SRL::Input::Digital::Button::Left);
        input.rightHeld = pad_.IsHeld(SRL::Input::Digital::Button::Right);
        const bool xHeld = input.xHeld;

        int32_t yawForCamera = carYawDeg_;
        const bool allowManualYawInput = !autoLapTestEnabled_;
        context_.cameraSystem->UpdateFromPad(pad_, yawForCamera, orbitState_, allowManualYawInput);
        if (allowManualYawInput)
        {
            carYawDeg_ = yawForCamera;
        }

        if (Game::CarSystem* car = ActiveCarSystem())
        {
            if (xHeld)
            {
                if (autoLapTestEnabled_)
                {
                    // In auto-lap, show dynamic path-driven forward offset.
                    SRL::Debug::Print(1, 27, "CAR FWD off:%d    ", static_cast<int>(autoLapCurrentOffDeg_));
                }
                else
                {
                    if (carForwardOffsetRepeatFrames_ > 0) --carForwardOffsetRepeatFrames_;
                    if (carForwardOffsetRepeatFrames_ == 0)
                    {
                        int32_t nextOffsetDeg = car->VisualYawOffsetDegrees();
                        bool changed = false;
                        if (input.lHeld)
                        {
                            nextOffsetDeg -= 5;
                            changed = true;
                        }
                        if (input.rHeld)
                        {
                            nextOffsetDeg += 5;
                            changed = true;
                        }
                        if (changed)
                        {
                            car->SetVisualYawOffsetDegrees(nextOffsetDeg);
                            carForwardOffsetRepeatFrames_ = 2;
                        }
                    }
                    SRL::Debug::Print(1, 27, "CAR FWD off:%d    ", car->VisualYawOffsetDegrees());
                }
            }
            else
            {
                carForwardOffsetRepeatFrames_ = 0;
                SRL::Debug::Print(1, 27, "                    ");
            }
        }

        const bool camera2CalibrationActive =
            xHeld &&
            context_.cameraSystem &&
            (context_.cameraSystem->GetChasePreset() == CameraSystem::ChasePreset::ChaseNear);
        if (camera2CalibrationActive)
        {
            // During camera 2 calibration, arrows are reserved for camera offset tuning.
            // Do not forward left/right as steering input to gameplay/car systems.
            input.leftHeld = false;
            input.rightHeld = false;
        }
        if (input.yHeld && !yHeldPrev_)
        {
            autoLapTestEnabled_ = !autoLapTestEnabled_;
            if (autoLapTestEnabled_)
            {
                autoLapRouteInitialized_ = false;
                autoLapRouteBuilt_ = false;
            }
            else
            {
                autoLapCurrentOffDeg_ = 0;
            }
            SRL::Debug::Print(1, 23, "CAR MOVE:%u", autoLapTestEnabled_ ? 1u : 0u);
        }
        yHeldPrev_ = input.yHeld;
        leftHeldPrev_ = input.leftHeld;
        rightHeldPrev_ = input.rightHeld;
        return input;
    }

    void ConsumeCompletedJobs()
    {
        if (simJobInFlight_ && simulationTask_.IsDone())
        {
            simJobInFlight_ = false;
            simHasCompleted_ = true;
            simCompletedIdx_ = simInFlightIdx_;
        }
        if (simHasCompleted_)
        {
            const auto& simOut = simOutput_[simCompletedIdx_];
            context_.carWorldPosition = simOut.outWorldPosition;
            carYawDeg_ = simOut.outYawDeg;
            latestActiveSegmentId_ = simOut.frameState.activeSegmentId;
        }
        if (carPrepareJobInFlight_ && carPrepareTask_.IsDone())
        {
            carPrepareJobInFlight_ = false;
            carPrepareHasCompleted_ = true;
            carPrepareCompletedIdx_ = carPrepareInFlightIdx_;
        }
    }

    Game::GameplayFrameState BuildGameplayFrameState(const FrameInputState& input)
    {
        Game::GameplayFrameState frameState{};
        frameState.frameId = frameCounter_;
        frameState.carWorldPosition = context_.carWorldPosition;
        frameState.carYawDeg = carYawDeg_;

        Game::CarSystem* car = ActiveCarSystem();
        if (car)
        {
            // Keep car stopped until Y toggles movement on.
            if (autoLapTestEnabled_)
            {
                if (input.cHeld) car->Command()->Accelerate();
                if (input.bHeld) car->Command()->Brake();
                if (input.leftHeld) car->Command()->SteerLeft();
                if (input.rightHeld) car->Command()->SteerRight();
                car->UpdateWheels(input.cHeld, input.bHeld);
            }
            else
            {
                car->UpdateWheels(false, true);
            }

            const auto& commands = car->Commands();
            if (autoLapTestEnabled_)
            {
                frameState.throttle = commands.throttle;
                frameState.steering = commands.steering;
                frameState.braking = commands.braking;
                frameState.wheelsSpinning = commands.wheelsSpinning;
            }
            else
            {
                frameState.throttle = 0;
                frameState.steering = 0;
                frameState.braking = false;
                frameState.wheelsSpinning = false;
            }
        }
        return frameState;
    }

    void ExecuteGameplayFrame(Game::GameplayFrameState& frameState)
    {
        const bool useSlaveSim =
            context_.enableSlaveForSimulation &&
            (context_.gameplayTick || context_.carPhysics || context_.audioEvents);

        if (useSlaveSim)
        {
            if (!simJobInFlight_)
            {
                SimulationTask::Payload simPayload{};
                simPayload.gameplayTick = context_.gameplayTick;
                simPayload.carPhysics = context_.carPhysics;
                simPayload.audioEvents = context_.audioEvents;
                simPayload.trackCollision = context_.trackCollision;
                simPayload.frameState = frameState;
                simPayload.outWorldPosition = frameState.carWorldPosition;
                simPayload.outYawDeg = frameState.carYawDeg;

                const uint8_t slot = simWriteIdx_;
                simInput_[slot] = simPayload;
                simulationTask_.Configure(&simInput_[slot], &simOutput_[slot]);
                SRL::Slave::ExecuteOnSlave(simulationTask_);
                simJobInFlight_ = true;
                simInFlightIdx_ = slot;
                simWriteIdx_ ^= 1u;
            }
            return;
        }

        if (context_.gameplayTick)
        {
            context_.gameplayTick->Tick(frameState, context_.trackCollision);
        }
        if (context_.carPhysics)
        {
            context_.carPhysics->Step(frameState,
                                      context_.trackCollision,
                                      frameState.carWorldPosition,
                                      frameState.carYawDeg);
        }
        if (frameState.resetRequested)
        {
            frameState.carWorldPosition = frameState.respawnPosition;
            frameState.carYawDeg = frameState.respawnYawDeg;
            frameState.resetRequested = false;
        }
        if (context_.audioEvents)
        {
            context_.audioEvents->OnFrame(frameState);
        }
        context_.carWorldPosition = frameState.carWorldPosition;
        carYawDeg_ = frameState.carYawDeg;
        latestActiveSegmentId_ = frameState.activeSegmentId;
    }

    void ScheduleCarPrepareIfEnabled()
    {
        if (!CanRenderCar() || !context_.enableSlaveForCarPrepare) return;
        if (carPrepareJobInFlight_) return;

        const uint8_t slot = carPrepareWriteIdx_;
        carPrepareInputYaw_[slot] = carYawDeg_;
        carPrepareTask_.Configure(&carPrepareInputYaw_[slot], &carPrepareOutputYaw_[slot]);
        SRL::Slave::ExecuteOnSlave(carPrepareTask_);
        carPrepareJobInFlight_ = true;
        carPrepareInFlightIdx_ = slot;
        carPrepareWriteIdx_ ^= 1u;
    }

    void UpdateBackground()
    {
        if (!context_.enableBg || !context_.bgManager) return;
        AppState::Set(AppState::Stage::LoopBackground, frameCounter_);
        context_.bgManager->Update(context_.cameraSystem->State());
    }

    CameraFrameState ResolveCameraFrameState()
    {
        const SRL::Math::Types::Vector3D rawCameraLocation =
            context_.cameraSystem->CameraLocation(context_.carWorldPosition);
        const SRL::Math::Types::Vector3D rawLookTarget =
            context_.cameraSystem->LookTarget(context_.carWorldPosition, context_.modelOffset);
        const bool cameraReady =
            IsFiniteCameraPoint(rawCameraLocation) &&
            IsFiniteCameraPoint(rawLookTarget);
        if (cameraReady)
        {
            lastValidCameraLocation_ = rawCameraLocation;
            lastValidLookTarget_ = rawLookTarget;
        }

        CameraFrameState frame{};
        frame.ready = cameraReady;
        frame.location = cameraReady ? rawCameraLocation : lastValidCameraLocation_;
        frame.lookTarget = cameraReady ? rawLookTarget : lastValidLookTarget_;

        if (context_.verboseFrameLogs)
        {
            SRL::Debug::Print(0, 18, "Cam pos: %d %d %d",
                              frame.location.X.As<int16_t>(),
                              frame.location.Y.As<int16_t>(),
                              frame.location.Z.As<int16_t>());
        }
        return frame;
    }

    void UpdateHud(const CameraFrameState& camera)
    {
        context_.hudSystem->Update(context_.cameraSystem->State(),
                                   context_.modelOffset,
                                   camera.location,
                                   context_.carWorldPosition);
    }

    void RenderCar(const CameraFrameState& /*camera*/)
    {
        if (!CanRenderCar()) return;

        AppState::Set(AppState::Stage::LoopCar, frameCounter_);
        Game::CarSystem* car = ActiveCarSystem();

        SRL::Math::Types::Vector3D carRenderPos = context_.carWorldPosition;
        if (!IsFiniteCarPos(carRenderPos))
        {
            carRenderPos = lastValidCarRenderPos_;
        }
        else
        {
            lastValidCarRenderPos_ = carRenderPos;
        }

        car->SetWorldPosition(carRenderPos);
        if (context_.enableSlaveForCarPrepare && carPrepareHasCompleted_)
        {
            car->SetYawDegrees(carPrepareOutputYaw_[carPrepareCompletedIdx_]);
            car->TickCommandState();
        }
        else
        {
            car->Render(carYawDeg_);
        }

        context_.renderPipeline->Reset();
        car->SubmitRender(*context_.renderPipeline);
        context_.renderPipeline->Flush();
    }

    void RenderFrame(const CameraFrameState& camera)
    {
        using SRL::Math::Types::Angle;
        if (!camera.ready)
        {
            SRL::Debug::Print(1, 23, "CAM wait snapshot");
            hwrStageTrace_.trackDraw = MaybeCaptureHighWorkRamSnapshot();
            lwrStageTrace_.trackDraw = MaybeCaptureLowWorkRamSnapshot();
            hwrStageTrace_.trackEnd = hwrStageTrace_.trackDraw;
            lwrStageTrace_.trackEnd = lwrStageTrace_.trackDraw;
            hwrStageTrace_.track = MaybeCaptureHighWorkRamSnapshot();
            lwrStageTrace_.track = MaybeCaptureLowWorkRamSnapshot();
            hwrStageTrace_.car = hwrStageTrace_.track;
            lwrStageTrace_.car = lwrStageTrace_.track;
            return;
        }

        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(camera.location, camera.lookTarget, Angle::FromDegrees(0.0));

        if (context_.trackSystemReady && context_.renderTrack)
        {
            context_.trackSystem->SetObservedCarSegmentId(latestActiveSegmentId_);
        }
        SetWorkRamDebugTag(SRL::Memory::DebugTag::TrackCore);
        context_.trackSystem->BeginFrame(frameCounter_);
        if (context_.trackSystemReady && context_.renderTrack)
        {
            AppState::Set(AppState::Stage::LoopTrack, frameCounter_);
            SetWorkRamDebugTag(SRL::Memory::DebugTag::TrackPrepare);
            context_.trackSystem->RenderFrame(true,
                                             context_.trackSegOffset,
                                             context_.lightDirection,
                                             camera.location,
                                             context_.carWorldPosition);
            hwrStageTrace_.trackDraw = MaybeCaptureHighWorkRamSnapshot();
            lwrStageTrace_.trackDraw = MaybeCaptureLowWorkRamSnapshot();
            SetWorkRamDebugTag(SRL::Memory::DebugTag::TrackCore);
            context_.trackSystem->EndFrame();
            hwrStageTrace_.trackEnd = MaybeCaptureHighWorkRamSnapshot();
            lwrStageTrace_.trackEnd = MaybeCaptureLowWorkRamSnapshot();
        }
        else
        {
            hwrStageTrace_.trackDraw = MaybeCaptureHighWorkRamSnapshot();
            lwrStageTrace_.trackDraw = MaybeCaptureLowWorkRamSnapshot();
            hwrStageTrace_.trackEnd = hwrStageTrace_.trackDraw;
            lwrStageTrace_.trackEnd = lwrStageTrace_.trackDraw;
        }
        hwrStageTrace_.track = hwrStageTrace_.trackEnd;
        lwrStageTrace_.track = lwrStageTrace_.trackEnd;

        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(camera.location, camera.lookTarget, Angle::FromDegrees(0.0));
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Car);
        RenderCar(camera);
        hwrStageTrace_.car = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.car = MaybeCaptureLowWorkRamSnapshot();
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Unknown);
    }

    void RenderAxes()
    {
        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector2D;
        using SRL::Math::Types::Vector3D;
        if (!context_.renderAxes) return;

        Vector2D o2D, x2D, y2D, z2D;
        SRL::Scene3D::ProjectToScreen(Vector3D(0.0, 0.0, 0.0), &o2D);
        SRL::Scene3D::ProjectToScreen(Vector3D(4.0, 0.0, 0.0), &x2D);
        SRL::Scene3D::ProjectToScreen(Vector3D(0.0, 4.0, 0.0), &y2D);
        SRL::Scene3D::ProjectToScreen(Vector3D(0.0, 0.0, 4.0), &z2D);
        const Fxp sort2D = 0;
        SRL::Scene2D::DrawLine(o2D, x2D, SRL::Types::HighColor::Colors::Red, sort2D);
        SRL::Scene2D::DrawLine(o2D, y2D, SRL::Types::HighColor::Colors::Green, sort2D);
        SRL::Scene2D::DrawLine(o2D, z2D, SRL::Types::HighColor::Colors::Blue, sort2D);
    }

    void FinishFrame()
    {
        uint32_t submittedTrackFaces = 0;
        if (context_.trackSystemReady && context_.renderTrack && context_.trackSystem)
        {
            submittedTrackFaces = context_.trackSystem->Telemetry().submittedTrackFaces;
        }
        const uint32_t submittedCarFaces = CanRenderCar() ? context_.faceCount : 0;

        ++frameCounter_;
        hwrStageTrace_.preFinish = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.preFinish = MaybeCaptureLowWorkRamSnapshot();
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Finish);
        context_.hudSystem->PresentPeriodicFrameStats(frameCounter_,
                                                      context_.enableRuntimeStatsLogs,
                                                      context_.logTrack,
                                                      context_.logCar,
                                                      context_.faceCount,
                                                      context_.vertexCount,
                                                      submittedTrackFaces,
                                                      submittedCarFaces);
        if (context_.enableManualGouraudCopy)
        {
            SRL::Scene3D::LightCopyGouraudTable();
        }
        if (context_.verboseFrameLogs)
        {
            SRL::Debug::Print(1, 15, "SRL::Core::Synchronize frame:%u", frameCounter_);
        }
        hwrStageTrace_.preSync = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.preSync = MaybeCaptureLowWorkRamSnapshot();
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Sync);
        AppState::Set(AppState::Stage::LoopSync, frameCounter_);
        SRL::Core::Synchronize();
        hwrStageTrace_.postSync = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.postSync = MaybeCaptureLowWorkRamSnapshot(true);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Unknown);
        constexpr bool kEnableWorkRamOverlayTelemetry = kEnableDetailedWorkRamTelemetry;
        constexpr uint16_t kWorkRamOverlayCadenceFrames = 10u;
        bool workRamOverlayDue = false;
        if constexpr (kEnableWorkRamOverlayTelemetry)
        {
            if (lwrTraceCooldownFrames_ == 0u)
            {
                workRamOverlayDue = true;
                lwrTraceCooldownFrames_ = kWorkRamOverlayCadenceFrames;
            }
            else
            {
                --lwrTraceCooldownFrames_;
            }
            if (workRamOverlayDue)
            {
                PrintWorkRamUsageRealtime();
                MaybeLogHighWorkRamTrace();
                MaybeLogLowWorkRamTrace();
            }
        }
        UpdateLowWorkFreeOverlay();
    }

    static int32_t NormalizeYawDeg360(int32_t yawDeg)
    {
        yawDeg %= 360;
        if (yawDeg < 0) yawDeg += 360;
        return yawDeg;
    }

    static int32_t NormalizeSignedDeg180(int32_t deg)
    {
        deg = NormalizeYawDeg360(deg);
        if (deg > 180) deg -= 360;
        return deg;
    }

    static int32_t ShortestDeltaDeg(int32_t fromDeg, int32_t toDeg)
    {
        return NormalizeSignedDeg180(toDeg - fromDeg);
    }

    static int32_t YawFromDeltaRaw(int32_t deltaXRaw, int32_t deltaZRaw, int32_t fallbackYawDeg)
    {
        if (deltaXRaw == 0 && deltaZRaw == 0)
        {
            return NormalizeYawDeg360(fallbackYawDeg);
        }

        // Keep the existing heading convention used by gameplay/car rendering:
        // 0 deg = -Z, 90 deg = +X, 180 deg = +Z, 270 deg = -X.
        const auto angle = SRL::Math::Trigonometry::Atan2(
            SRL::Math::Types::Fxp::BuildRaw(deltaXRaw),
            SRL::Math::Types::Fxp::BuildRaw(-deltaZRaw));
        const auto yawDegFxp = angle.ToDegrees();
        const int32_t yawDeg = (yawDegFxp.RawValue() + (1 << 15)) >> 16;
        // Auto-lap uses the car visual forward axis, which is opposite to the
        // raw path tangent convention used by the movement vector.
        return NormalizeYawDeg360(yawDeg + kAutoLapYawBiasDeg);
    }

    static int32_t LerpShortestAngleDeg(int32_t aDeg, int32_t bDeg, int32_t tRaw)
    {
        const int32_t clampedTRaw = std::clamp<int32_t>(tRaw, 0, (1 << 16));
        const int32_t delta = ShortestDeltaDeg(aDeg, bDeg);
        const int32_t mixed =
            aDeg + static_cast<int32_t>((static_cast<int64_t>(delta) * clampedTRaw) >> 16);
        return NormalizeSignedDeg180(mixed);
    }

    void RebuildAutoLapRouteYawData()
    {
        autoLapRouteYawDeg_.clear();
        autoLapRouteOffDeg_.clear();
        autoLapRouteBaseYawDeg_ = 0;

        const size_t pointCount = autoLapRouteCenters_.size();
        if (pointCount < 2u) return;

        autoLapRouteYawDeg_.reserve(pointCount);
        for (size_t i = 0u; i < pointCount; ++i)
        {
            const size_t j = (i + 1u) % pointCount;
            const int32_t dxRaw =
                autoLapRouteCenters_[j].X.RawValue() - autoLapRouteCenters_[i].X.RawValue();
            const int32_t dzRaw =
                autoLapRouteCenters_[j].Z.RawValue() - autoLapRouteCenters_[i].Z.RawValue();
            const int32_t fallbackYaw = autoLapRouteYawDeg_.empty()
                                            ? 0
                                            : static_cast<int32_t>(autoLapRouteYawDeg_.back());
            const int32_t yawDeg = YawFromDeltaRaw(dxRaw, dzRaw, fallbackYaw);
            autoLapRouteYawDeg_.push_back(static_cast<int16_t>(yawDeg));
        }

        autoLapRouteBaseYawDeg_ = static_cast<int16_t>(autoLapRouteYawDeg_.front());
        autoLapRouteOffDeg_.reserve(pointCount);
        for (size_t i = 0u; i < pointCount; ++i)
        {
            const int32_t offDeg = ShortestDeltaDeg(
                static_cast<int32_t>(autoLapRouteBaseYawDeg_),
                static_cast<int32_t>(autoLapRouteYawDeg_[i]));
            autoLapRouteOffDeg_.push_back(static_cast<int16_t>(offDeg));
        }
    }

    // Advance car through the preferred route. When PATH.NYA is available and
    // populated, the middle line drives the player route; otherwise we fall
    // back to the segment-center path.
    void UpdateAutoLapRoute(const Context& context,
                            SRL::Math::Types::Vector3D& ioCarWorldPosition,
                            int32_t& ioCarYawDeg)
    {
        if (!context.trackSystem || !context.trackSystemReady) return;
        if (!autoLapRouteBuilt_)
        {
            BuildAutoLapRoute(context, ioCarWorldPosition);
        }
        if (autoLapRouteCenters_.size() < 2) return;
        if (autoLapRouteYawDeg_.size() != autoLapRouteCenters_.size() ||
            autoLapRouteOffDeg_.size() != autoLapRouteCenters_.size())
        {
            RebuildAutoLapRouteYawData();
        }
        const auto rideHeight = SRL::Math::Types::Fxp::BuildRaw(-3 << 16);
        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());
        auto wrapSegmentId = [&](int32_t segmentId) -> int32_t
        {
            if (segmentCount <= 0) return -1;
            int32_t normalized = (segmentId - 1) % segmentCount;
            if (normalized < 0) normalized += segmentCount;
            return normalized + 1;
        };
        auto advanceObservedSegmentToward = [&](int32_t desiredSegmentId)
        {
            if (desiredSegmentId <= 0 || segmentCount <= 0)
            {
                latestActiveSegmentId_ = desiredSegmentId;
                return;
            }

            desiredSegmentId = wrapSegmentId(desiredSegmentId);
            if (latestActiveSegmentId_ <= 0)
            {
                latestActiveSegmentId_ = static_cast<int16_t>(desiredSegmentId);
                return;
            }

            const int32_t currentSegmentId = wrapSegmentId(latestActiveSegmentId_);
            if (currentSegmentId <= 0)
            {
                latestActiveSegmentId_ = static_cast<int16_t>(desiredSegmentId);
                return;
            }

            const int32_t total = segmentCount;
            int32_t forwardDistance = (desiredSegmentId - currentSegmentId) % total;
            if (forwardDistance < 0) forwardDistance += total;
            if (forwardDistance == 0)
            {
                latestActiveSegmentId_ = static_cast<int16_t>(currentSegmentId);
                return;
            }

            if (forwardDistance < (total / 2))
            {
                latestActiveSegmentId_ =
                    static_cast<int16_t>(wrapSegmentId(currentSegmentId + 1));
                return;
            }

            // Ignore backward/noisy remaps from the decimated PATH so the
            // streaming window does not thrash or try to catch up by several
            // segments in one frame.
            latestActiveSegmentId_ = static_cast<int16_t>(currentSegmentId);
        };

        if (!autoLapRouteInitialized_)
        {
            size_t bestIdx = 0u;
            bool foundRoutePoint = false;
            SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
            for (size_t i = 0; i < autoLapRouteCenters_.size(); ++i)
            {
                const auto& routePoint = autoLapRouteCenters_[i];
                const auto dx = (routePoint.X - ioCarWorldPosition.X).Abs();
                const auto dz = (routePoint.Z - ioCarWorldPosition.Z).Abs();
                const auto score = dx + dz;
                if (!foundRoutePoint || score < bestScore)
                {
                    bestIdx = i;
                    bestScore = score;
                    foundRoutePoint = true;
                }
            }
            if (!foundRoutePoint) return;

            autoLapRouteIndex_ = static_cast<uint16_t>(bestIdx);
            ioCarWorldPosition.X = autoLapRouteCenters_[autoLapRouteIndex_].X;
            ioCarWorldPosition.Z = autoLapRouteCenters_[autoLapRouteIndex_].Z;
            ioCarWorldPosition.Y = autoLapRouteCenters_[autoLapRouteIndex_].Y + rideHeight;
            autoLapRouteInitialized_ = true;
            if (autoLapRouteYawDeg_.size() == autoLapRouteCenters_.size() &&
                autoLapRouteIndex_ < autoLapRouteYawDeg_.size())
            {
                ioCarYawDeg = autoLapRouteYawDeg_[autoLapRouteIndex_];
            }
            if (!autoLapRouteIds_.empty() && autoLapRouteIndex_ < autoLapRouteIds_.size())
            {
                advanceObservedSegmentToward(autoLapRouteIds_[autoLapRouteIndex_]);
            }
        }

        const size_t routePointCount = autoLapRouteCenters_.size();
        size_t currentIndex = static_cast<size_t>(autoLapRouteIndex_);
        size_t nextIndex = (currentIndex + 1u) % routePointCount;
        const SRL::Math::Types::Vector3D& currentCenter = autoLapRouteCenters_[currentIndex];
        const SRL::Math::Types::Vector3D& nextCenter = autoLapRouteCenters_[nextIndex];
        const int32_t prevCarXRaw = ioCarWorldPosition.X.RawValue();
        const int32_t prevCarZRaw = ioCarWorldPosition.Z.RawValue();

        // Move from current car position to next segment center.
        const int32_t ndx = nextCenter.X.RawValue() - ioCarWorldPosition.X.RawValue();
        const int32_t ndz = nextCenter.Z.RawValue() - ioCarWorldPosition.Z.RawValue();
        const int32_t nAdx = (ndx < 0) ? -ndx : ndx;
        const int32_t nAdz = (ndz < 0) ? -ndz : ndz;
        const int32_t maxAxis = (nAdx > nAdz) ? nAdx : nAdz;
        if (maxAxis > 0)
        {
            const int32_t stepRaw = (autoLapStepUnits_ << 16);
            const int64_t moveX64 = (static_cast<int64_t>(ndx) * static_cast<int64_t>(stepRaw)) / maxAxis;
            const int64_t moveZ64 = (static_cast<int64_t>(ndz) * static_cast<int64_t>(stepRaw)) / maxAxis;
            ioCarWorldPosition.X += SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(moveX64));
            ioCarWorldPosition.Z += SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(moveZ64));
        }

        // Advance route when the car reaches next center.
        const int32_t tx = nextCenter.X.RawValue() - ioCarWorldPosition.X.RawValue();
        const int32_t tz = nextCenter.Z.RawValue() - ioCarWorldPosition.Z.RawValue();
        const int32_t atx = (tx < 0) ? -tx : tx;
        const int32_t atz = (tz < 0) ? -tz : tz;
        const int32_t totalDx = nextCenter.X.RawValue() - currentCenter.X.RawValue();
        const int32_t totalDz = nextCenter.Z.RawValue() - currentCenter.Z.RawValue();
        const int32_t totalAdx = (totalDx < 0) ? -totalDx : totalDx;
        const int32_t totalAdz = (totalDz < 0) ? -totalDz : totalDz;
        const int32_t totalAxis = (totalAdx > totalAdz) ? totalAdx : totalAdz;
        if (totalAxis > 0)
        {
            const int32_t remAxis = (atx > atz) ? atx : atz;
            const int32_t alphaRaw = ((totalAxis - remAxis) << 16) / totalAxis;
            const auto alpha = SRL::Math::Types::Fxp::BuildRaw(std::clamp<int32_t>(alphaRaw, 0, (1 << 16)));
            ioCarWorldPosition.Y = currentCenter.Y + ((nextCenter.Y - currentCenter.Y) * alpha) + rideHeight;
        }
        else
        {
            ioCarWorldPosition.Y = nextCenter.Y + rideHeight;
        }

        auto reachedOrPassedWaypoint = [&](size_t fromIndex, size_t toIndex) -> bool
        {
            const auto& from = autoLapRouteCenters_[fromIndex];
            const auto& to = autoLapRouteCenters_[toIndex];
            const int32_t remX = to.X.RawValue() - ioCarWorldPosition.X.RawValue();
            const int32_t remZ = to.Z.RawValue() - ioCarWorldPosition.Z.RawValue();
            const int32_t absRemX = (remX < 0) ? -remX : remX;
            const int32_t absRemZ = (remZ < 0) ? -remZ : remZ;
            if (absRemX <= (8 << 16) && absRemZ <= (8 << 16)) return true;

            const int64_t segX = static_cast<int64_t>(to.X.RawValue()) -
                                 static_cast<int64_t>(from.X.RawValue());
            const int64_t segZ = static_cast<int64_t>(to.Z.RawValue()) -
                                 static_cast<int64_t>(from.Z.RawValue());
            const int64_t toCarX = static_cast<int64_t>(ioCarWorldPosition.X.RawValue()) -
                                   static_cast<int64_t>(to.X.RawValue());
            const int64_t toCarZ = static_cast<int64_t>(ioCarWorldPosition.Z.RawValue()) -
                                   static_cast<int64_t>(to.Z.RawValue());
            return ((segX * toCarX) + (segZ * toCarZ)) >= 0;
        };

        for (size_t guard = 0u; guard < 8u; ++guard)
        {
            if (!reachedOrPassedWaypoint(currentIndex, nextIndex)) break;
            currentIndex = nextIndex;
            nextIndex = (currentIndex + 1u) % routePointCount;
            ioCarWorldPosition.Y = autoLapRouteCenters_[currentIndex].Y + rideHeight;
        }
        autoLapRouteIndex_ = static_cast<uint16_t>(currentIndex);

        if (!autoLapRouteIds_.empty() && autoLapRouteIndex_ < autoLapRouteIds_.size())
        {
            advanceObservedSegmentToward(autoLapRouteIds_[autoLapRouteIndex_]);
        }

        const auto normalizeYawDeg = [](int32_t yawDeg) -> int32_t
        {
            return NormalizeYawDeg360(yawDeg);
        };
        const size_t headingA = currentIndex;
        const size_t headingB = nextIndex;
        // Align car heading with effective movement direction so visual forward
        // matches the camera follow vector on curves.
        const int32_t moveDxRaw = ioCarWorldPosition.X.RawValue() - prevCarXRaw;
        const int32_t moveDzRaw = ioCarWorldPosition.Z.RawValue() - prevCarZRaw;
        const int32_t moveAxis = std::max(std::abs(moveDxRaw), std::abs(moveDzRaw));

        // Fallback to active PATH edge heading when movement delta is tiny.
        const int32_t pathDxRaw = autoLapRouteCenters_[headingB].X.RawValue() -
                                  autoLapRouteCenters_[headingA].X.RawValue();
        const int32_t pathDzRaw = autoLapRouteCenters_[headingB].Z.RawValue() -
                                  autoLapRouteCenters_[headingA].Z.RawValue();
        int32_t targetYawDeg = YawFromDeltaRaw(pathDxRaw, pathDzRaw, ioCarYawDeg);
        constexpr int32_t kMinMotionForYawRaw = (1 << 10);
        if (moveAxis > kMinMotionForYawRaw)
        {
            targetYawDeg = YawFromDeltaRaw(moveDxRaw, moveDzRaw, targetYawDeg);
        }

        ioCarYawDeg = normalizeYawDeg(targetYawDeg);
        if (autoLapRouteYawDeg_.size() == autoLapRouteCenters_.size() &&
            !autoLapRouteYawDeg_.empty())
        {
            autoLapCurrentOffDeg_ = static_cast<int16_t>(
                ShortestDeltaDeg(static_cast<int32_t>(autoLapRouteBaseYawDeg_), ioCarYawDeg));
        }
        else
        {
            autoLapCurrentOffDeg_ = 0;
        }
    }

    static bool ReadCdBinaryFile(const char* path, std::vector<uint8_t>& outBytes)
    {
        outBytes.clear();
        if (!path || path[0] == '\0') return false;

        SRL::Cd::File f(path);
        if (!f.Exists() || f.Size.Bytes <= 0) return false;
        if (!f.Open()) return false;

        const size_t size = static_cast<size_t>(std::max<int32_t>(0, f.Size.Bytes));
        if (size == 0u) return false;

        outBytes.resize(size);
        size_t totalRead = 0u;
        while (totalRead < size)
        {
            const int32_t want = static_cast<int32_t>(std::min<size_t>(2048u, size - totalRead));
            const int32_t got = f.Read(want, outBytes.data() + totalRead);
            if (got <= 0) break;
            totalRead += static_cast<size_t>(got);
            if (got < want) break;
        }

        if (totalRead != size)
        {
            outBytes.clear();
            return false;
        }

        return true;
    }

    bool LoadAutoLapGuideLines()
    {
        for (size_t i = 0; i < autoLapGuideLines_.size(); ++i)
        {
            autoLapGuideLines_[i].clear();
        }

        const char* candidates[] = {
            "/CD/DATA/PATH.NYA",
            "/CD/DATA/PATH.NYA;1",
            "/DATA/PATH.NYA",
            "/DATA/PATH.NYA;1",
            "CD/DATA/PATH.NYA",
            "CD/DATA/PATH.NYA;1",
            "DATA/PATH.NYA",
            "DATA/PATH.NYA;1",
            "cd/data/PATH.NYA",
            "cd/data/PATH.NYA;1",
            "data/PATH.NYA",
            "data/PATH.NYA;1",
            "/PATH.NYA",
            "/PATH.NYA;1",
            "PATH.NYA",
            "PATH.NYA;1",
        };

        std::vector<uint8_t> bytes{};
        bool loaded = false;
        const char* loadedCandidate = nullptr;
        for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i)
        {
            if (!ReadCdBinaryFile(candidates[i], bytes)) continue;
            loaded = true;
            loadedCandidate = candidates[i];
            break;
        }
        if (!loaded)
        {
            SRL::Debug::Print(1, 23, "AUTO PATH read fail");
            return false;
        }

        PathNya::ParseResult parsed{};
        if (!PathNya::Parse(bytes.data(), bytes.size(), parsed))
        {
            SRL::Debug::Print(1, 23, "AUTO PATH parse fail %s",
                              loadedCandidate ? loadedCandidate : "none");
            SRL::Debug::Print(1, 24, "AUTO PATH parse sz:%u",
                              static_cast<unsigned>(bytes.size()));
            return false;
        }

        for (size_t lineIndex = 0; lineIndex < autoLapGuideLines_.size(); ++lineIndex)
        {
            const auto& srcLine = parsed.lines[lineIndex];
            auto& dstLine = autoLapGuideLines_[lineIndex];
            dstLine.reserve(srcLine.size());
            for (size_t pointIndex = 0; pointIndex < srcLine.size(); ++pointIndex)
            {
                const auto& srcPoint = srcLine[pointIndex];
                dstLine.push_back(SRL::Math::Types::Vector3D(
                    SRL::Math::Types::Fxp::BuildRaw(srcPoint.xRaw),
                    SRL::Math::Types::Fxp::BuildRaw(srcPoint.yRaw),
                    SRL::Math::Types::Fxp::BuildRaw(srcPoint.zRaw)));
            }
        }

        SRL::Debug::Print(1, 23, "AUTO PATH ok %s",
                          loadedCandidate ? loadedCandidate : "none");
        SRL::Debug::Print(1, 24, "AUTO PATH v:%u l0:%u l1:%u l2:%u",
                          static_cast<unsigned>(parsed.version),
                          static_cast<unsigned>(autoLapGuideLines_[0].size()),
                          static_cast<unsigned>(autoLapGuideLines_[1].size()),
                          static_cast<unsigned>(autoLapGuideLines_[2].size()));
        return true;
    }

    bool BuildAutoLapRouteFromPathGuide(const Context& context,
                                        const SRL::Math::Types::Vector3D& referenceCarWorldPosition)
    {
        if (!LoadAutoLapGuideLines()) return false;

        int32_t selectedLineIndex = -1;
        SRL::Math::Types::Fxp selectedScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        for (int32_t lineIndex = 0; lineIndex < static_cast<int32_t>(autoLapGuideLines_.size()); ++lineIndex)
        {
            const auto& line = autoLapGuideLines_[lineIndex];
            if (line.size() < 2u) continue;

            SRL::Math::Types::Fxp lineScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
            for (size_t i = 0; i < line.size(); ++i)
            {
                const auto worldPoint = line[i] + context.trackSegOffset;
                const auto dx = (worldPoint.X - referenceCarWorldPosition.X).Abs();
                const auto dz = (worldPoint.Z - referenceCarWorldPosition.Z).Abs();
                const auto score = dx + dz;
                if (score < lineScore) lineScore = score;
            }

            if (selectedLineIndex < 0 || lineScore < selectedScore)
            {
                selectedLineIndex = lineIndex;
                selectedScore = lineScore;
            }
        }
        if (selectedLineIndex < 0)
        {
            SRL::Debug::Print(1, 24, "AUTO PATH line empty");
            return false;
        }
        autoLapSelectedGuideLine_ = static_cast<int8_t>(selectedLineIndex);
        const auto& selectedLine = autoLapGuideLines_[static_cast<size_t>(selectedLineIndex)];

        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());
        if (segmentCount <= 0) return false;

        const auto simplifiedMiddleLine =
            SimplifyAutoLapGuideLine(selectedLine, static_cast<size_t>(segmentCount));
        const auto& routeLine = (simplifiedMiddleLine.size() >= 2u) ? simplifiedMiddleLine : selectedLine;

        autoLapRouteCenters_.reserve(routeLine.size());
        autoLapRouteIds_.reserve(routeLine.size());

        auto scoreToSegmentId = [&](const SRL::Math::Types::Vector3D& point,
                                    int32_t segmentId) -> SRL::Math::Types::Fxp
        {
            SRL::Math::Types::Vector3D center{};
            if (!context.trackSystem->FindSegmentCenterById(segmentId, context.trackSegOffset, center))
            {
                return SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
            }
            return (center.X - point.X).Abs() + (center.Z - point.Z).Abs();
        };

        auto wrapSegmentId = [&](int32_t segmentId) -> int32_t
        {
            int32_t normalized = (segmentId - 1) % segmentCount;
            if (normalized < 0) normalized += segmentCount;
            return normalized + 1;
        };

        int32_t mappedSegmentId = -1;
        for (size_t i = 0; i < routeLine.size(); ++i)
        {
            const SRL::Math::Types::Vector3D routePoint = routeLine[i] + context.trackSegOffset;
            autoLapRouteCenters_.push_back(routePoint);

            int32_t bestSegmentId = -1;
            SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
            if (mappedSegmentId <= 0)
            {
                for (int32_t segmentId = 1; segmentId <= segmentCount; ++segmentId)
                {
                    const auto score = scoreToSegmentId(routePoint, segmentId);
                    if (bestSegmentId > 0 && !(score < bestScore)) continue;
                    bestSegmentId = segmentId;
                    bestScore = score;
                }
            }
            else
            {
                // The exported PATH guide has fewer points than the full segment catalog,
                // so some consecutive guide points legitimately advance by up to ~5 segments.
                // Keep the search forward-biased, but wide enough to avoid freezing the
                // mapping on the current segment near the end of the lap.
                static constexpr int32_t kBackSearch = 0;
                static constexpr int32_t kForwardSearch = 12;
                for (int32_t delta = -kBackSearch; delta <= kForwardSearch; ++delta)
                {
                    const int32_t segmentId = wrapSegmentId(mappedSegmentId + delta);
                    const auto score = scoreToSegmentId(routePoint, segmentId);
                    if (bestSegmentId > 0 && !(score < bestScore)) continue;
                    bestSegmentId = segmentId;
                    bestScore = score;
                }
            }

            if (bestSegmentId <= 0) return false;
            mappedSegmentId = bestSegmentId;
            autoLapRouteIds_.push_back(static_cast<int16_t>(mappedSegmentId));
        }

        // If PATH points were exported in reverse order, the car will turn in
        // the opposite direction at curves. Normalize to forward segment flow.
        int32_t directionScore = 0;
        if (autoLapRouteIds_.size() >= 2u)
        {
            for (size_t i = 0; i < autoLapRouteIds_.size(); ++i)
            {
                const int32_t fromId = autoLapRouteIds_[i];
                const int32_t toId = autoLapRouteIds_[(i + 1u) % autoLapRouteIds_.size()];
                if (fromId <= 0 || toId <= 0) continue;
                int32_t forwardDelta = (toId - fromId) % segmentCount;
                if (forwardDelta < 0) forwardDelta += segmentCount;
                if (forwardDelta == 0) continue;
                if (forwardDelta <= (segmentCount / 2))
                {
                    ++directionScore;
                }
                else
                {
                    --directionScore;
                }
            }
        }

        if (directionScore < 0)
        {
            std::reverse(autoLapRouteCenters_.begin(), autoLapRouteCenters_.end());
            std::reverse(autoLapRouteIds_.begin(), autoLapRouteIds_.end());
            SRL::Debug::Print(1, 26, "AUTO PATH dir:REV fix");
        }
        else
        {
            SRL::Debug::Print(1, 26, "AUTO PATH dir:FWD");
        }

        SRL::Debug::Print(1, 25, "AUTO PATH l:%d raw:%u out:%u",
                          static_cast<int>(selectedLineIndex),
                          static_cast<unsigned>(selectedLine.size()),
                          static_cast<unsigned>(routeLine.size()));
        RebuildAutoLapRouteYawData();
        return !autoLapRouteCenters_.empty() &&
               autoLapRouteCenters_.size() == autoLapRouteIds_.size() &&
               autoLapRouteCenters_.size() == autoLapRouteYawDeg_.size() &&
               autoLapRouteCenters_.size() == autoLapRouteOffDeg_.size();
    }

    void ReleaseAutoLapGuideLines()
    {
        for (size_t i = 0; i < autoLapGuideLines_.size(); ++i)
        {
            using GuideLineVector = std::remove_reference_t<decltype(autoLapGuideLines_[i])>;
            GuideLineVector{}.swap(autoLapGuideLines_[i]);
        }
    }

    static double AutoLapPointSegmentDistanceSqXZ(const SRL::Math::Types::Vector3D& point,
                                                  const SRL::Math::Types::Vector3D& a,
                                                  const SRL::Math::Types::Vector3D& b)
    {
        const double px = static_cast<double>(point.X.RawValue());
        const double pz = static_cast<double>(point.Z.RawValue());
        const double ax = static_cast<double>(a.X.RawValue());
        const double az = static_cast<double>(a.Z.RawValue());
        const double bx = static_cast<double>(b.X.RawValue());
        const double bz = static_cast<double>(b.Z.RawValue());

        const double vx = bx - ax;
        const double vz = bz - az;
        const double wx = px - ax;
        const double wz = pz - az;
        const double vv = (vx * vx) + (vz * vz);
        if (vv <= 0.0)
        {
            return (wx * wx) + (wz * wz);
        }

        double t = ((wx * vx) + (wz * vz)) / vv;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;

        const double dx = px - (ax + (vx * t));
        const double dz = pz - (az + (vz * t));
        return (dx * dx) + (dz * dz);
    }

    static TrackLowWorkVector<SRL::Math::Types::Vector3D> SimplifyAutoLapGuideLine(
        const TrackLowWorkVector<SRL::Math::Types::Vector3D>& input,
        size_t segmentCount)
    {
        TrackLowWorkVector<SRL::Math::Types::Vector3D> output{};
        if (input.size() <= 2u)
        {
            output = input;
            return output;
        }

        const size_t targetMaxPoints = std::min<size_t>(
            std::max<size_t>(segmentCount * 6u, 1536u),
            2048u);
        if (input.size() <= targetMaxPoints)
        {
            output = input;
            return output;
        }

        constexpr double kEpsilonRaw = static_cast<double>(2 << 16);
        const double epsilonSq = kEpsilonRaw * kEpsilonRaw;

        std::vector<uint8_t> keep(input.size(), 0u);
        keep.front() = 1u;
        keep.back() = 1u;

        std::vector<std::pair<size_t, size_t>> stack{};
        stack.emplace_back(0u, input.size() - 1u);
        while (!stack.empty())
        {
            const auto range = stack.back();
            stack.pop_back();
            if (range.second <= range.first + 1u) continue;

            size_t farthestIndex = 0u;
            double farthestDistSq = -1.0;
            for (size_t i = range.first + 1u; i < range.second; ++i)
            {
                const double distSq =
                    AutoLapPointSegmentDistanceSqXZ(input[i], input[range.first], input[range.second]);
                if (distSq <= farthestDistSq) continue;
                farthestDistSq = distSq;
                farthestIndex = i;
            }

            if (farthestDistSq > epsilonSq)
            {
                keep[farthestIndex] = 1u;
                stack.emplace_back(range.first, farthestIndex);
                stack.emplace_back(farthestIndex, range.second);
            }
        }

        output.reserve(std::min(targetMaxPoints, input.size()));
        for (size_t i = 0; i < input.size(); ++i)
        {
            if (keep[i] == 0u) continue;
            output.push_back(input[i]);
        }

        if (output.size() <= targetMaxPoints)
        {
            return output;
        }

        TrackLowWorkVector<SRL::Math::Types::Vector3D> capped{};
        capped.reserve(targetMaxPoints);
        const size_t lastIndex = output.size() - 1u;
        for (size_t i = 0; i < targetMaxPoints; ++i)
        {
            const size_t srcIndex =
                (i * lastIndex) / std::max<size_t>(1u, targetMaxPoints - 1u);
            if (!capped.empty() &&
                capped.back().X.RawValue() == output[srcIndex].X.RawValue() &&
                capped.back().Y.RawValue() == output[srcIndex].Y.RawValue() &&
                capped.back().Z.RawValue() == output[srcIndex].Z.RawValue())
            {
                continue;
            }
            capped.push_back(output[srcIndex]);
        }

        if (capped.size() >= 2u)
        {
            return capped;
        }
        return output;
    }

    // Build preferred route for the player car.
    void BuildAutoLapRoute(const Context& context,
                           const SRL::Math::Types::Vector3D& referenceCarWorldPosition)
    {
        autoLapRouteIds_.clear();
        autoLapRouteCenters_.clear();
        autoLapRouteYawDeg_.clear();
        autoLapRouteOffDeg_.clear();
        autoLapRouteBaseYawDeg_ = 0;
        autoLapSelectedGuideLine_ = -1;
        if (!context.trackSystem) return;

        if (BuildAutoLapRouteFromPathGuide(context, referenceCarWorldPosition))
        {
            ReleaseAutoLapGuideLines();
            autoLapRouteBuilt_ = true;
            autoLapRouteInitialized_ = false;
            return;
        }

        ReleaseAutoLapGuideLines();
        SRL::Debug::Print(1, 24, "AUTO PATH fallback seg centers");

        SRL::Math::Types::Vector3D c{};
        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());
        for (int32_t id = 1; id <= segmentCount; ++id)
        {
            if (!context.trackSystem->FindSegmentCenterById(id, context.trackSegOffset, c)) continue;
            autoLapRouteIds_.push_back(id);
            autoLapRouteCenters_.push_back(c);
        }
        RebuildAutoLapRouteYawData();
        autoLapRouteBuilt_ = !autoLapRouteIds_.empty();
        autoLapRouteInitialized_ = false;
    }

    class SimulationTask final : public SRL::Types::ITask
    {
    public:
        struct Payload
        {
            Game::IGameplayTick* gameplayTick = nullptr;
            Game::ICarPhysics* carPhysics = nullptr;
            Game::IAudioEvents* audioEvents = nullptr;
            Game::ITrackCollisionQuery* trackCollision = nullptr;
            Game::GameplayFrameState frameState{};
            SRL::Math::Types::Vector3D outWorldPosition{};
            int32_t outYawDeg = 0;
        };

        void Configure(const Payload* input, Payload* output)
        {
            input_ = input;
            output_ = output;
        }

    private:
        void Do() override
        {
            if (!input_ || !output_) return;
            auto state = input_->frameState;
            if (input_->gameplayTick)
            {
                input_->gameplayTick->Tick(state, input_->trackCollision);
            }
            if (input_->carPhysics)
            {
                input_->carPhysics->Step(state,
                                          input_->trackCollision,
                                          state.carWorldPosition,
                                          state.carYawDeg);
            }
            if (state.resetRequested)
            {
                state.carWorldPosition = state.respawnPosition;
                state.carYawDeg = state.respawnYawDeg;
                state.resetRequested = false;
            }
            if (input_->audioEvents)
            {
                input_->audioEvents->OnFrame(state);
            }

            *output_ = *input_;
            output_->frameState = state;
            output_->outWorldPosition = state.carWorldPosition;
            output_->outYawDeg = state.carYawDeg;
        }

        const Payload* input_ = nullptr;
        Payload* output_ = nullptr;
    };

    class CarRenderPrepareTask final : public SRL::Types::ITask
    {
    public:
        void Configure(const int32_t* inputYawDeg, int32_t* outputYawDeg)
        {
            inputYawDeg_ = inputYawDeg;
            outputYawDeg_ = outputYawDeg;
        }

    private:
        void Do() override
        {
            if (!inputYawDeg_ || !outputYawDeg_) return;
            int32_t yaw = *inputYawDeg_;
            yaw %= 360;
            if (yaw < 0) yaw += 360;
            *outputYawDeg_ = yaw;
        }

        const int32_t* inputYawDeg_ = nullptr;
        int32_t* outputYawDeg_ = nullptr;
    };

    Context context_{};
    SRL::Input::Digital pad_{0};
    CameraRig::OrbitState orbitState_{};
    int32_t carYawDeg_ = 0;
    uint32_t frameCounter_ = 0;
    SimulationTask simulationTask_{};
    SimulationTask::Payload simInput_[2]{};
    SimulationTask::Payload simOutput_[2]{};
    bool simJobInFlight_ = false;
    bool simHasCompleted_ = false;
    uint8_t simWriteIdx_ = 0;
    uint8_t simInFlightIdx_ = 0;
    uint8_t simCompletedIdx_ = 0;
    CarRenderPrepareTask carPrepareTask_{};
    int32_t carPrepareInputYaw_[2]{};
    int32_t carPrepareOutputYaw_[2]{};
    bool carPrepareJobInFlight_ = false;
    bool carPrepareHasCompleted_ = false;
    uint8_t carPrepareWriteIdx_ = 0;
    uint8_t carPrepareInFlightIdx_ = 0;
    uint8_t carPrepareCompletedIdx_ = 0;
    int16_t latestActiveSegmentId_ = -1;
    SRL::Math::Types::Vector3D lastValidCarRenderPos_{
        SRL::Math::Types::Fxp::BuildRaw(0),
        SRL::Math::Types::Fxp::BuildRaw(0),
        SRL::Math::Types::Fxp::BuildRaw(0)};
    SRL::Math::Types::Vector3D lastValidCameraLocation_{
        SRL::Math::Types::Fxp::BuildRaw(0),
        SRL::Math::Types::Fxp::BuildRaw(0),
        SRL::Math::Types::Fxp::BuildRaw(0)};
    SRL::Math::Types::Vector3D lastValidLookTarget_{
        SRL::Math::Types::Fxp::BuildRaw(0),
        SRL::Math::Types::Fxp::BuildRaw(0),
        SRL::Math::Types::Fxp::BuildRaw(0)};
    bool autoLapTestEnabled_ = false;
    int16_t autoLapTargetSegmentId_ = 1;
    int16_t autoLapStepUnits_ = 6;
    bool autoLapRouteInitialized_ = false;
    bool autoLapRouteBuilt_ = false;
    uint16_t autoLapRouteIndex_ = 0;
    TrackLowWorkVector<int16_t> autoLapRouteIds_{};
    TrackLowWorkVector<SRL::Math::Types::Vector3D> autoLapRouteCenters_{};
    TrackLowWorkVector<int16_t> autoLapRouteYawDeg_{};
    TrackLowWorkVector<int16_t> autoLapRouteOffDeg_{};
    int16_t autoLapRouteBaseYawDeg_ = 0;
    int16_t autoLapCurrentOffDeg_ = 0;
    int8_t autoLapSelectedGuideLine_ = -1;
    std::array<TrackLowWorkVector<SRL::Math::Types::Vector3D>, 3> autoLapGuideLines_{};
    HwrStageTrace hwrStageTrace_{};
    uint16_t hwrTraceCooldownFrames_ = 0;
    LwrStageTrace lwrStageTrace_{};
    uint16_t lwrTraceCooldownFrames_ = 0;
    uint16_t lowWorkFreeOverlayCooldownFrames_ = 0;
    uint32_t lastLowWorkFreeOverlayBytes_ = 0;
    bool lowWorkFreeOverlayValid_ = false;
    TrackSystem::LowWorkCategoryBreakdown lastLowWorkBreakdownOverlay_{};
    bool lowWorkBreakdownOverlayValid_ = false;
    LowWorkTagGroupOverlay lastLowWorkTagGroupOverlay_{};
    bool lowWorkTagGroupOverlayValid_ = false;
    uint32_t lastLowWorkPayloadOverlayBytes_ = 0;
    uint32_t lastLowWorkOverheadOverlayBytes_ = 0;
    uint32_t lastLowWorkFreeBlocksOverlay_ = 0;
    bool lowWorkAllocatorOverlayValid_ = false;
    bool yHeldPrev_ = false;
    bool leftHeldPrev_ = false;
    bool rightHeldPrev_ = false;
    uint8_t carForwardOffsetRepeatFrames_ = 0u;
};
