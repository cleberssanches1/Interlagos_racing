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
#include "physics_feature_flags.hpp"
#include "render_pipeline.hpp"
#include "sh2_frt_profiler.hpp"
#include "track_system.hpp"

extern "C" uint32_t SRL_AppGetVblankCounter();

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
        bool enableMinimalFpsOverlay = true;
        bool enableSlaveForCarPrepare = false;
        bool enableSlaveForSimulation = false;
        bool slaveSimulationLockstep = true;
        bool enableManualGouraudCopy = false;
        bool autoLapEnabledOnStart = true;
        bool allowAutoLapInputToggle = true;
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
        MeshRenderer* carShadowRenderer = nullptr;
        bool renderCarShadowModel = false;
        bool sbaLoaded = false;
        uint16_t sbaMeshCount = 0;
        uint16_t sbaFaceCount = 0;
        RenderPipeline* renderPipeline = nullptr;
        HudSystem* hudSystem = nullptr;
        Game::ITrackCollisionQuery* trackCollision = nullptr;
        Game::ICarPhysics* carPhysics = nullptr;
        Game::IGameplayTick* gameplayTick = nullptr;
        Game::IAudioEvents* audioEvents = nullptr;
    };

    explicit GameLoopSystem(const Context& context)
        : context_(context)
        , autoLapTestEnabled_(context.autoLapEnabledOnStart)
        , autoLapInputToggleEnabled_(context.allowAutoLapInputToggle)
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
            ResetPerFrameStageTraces();
            ResetPerFrameSimulationTelemetry();
            SetWorkRamDebugTag(SRL::Memory::DebugTag::Unknown);
            CaptureBeginStageTraces();

            const FrameInputState input = PollFrameInput();
            ConsumeCompletedJobs();

            SetWorkRamDebugTag(SRL::Memory::DebugTag::Gameplay);
            Game::GameplayFrameState frameState = BuildGameplayFrameState(input);
            ExecuteGameplayFrame(frameState);
            // Keep chase heading synced with gameplay/physics yaw every frame.
            SyncCameraHeadingFromCar();
            CaptureGameplayStageTraces();

            if (autoLapTestEnabled_)
            {
                SetWorkRamDebugTag(SRL::Memory::DebugTag::AutoLap);
                UpdateAutoLapRoute(context_, context_.carWorldPosition, carYawDeg_);
                SyncCameraHeadingFromCar();
            }
            CaptureAutoLapStageTraces();

            SetWorkRamDebugTag(SRL::Memory::DebugTag::Background);
            ScheduleCarPrepareIfEnabled();
            UpdateBackground();
            CaptureBackgroundStageTraces();

            const CameraFrameState camera = ResolveCameraFrameState();
            SetWorkRamDebugTag(SRL::Memory::DebugTag::Hud);
            UpdateHud(camera);
            CaptureHudStageTraces();
            RenderFrame(camera);
            RenderAxes();

            FinishFrame();
        }
    }

private:
    static constexpr int32_t kAutoLapYawBiasDeg = 0;

    static constexpr bool kEnableDetailedWorkRamTelemetry = false;
    // Overlay detalhado de LWR/HWR gera muito texto variavel por frame e
    // pode afetar performance durante diagnostico de streaming.
    static constexpr bool kEnableLowWorkFreeOverlay = false;
    // Modo completo imprime muitas linhas e pode degradar FPS em corrida longa.
    // Mantemos o modo leve por padrao para monitorar memoria com menor custo.
    static constexpr bool kEnableLowWorkFreeOverlayFull = false;
    // Permite ligar o overlay de memoria mesmo quando os logs gerais de runtime
    // estao desligados em main.cxx.
    static constexpr bool kEnableLowWorkFreeOverlayRequireRuntimeStats = false;
    // Lower overhead while keeping memory visibility on-screen.
    // Full allocator/tag overlay is expensive. Sample less often to keep frame
    // pacing stable during long soak runs.
    static constexpr uint16_t kLowWorkFreeOverlayCadenceFrames = 180u;
    static constexpr bool kEnableCameraRuntimeLogs = false;
    static constexpr bool kEnableAutoPathLogs = false;
    static constexpr bool kEnableSh2ToggleLogs = false;
    static constexpr bool kEnablePhysicsSafeTelemetry =
        Game::PhysicsFeatureFlags::kEnableSafeTelemetry;
    // Slave SH2 drain guardrails:
    // - soft: account wait and back off future dispatches
    // - hard: guarantee completion before entering track render window
    static constexpr uint32_t kSimDrainSoftSpinLimit = 512u * 1024u;
    static constexpr uint32_t kSimDrainHardSpinLimit = 8u * 1024u * 1024u;
    static constexpr uint8_t kSimSlaveBackoffFrames = 6u;

    struct SegmentOverlaySnapshot
    {
        int32_t windowStartId = -1;
        int8_t windowDir = 1;
        uint16_t windowCount = 0;
        int32_t carSegmentId = -1;
        int32_t nearestSegmentId = -1;
        int32_t carX = 0;
        int32_t carY = 0;
        int32_t carZ = 0;
        int32_t camX = 0;
        int32_t camY = 0;
        int32_t camZ = 0;
        int32_t segY = 0;
        int32_t deltaY = 0;
        int32_t deltaX = 0;
        int32_t deltaZ = 0;
        int32_t camDirX = 0;
        int32_t camDirZ = 0;
        int32_t fwdX = 0;
        int32_t fwdZ = 0;
        char carXSgn = '+';
        char carYSgn = '+';
        char carZSgn = '+';
        char camXSgn = '+';
        char camYSgn = '+';
        char camZSgn = '+';
        std::array<int32_t, 5> seq{{-1, -1, -1, -1, -1}};
    };

    struct OverlayDiagnosticsSnapshot
    {
        SegmentOverlaySnapshot segment{};
        Game::CarSystem::RuntimeDebugSnapshot carDebug{};
        Game::CarSystem::GameplayInputSnapshot input{};
        uint32_t submittedTrackFaces = 0u;
        uint32_t submittedCarFaces = 0u;
        uint32_t submittedFacesTotal = 0u;
        uint32_t queryCalls = 0u;
        uint32_t queryGlobalPasses = 0u;
        uint32_t queryCacheHits = 0u;
        uint32_t queryCacheMisses = 0u;
        uint32_t wallQueryCalls = 0u;
        uint32_t wallQueryHits = 0u;
    };

    struct Sh2SplitTelemetrySnapshot
    {
        uint16_t trackMasterTicks = 0;
        uint16_t trackSlaveProducerTicks = 0;
        uint16_t trackSlaveSortTicks = 0;
        uint16_t trackSlavePlanTicks = 0;
        uint16_t simSlaveTicks = 0;
        uint16_t simMasterWaitTicks = 0;
        uint32_t masterBusyTicks = 0;
        uint32_t masterWaitTicks = 0;
        uint32_t slaveWorkTicks = 0;
        uint32_t busyTotal = 0;
        uint32_t masterBusyPct = 0;
        uint32_t slaveWorkPct = 0;
        uint32_t masterWaitPctOfSim = 0;
        uint32_t queryCalls = 0;
        uint32_t queryGlobal = 0;
        uint32_t queryScmap = 0;
        bool valid = false;
    };

    struct FramePresentationSnapshot
    {
        uint32_t submittedTrackFaces = 0u;
        uint32_t submittedCarFaces = 0u;
        bool runtimeStatsEnabled = false;
        Sh2SplitTelemetrySnapshot sh2{};
    };
    struct CarRenderFrameState
    {
        Game::CarSystem* car = nullptr;
        SRL::Math::Types::Vector3D renderPosition{
            SRL::Math::Types::Fxp::BuildRaw(0),
            SRL::Math::Types::Fxp::BuildRaw(0),
            SRL::Math::Types::Fxp::BuildRaw(0)};
        Game::CarSystem::RuntimeDebugSnapshot runtimeDebug{};
        int32_t renderYawDeg = 0;
    };

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

    struct SimulationPayload
    {
        Game::IGameplayTick* gameplayTick = nullptr;
        Game::ICarPhysics* carPhysics = nullptr;
        Game::IAudioEvents* audioEvents = nullptr;
        Game::ITrackCollisionQuery* trackCollision = nullptr;
        Game::GameplayFrameState frameState{};
        SRL::Math::Types::Vector3D outWorldPosition{};
        int32_t outYawDeg = 0;
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

    void ResetPerFrameStageTraces()
    {
        hwrStageTrace_ = {};
        lwrStageTrace_ = {};
    }

    void ResetPerFrameSimulationTelemetry()
    {
        simState_.masterWaitTicksThisFrame = 0u;
        simState_.slaveLastJobTicksThisFrame = 0u;
    }

    void CaptureBeginStageTraces()
    {
        hwrStageTrace_.begin = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.begin = MaybeCaptureLowWorkRamSnapshot(true);
    }

    void CaptureGameplayStageTraces()
    {
        hwrStageTrace_.gameplay = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.gameplay = MaybeCaptureLowWorkRamSnapshot();
    }

    void CaptureAutoLapStageTraces()
    {
        hwrStageTrace_.autoLap = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.autoLap = MaybeCaptureLowWorkRamSnapshot();
    }

    void CaptureBackgroundStageTraces()
    {
        hwrStageTrace_.background = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.background = MaybeCaptureLowWorkRamSnapshot();
    }

    void CaptureHudStageTraces()
    {
        hwrStageTrace_.hud = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.hud = MaybeCaptureLowWorkRamSnapshot();
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
        if constexpr (kEnableLowWorkFreeOverlayRequireRuntimeStats)
        {
            if (!context_.enableRuntimeStatsLogs)
            {
                return;
            }
        }

        if constexpr (!kEnableLowWorkFreeOverlay)
        {
            return;
        }

        if (lowWorkFreeOverlayCooldownFrames_ > 0u)
        {
            --lowWorkFreeOverlayCooldownFrames_;
            return;
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
        uint8_t prefetchBuildAttempts = 0u;
        uint8_t prefetchBuildBudget = 0u;
        uint8_t prefetchBuildDrops = 0u;
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
            prefetchBuildAttempts = context_.trackSystem->PrefetchBuildAttemptsThisFrame();
            prefetchBuildBudget = context_.trackSystem->PrefetchBuildBudgetThisFrame();
            prefetchBuildDrops = context_.trackSystem->PrefetchBuildBudgetDropsThisFrame();
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

        const int32_t freeDelta = lowWorkOverlay_.freeValid
            ? (static_cast<int32_t>(freeBytes) - static_cast<int32_t>(lowWorkOverlay_.lastFreeBytes))
            : 0;
        lowWorkOverlay_.lastFreeBytes = freeBytes;
        lowWorkOverlay_.freeValid = true;

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

        lowWorkOverlay_.lastBreakdown = breakdown;
        lowWorkOverlay_.breakdownValid = true;

#ifdef TRACK_LWR_STAGE_TRACE
        TrackSystem::PrintLwrStageProbes();
#endif

        SRL::Debug::Print(2, 16, "LWC1 r:%u s:%u w:%u       ",
                          static_cast<unsigned>(breakdown.renderers),
                          static_cast<unsigned>(breakdown.slotState),
                          static_cast<unsigned>(breakdown.workingSet));
        SRL::Debug::Print(2, 17, "LWC2 f:%u t:%u m:%u       ",
                          static_cast<unsigned>(breakdown.familyCache),
                          static_cast<unsigned>(breakdown.transient),
                          static_cast<unsigned>(breakdown.metadata));

        if constexpr (!kEnableLowWorkFreeOverlayFull)
        {
            SRL::Debug::Print(2, 18, "LTK st:%u mw:%u dr:%u fr:%u   ",
                              static_cast<unsigned>(trackStreamTicks),
                              static_cast<unsigned>(trackMaintenanceTicks),
                              static_cast<unsigned>(trackDrawTicks),
                              static_cast<unsigned>(trackFrameTicks));
            SRL::Debug::Print(2, 19, "LTK2 w:%u pf:%u ld:%u ws:%u   ",
                              static_cast<unsigned>(trackWindowTicks),
                              static_cast<unsigned>(trackPrefetchTicks),
                              static_cast<unsigned>(trackLodTicks),
                              static_cast<unsigned>(trackWorkingSetTicks));
            SRL::Debug::Print(2, 20, "PB b:%u/%u d:%u            ",
                              static_cast<unsigned>(prefetchBuildAttempts),
                              static_cast<unsigned>(prefetchBuildBudget),
                              static_cast<unsigned>(prefetchBuildDrops));
            return;
        }

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

        lowWorkOverlay_.lastTagGroup = tagGroups;
        lowWorkOverlay_.tagGroupValid = true;

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

        lowWorkOverlay_.lastPayloadBytes = payloadBytes;
        lowWorkOverlay_.lastOverheadBytes = overheadBytes;
        lowWorkOverlay_.lastFreeBlocks = freeBlocks;
        lowWorkOverlay_.allocatorValid = true;

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
        SRL::Debug::Print(2, 26, "PB b:%u/%u d:%u            ",
                          static_cast<unsigned>(prefetchBuildAttempts),
                          static_cast<unsigned>(prefetchBuildBudget),
                          static_cast<unsigned>(prefetchBuildDrops));
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

    int32_t CurrentCameraYawDeg() const
    {
        // Camera heading must follow gameplay/physics yaw.
        // The render path already has its own model-space orientation correction.
        return NormalizeYawDeg360(carYawDeg_);
    }

    void SyncCameraHeadingFromCar()
    {
        if (!context_.cameraSystem) return;
        context_.cameraSystem->SetCarYawDegrees(CurrentCameraYawDeg());
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
        const bool zHeld = pad_.IsHeld(SRL::Input::Digital::Button::Z);
        const bool xHeld = input.xHeld;

        int32_t yawForCamera = carYawDeg_;
        const bool allowManualYawInput =
            !autoLapTestEnabled_ && (context_.carPhysics == nullptr);
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
                    SRL::Debug::Print(1, 27, "CAR FWD off:%d    ", static_cast<int>(autoLapRoute_.currentOffDeg));
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
                const int32_t gameplayYawDeg = NormalizeYawDeg360(carYawDeg_);
                const int32_t renderYawDeg =
                    NormalizeYawDeg360(gameplayYawDeg + car->VisualYawOffsetDegrees());
                SRL::Debug::Print(1, 28, "CAR yaw g:%d r:%d ",
                                  static_cast<int>(gameplayYawDeg),
                                  static_cast<int>(renderYawDeg));
            }
            else
            {
                carForwardOffsetRepeatFrames_ = 0;
                SRL::Debug::Print(1, 27, "                    ");
                SRL::Debug::Print(1, 28, "                    ");
            }
        }

        const bool camera2CalibrationActive = false;
        if (camera2CalibrationActive)
        {
            // During camera 2 calibration, arrows are reserved for camera offset tuning.
            // Do not forward left/right as steering input to gameplay/car systems.
            input.leftHeld = false;
            input.rightHeld = false;
        }
        if (input.yHeld && !yHeldPrev_)
        {
            const bool toggleTrackSlave = zHeld && context_.trackSystem;
            if (toggleTrackSlave)
            {
                const bool nextSlaveMode = !context_.trackSystem->TrackSlaveModeRequested();
                context_.trackSystem->SetTrackSlaveMode(nextSlaveMode);
                if constexpr (kEnableSh2ToggleLogs)
                {
                    SRL::Debug::Print(1, 18, "TRK SH2 mode:%s   ", nextSlaveMode ? "DUAL" : "SINGLE");
                }
            }
            else
            {
                if (autoLapInputToggleEnabled_)
                {
                    autoLapTestEnabled_ = !autoLapTestEnabled_;
                    autoLapRoute_.startupYawAligned = false;
                    if (autoLapTestEnabled_)
                    {
                        autoLapRoute_.initialized = false;
                        autoLapRoute_.built = false;
                    }
                    else
                    {
                        autoLapRoute_.currentOffDeg = 0;
                        cameraPathRuntime_.prevCarWorldPositionValid = false;
                        if (!CameraSystem::kPathGuidedChaseEnabled)
                        {
                            ReleaseAutoLapRouteStorage();
                        }
                    }
                    SRL::Debug::Print(1, 23, "CAR MOVE:%u", autoLapTestEnabled_ ? 1u : 0u);
                }
            }
        }
        yHeldPrev_ = input.yHeld;
        return input;
    }

    void ApplyResolvedFrameState(const Game::GameplayFrameState& frameState,
                                 const SRL::Math::Types::Vector3D& worldPosition,
                                 int32_t yawDeg)
    {
        context_.carWorldPosition = worldPosition;
        carYawDeg_ = yawDeg;
        SyncCameraHeadingFromCar();
        latestActiveSegmentId_ = frameState.activeSegmentId;
        if (Game::CarSystem* car = ActiveCarSystem())
        {
            car->ApplySimulationFrameState(frameState);
        }
    }

    void ApplySimulationOutput(const SimulationPayload& simOut)
    {
        ApplyResolvedFrameState(simOut.frameState, simOut.outWorldPosition, simOut.outYawDeg);
    }

    bool IsTrackProducerJobInFlightHint() const
    {
        if (!context_.trackSystem || !context_.trackSystemReady || !context_.renderTrack)
        {
            return false;
        }
        return context_.trackSystem->Telemetry().producer.jobInFlight;
    }

    void BackoffSimulationSlaveDispatch()
    {
        simState_.slaveBackoffFrames = std::max<uint8_t>(simState_.slaveBackoffFrames, kSimSlaveBackoffFrames);
    }

    // Ensure SimulationTask is fully drained before entering the track render
    // window that may submit SlaveTrackDrawProducer jobs.
    bool DrainSimulationJobIfInFlight(bool mandatoryWait)
    {
        if (!simState_.jobInFlight) return true;

        if (simulationTask_.IsDone())
        {
            simState_.jobInFlight = false;
            simState_.hasCompleted = true;
            simState_.completedIdx = simState_.inFlightIdx;
            simState_.slaveLastJobTicksThisFrame = simulationTask_.LastTicks();
            ApplySimulationOutput(simState_.output[simState_.completedIdx]);
            return true;
        }

        if (!mandatoryWait) return false;

        Sh2FrtProfiler::EnsureInitialized();
        const uint16_t waitStartTicks = Sh2FrtProfiler::Now();
        uint32_t spins = 0;
        while (!simulationTask_.IsDone() && spins < kSimDrainSoftSpinLimit)
        {
            ++spins;
        }

        if (!simulationTask_.IsDone())
        {
            ++simState_.drainSoftTimeouts;
            BackoffSimulationSlaveDispatch();
            while (!simulationTask_.IsDone() && spins < kSimDrainHardSpinLimit)
            {
                ++spins;
            }
            if (!simulationTask_.IsDone())
            {
                ++simState_.drainHardWaits;
                while (!simulationTask_.IsDone()) {}
            }
        }

        simState_.masterWaitTicksThisFrame = static_cast<uint16_t>(
            simState_.masterWaitTicksThisFrame +
            Sh2FrtProfiler::Elapsed(waitStartTicks, Sh2FrtProfiler::Now()));
        simState_.jobInFlight = false;
        simState_.hasCompleted = true;
        simState_.completedIdx = simState_.inFlightIdx;
        simState_.slaveLastJobTicksThisFrame = simulationTask_.LastTicks();
        ApplySimulationOutput(simState_.output[simState_.completedIdx]);
        return true;
    }

    void ConsumeCompletedJobs()
    {
        if (simState_.jobInFlight && simulationTask_.IsDone())
        {
            simState_.jobInFlight = false;
            simState_.hasCompleted = true;
            simState_.completedIdx = simState_.inFlightIdx;
            simState_.slaveLastJobTicksThisFrame = simulationTask_.LastTicks();
        }
        if (simState_.hasCompleted)
        {
            ApplySimulationOutput(simState_.output[simState_.completedIdx]);
        }
        if (carPrepareState_.jobInFlight && carPrepareTask_.IsDone())
        {
            carPrepareState_.jobInFlight = false;
            carPrepareState_.hasCompleted = true;
            carPrepareState_.completedIdx = carPrepareState_.inFlightIdx;
        }
    }

    Game::GameplayFrameState BuildGameplayFrameState(const FrameInputState& input)
    {
        Game::GameplayFrameState frameState{};

        Game::CarSystem* car = ActiveCarSystem();
        if (car)
        {
            Game::CarSystem::GameplayInputSnapshot gameplayInput{};
            if (!autoLapTestEnabled_)
            {
                gameplayInput.accelerateHeld = input.bHeld;
                gameplayInput.brakeHeld = input.cHeld;
                gameplayInput.steerLeftHeld = input.leftHeld;
                gameplayInput.steerRightHeld = input.rightHeld;
                gameplayInput.shiftDownHeld = input.lHeld;
                gameplayInput.shiftUpHeld = input.rHeld;
                gameplayInput.shiftLockHeld = input.xHeld;
            }
            car->PrepareGameplayFrameState(autoLapTestEnabled_ ? nullptr : &gameplayInput,
                                           frameCounter_,
                                           context_.carWorldPosition,
                                           carYawDeg_,
                                           autoLapTestEnabled_,
                                           frameState);
        }
        else
        {
            frameState.frameId = frameCounter_;
            frameState.carWorldPosition = context_.carWorldPosition;
            frameState.carYawDeg = carYawDeg_;
        }
        return frameState;
    }

    void RunGameplayFrameSynchronously(Game::GameplayFrameState& frameState)
    {
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
        ApplyResolvedFrameState(frameState, frameState.carWorldPosition, frameState.carYawDeg);
    }

    bool TryDispatchSimulationOnSlave(const Game::GameplayFrameState& frameState)
    {
        if (simState_.slaveBackoffFrames > 0)
        {
            --simState_.slaveBackoffFrames;
            ++simState_.slaveDispatchSkipsBackoff;
            return false;
        }
        if (simState_.jobInFlight || carPrepareState_.jobInFlight)
        {
            return false;
        }
        if (IsTrackProducerJobInFlightHint())
        {
            ++simState_.slaveDispatchSkipsTrackBusy;
            BackoffSimulationSlaveDispatch();
            return false;
        }

        SimulationPayload simPayload{};
        simPayload.gameplayTick = context_.gameplayTick;
        simPayload.carPhysics = context_.carPhysics;
        simPayload.audioEvents = context_.audioEvents;
        simPayload.trackCollision = context_.trackCollision;
        simPayload.frameState = frameState;
        simPayload.outWorldPosition = frameState.carWorldPosition;
        simPayload.outYawDeg = frameState.carYawDeg;

        const uint8_t slot = simState_.writeIdx;
        simState_.input[slot] = simPayload;
        simulationTask_.Configure(&simState_.input[slot], &simState_.output[slot]);
        SRL::Slave::ExecuteOnSlave(simulationTask_);
        simState_.jobInFlight = true;
        simState_.inFlightIdx = slot;
        simState_.writeIdx ^= 1u;
        ++simState_.slaveDispatchCount;
        return true;
    }

    void ExecuteGameplayFrame(Game::GameplayFrameState& frameState)
    {
        const bool useSlaveSim =
            context_.enableSlaveForSimulation &&
            (context_.gameplayTick || context_.carPhysics || context_.audioEvents);

        if (useSlaveSim)
        {
            if (context_.slaveSimulationLockstep)
            {
                // True lockstep: camera/render must consume the same frame state
                // produced by simulation to avoid chase drift.
                if (simState_.jobInFlight)
                {
                    (void)DrainSimulationJobIfInFlight(true);
                    if (simState_.hasCompleted)
                    {
                        simState_.hasCompleted = false;
                        return;
                    }
                }

                if (TryDispatchSimulationOnSlave(frameState))
                {
                    (void)DrainSimulationJobIfInFlight(true);
                    if (simState_.hasCompleted)
                    {
                        simState_.hasCompleted = false;
                        return;
                    }
                }

                // Fallback safety: if Slave dispatch/drain fails, keep frame coherent.
                RunGameplayFrameSynchronously(frameState);
                return;
            }

            // Non-lockstep mode: never block Master waiting for Slave sim.
            // Consume completion opportunistically and keep rendering with last
            // committed state while a new sim job is in-flight.
            (void)DrainSimulationJobIfInFlight(false);
            if (simState_.jobInFlight)
            {
                return;
            }

            if (TryDispatchSimulationOnSlave(frameState))
            {
                return;
            }
        }

        RunGameplayFrameSynchronously(frameState);
    }

    void ScheduleCarPrepareIfEnabled()
    {
        if (!CanRenderCar() || !context_.enableSlaveForCarPrepare) return;
        if (carPrepareState_.jobInFlight) return;
        if (IsTrackProducerJobInFlightHint()) return;

        const uint8_t slot = carPrepareState_.writeIdx;
        carPrepareState_.inputYaw[slot] = carYawDeg_;
        carPrepareTask_.Configure(&carPrepareState_.inputYaw[slot], &carPrepareState_.outputYaw[slot]);
        SRL::Slave::ExecuteOnSlave(carPrepareTask_);
        carPrepareState_.jobInFlight = true;
        carPrepareState_.inFlightIdx = slot;
        carPrepareState_.writeIdx ^= 1u;
    }

    void UpdateBackground()
    {
        if (!context_.enableBg || !context_.bgManager) return;
        AppState::Set(AppState::Stage::LoopBackground, frameCounter_);
        context_.bgManager->Update(context_.cameraSystem->State(), carYawDeg_);
    }

    SRL::Math::Types::Vector3D ResolveCameraWallOcclusion(
        const SRL::Math::Types::Vector3D& desiredCameraLocation,
        const SRL::Math::Types::Vector3D& lookTarget) const
    {
        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector3D;

        (void)lookTarget;
        if (!context_.trackSystem || !context_.trackSystemReady)
        {
            return desiredCameraLocation;
        }
        if (context_.cameraSystem &&
            context_.cameraSystem->GetChasePreset() == CameraSystem::ChasePreset::FirstPerson)
        {
            return desiredCameraLocation;
        }

        const Vector3D carPos = context_.carWorldPosition;
        const Vector3D ray = desiredCameraLocation - carPos;
        const int32_t rayDxAbs = std::abs(ray.X.RawValue());
        const int32_t rayDzAbs = std::abs(ray.Z.RawValue());
        if (std::max(rayDxAbs, rayDzAbs) <= (1 << 12))
        {
            return desiredCameraLocation;
        }

        constexpr int32_t kProbeSamples = 5;
        constexpr int32_t kFirstProbeStep = 2;
        constexpr int32_t kPushEpsilonRaw = (1 << 10);
        constexpr int32_t kStepSafetyMarginRaw = (1 << 13); // 0.125
        constexpr Fxp kProbeRadius = Fxp::BuildRaw(0x0000E666); // 0.90

        const int32_t seedSegmentId = (latestActiveSegmentId_ > 0)
            ? static_cast<int32_t>(latestActiveSegmentId_)
            : -1;
        const Vector3D rayDir = Vector3D(ray.X, Fxp::BuildRaw(0), ray.Z);

        int32_t hitStep = -1;
        for (int32_t step = kFirstProbeStep; step <= kProbeSamples; ++step)
        {
            const int32_t tRaw = (step << 16) / kProbeSamples;
            const Fxp t = Fxp::BuildRaw(tRaw);
            const Vector3D samplePos = carPos + Vector3D(ray.X * t, ray.Y * t, ray.Z * t);
            Vector3D push{};
            if (!context_.trackSystem->FindPlanarWallPush(samplePos,
                                                          context_.trackSegOffset,
                                                          rayDir,
                                                          kProbeRadius,
                                                          push,
                                                          nullptr,
                                                          seedSegmentId,
                                                          false))
            {
                continue;
            }

            const int32_t pushMagRaw =
                std::abs(push.X.RawValue()) + std::abs(push.Z.RawValue());
            if (pushMagRaw > kPushEpsilonRaw)
            {
                hitStep = step;
                break;
            }
        }

        if (hitStep < 0)
        {
            return desiredCameraLocation;
        }

        int32_t safeTRaw = (((hitStep - 1) << 16) / kProbeSamples) - kStepSafetyMarginRaw;
        safeTRaw = std::clamp<int32_t>(safeTRaw, 0, (1 << 16));
        const Fxp safeT = Fxp::BuildRaw(safeTRaw);
        return carPos + Vector3D(ray.X * safeT, ray.Y * safeT, ray.Z * safeT);
    }

    CameraFrameState ResolveCameraFrameState()
    {
        UpdateCameraPathFrameContext();
        SRL::Math::Types::Vector3D rawCameraLocation =
            context_.cameraSystem->CameraLocation(context_.carWorldPosition);
        SRL::Math::Types::Vector3D rawLookTarget =
            context_.cameraSystem->LookTarget(context_.carWorldPosition, context_.modelOffset);

        // Follow track slope by lifting camera on uphill and relaxing on level ground.
        // Ground probes are already in world Y units used by telemetry.
        {
            const Game::CarSystem::RuntimeDebugSnapshot carDebug = CarRuntimeDebugSnapshot();
            const int32_t slopeDelta = static_cast<int32_t>(carDebug.groundFrontY) -
                                       static_cast<int32_t>(carDebug.groundRearY);
            const int32_t uphillDelta = std::max<int32_t>(0, slopeDelta);
            enum class SlopeCamProfile : int32_t { Suave = 0, Medio = 1, Forte = 2 };
            constexpr SlopeCamProfile kSlopeCamProfile = SlopeCamProfile::Medio;
            int32_t kSlopeDeadZone = 4;
            int32_t kSlopeGainNum = 1;
            int32_t kSlopeGainDen = 2;
            int32_t kSlopeLiftMax = 36;
            int32_t kSlopeBlendRaw = (1 << 15); // 0.5

            switch (kSlopeCamProfile)
            {
            case SlopeCamProfile::Suave:
                kSlopeDeadZone = 6;
                kSlopeGainNum = 1;
                kSlopeGainDen = 3;
                kSlopeLiftMax = 24;
                kSlopeBlendRaw = (1 << 14); // 0.25
                break;
            case SlopeCamProfile::Forte:
                kSlopeDeadZone = 2;
                kSlopeGainNum = 1;
                kSlopeGainDen = 1;
                kSlopeLiftMax = 52;
                kSlopeBlendRaw = (3 << 14); // 0.75
                break;
            case SlopeCamProfile::Medio:
            default:
                break;
            }

            int32_t targetLiftUnits = 0;
            if (uphillDelta > kSlopeDeadZone)
            {
                const int32_t effective = uphillDelta - kSlopeDeadZone;
                targetLiftUnits = (effective * kSlopeGainNum) / kSlopeGainDen;
                targetLiftUnits = std::clamp<int32_t>(targetLiftUnits, 0, kSlopeLiftMax);
            }

            // Smooth response so camera does not jitter on uneven faces.
            cameraSlopeLiftRaw_ = cameraSlopeLiftRaw_ +
                                  static_cast<int32_t>(
                                      (static_cast<int64_t>(targetLiftUnits - cameraSlopeLiftRaw_) * kSlopeBlendRaw) >> 16);

            const SRL::Math::Types::Fxp lift =
                SRL::Math::Types::Fxp::BuildRaw(cameraSlopeLiftRaw_ << 16);
            // In this project, negative Y is up.
            rawCameraLocation.Y -= lift;
            rawLookTarget.Y -= lift;

            if (context_.verboseFrameLogs)
            {
                SRL::Debug::Print(1, 31, "CAM slp p:%d dy:%d lf:%d",
                                  static_cast<int>(kSlopeCamProfile),
                                  static_cast<int>(uphillDelta),
                                  static_cast<int>(cameraSlopeLiftRaw_));
            }
        }

        const SRL::Math::Types::Vector3D resolvedCameraLocation = rawCameraLocation;
        const bool cameraReady =
            IsFiniteCameraPoint(resolvedCameraLocation) &&
            IsFiniteCameraPoint(rawLookTarget);
        if (cameraReady)
        {
            lastValidCameraLocation_ = resolvedCameraLocation;
            lastValidLookTarget_ = rawLookTarget;
        }

        CameraFrameState frame{};
        frame.ready = cameraReady;
        frame.location = cameraReady ? resolvedCameraLocation : lastValidCameraLocation_;
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

    int32_t ResolveCarRenderYawDegrees() const
    {
        if (Game::CarSystem* car = ActiveCarSystem())
        {
            return car->RenderYawDegrees();
        }
        return carYawDeg_;
    }

    Game::CarSystem::RuntimeDebugSnapshot CarRuntimeDebugSnapshot() const
    {
        if (Game::CarSystem* car = ActiveCarSystem())
        {
            return car->RuntimeDebug();
        }
        return {};
    }

    CarRenderFrameState BuildCarRenderFrameState(const CameraFrameState& camera)
    {
        CarRenderFrameState state{};
        state.car = ActiveCarSystem();
        if (!state.car)
        {
            return state;
        }

        state.renderPosition = ResolveCarRenderPosition(camera);
        state.runtimeDebug = state.car->RuntimeDebug();
        state.renderYawDeg = state.car->RenderYawDegrees();
        return state;
    }

    void StoreShadowDebugState(const SRL::Math::Types::Vector3D& shadowWorldPosition,
                               int32_t shadowYawDeg)
    {
        shadowDebug_.worldPos = shadowWorldPosition;
        shadowDebug_.yawDeg = shadowYawDeg;
    }

    void DrawCarShadowBlob(const CarRenderFrameState& carFrame)
    {
        using SRL::Math::Types::Angle;
        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector2D;
        using SRL::Math::Types::Vector3D;

        constexpr Fxp kShadowHalfLength = Fxp::BuildRaw(0x002C0000); // 44.0 (+~30%)
        constexpr Fxp kShadowHalfWidth = Fxp::BuildRaw(0x00150000);  // 21.0 (+~30%)
        constexpr Fxp kShadowGroundBias = Fxp::BuildRaw(10 << 16);   // +10.0 over sampled ground Y
        // Scene2D sort bias: positive pushes farther back in the VDP1 order used here.
        constexpr Fxp kShadowSortBias = Fxp::BuildRaw(0x00100000);   // force shadow behind car
        constexpr SRL::Types::HighColor kShadowColor = SRL::Types::HighColor::FromRGB555(0, 0, 0);

        Vector3D center = carFrame.renderPosition;
        // Anchor shadow to sampled ground to keep it detached from car body.
        if (carFrame.runtimeDebug.groundMask != 0u)
        {
            center.Y = Fxp::BuildRaw(static_cast<int32_t>(carFrame.runtimeDebug.groundTargetY) << 16);
        }
        center.Y += kShadowGroundBias;

        const int32_t shadowYawDeg = carFrame.renderYawDeg;
        StoreShadowDebugState(center, shadowYawDeg);
        const Angle yaw = Angle::FromDegrees(Fxp::BuildRaw(static_cast<int32_t>(shadowYawDeg) << 16));
        const Fxp sinYaw = SRL::Math::Trigonometry::Sin(yaw);
        const Fxp cosYaw = SRL::Math::Trigonometry::Cos(yaw);

        const Fxp forwardX = sinYaw;
        const Fxp forwardZ = Fxp::BuildRaw(-cosYaw.RawValue());
        const Fxp rightX = cosYaw;
        const Fxp rightZ = sinYaw;

        auto makePoint = [&](int32_t longSign, int32_t latSign) -> Vector3D
        {
            const Fxp longOffset = (longSign >= 0) ? kShadowHalfLength : Fxp::BuildRaw(-kShadowHalfLength.RawValue());
            const Fxp latOffset = (latSign >= 0) ? kShadowHalfWidth : Fxp::BuildRaw(-kShadowHalfWidth.RawValue());
            Vector3D p = center;
            p.X += (forwardX * longOffset) + (rightX * latOffset);
            p.Z += (forwardZ * longOffset) + (rightZ * latOffset);
            return p;
        };

        Vector3D worldPts[4] = {
            makePoint(+1, -1),
            makePoint(+1, +1),
            makePoint(-1, +1),
            makePoint(-1, -1)
        };

        Vector2D center2D{};
        const Fxp centerDepth = SRL::Scene3D::ProjectToScreen(center, &center2D);
        if (centerDepth.RawValue() <= 0)
        {
            return;
        }

        Vector2D screenPts[4]{};
        Fxp depthSum = Fxp::BuildRaw(0);
        bool projectedAll = true;
        for (size_t i = 0; i < 4u; ++i)
        {
            const Fxp depth = SRL::Scene3D::ProjectToScreen(worldPts[i], &screenPts[i]);
            if (depth.RawValue() <= 0)
            {
                projectedAll = false;
                break;
            }
            depthSum += depth;
        }

        Fxp sort = centerDepth + kShadowSortBias;
        if (projectedAll)
        {
            sort = (depthSum / 4) + kShadowSortBias;
        }
        else
        {
            // Fallback: axis-aligned blob centered on projected car point.
            const int32_t depthInt = std::max<int32_t>(1, centerDepth.RawValue() >> 16);
            const int16_t halfW = static_cast<int16_t>(std::clamp<int32_t>(26 - (depthInt / 20), 10, 26));
            const int16_t halfH = static_cast<int16_t>(std::clamp<int32_t>(12 - (depthInt / 40), 4, 12));
            screenPts[0] = Vector2D(center2D.X - Fxp::BuildRaw(halfW << 16), center2D.Y - Fxp::BuildRaw(halfH << 16));
            screenPts[1] = Vector2D(center2D.X + Fxp::BuildRaw(halfW << 16), center2D.Y - Fxp::BuildRaw(halfH << 16));
            screenPts[2] = Vector2D(center2D.X + Fxp::BuildRaw(halfW << 16), center2D.Y + Fxp::BuildRaw(halfH << 16));
            screenPts[3] = Vector2D(center2D.X - Fxp::BuildRaw(halfW << 16), center2D.Y + Fxp::BuildRaw(halfH << 16));
        }

        // Keep shadow fully opaque for debugging background interaction.
        const int32_t prevHalfTrans =
            SRL::Scene2D::GetEffect(SRL::Scene2D::SpriteEffect::HalfTransparency);
        const int32_t prevScreenDoors =
            SRL::Scene2D::GetEffect(SRL::Scene2D::SpriteEffect::ScreenDoors);
        SRL::Scene2D::SetEffect(SRL::Scene2D::SpriteEffect::HalfTransparency, 0);
        SRL::Scene2D::SetEffect(SRL::Scene2D::SpriteEffect::ScreenDoors, 0);
        bool drawn = SRL::Scene2D::DrawPolygon(screenPts, true, kShadowColor, sort);
        if (!drawn)
        {
            (void)SRL::Scene2D::DrawPolygon(screenPts, true, kShadowColor, Fxp::BuildRaw(0));
        }
        SRL::Scene2D::SetEffect(SRL::Scene2D::SpriteEffect::ScreenDoors, prevScreenDoors ? 1 : 0);
        SRL::Scene2D::SetEffect(SRL::Scene2D::SpriteEffect::HalfTransparency, prevHalfTrans ? 1 : 0);
    }

    void DrawCarShadowModel(const CarRenderFrameState& carFrame)
    {
        using SRL::Math::Types::Angle;
        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector3D;

        if (!context_.carShadowRenderer) return;

        Vector3D shadowPos = carFrame.renderPosition;
        if (carFrame.runtimeDebug.groundMask != 0u)
        {
            shadowPos.Y = Fxp::BuildRaw(static_cast<int32_t>(carFrame.runtimeDebug.groundTargetY) << 16);
        }
        // Keep SBA close to asphalt. Large positive offsets can bury the model.
        // In this project, positive Y is down.
        shadowPos.Y += Fxp::BuildRaw(1 << 16); // 1 world unit down from sampled asphalt

        const int32_t shadowYawDeg = carFrame.renderYawDeg;
        StoreShadowDebugState(shadowPos, shadowYawDeg);
        const Angle yaw =
            Angle::FromDegrees(Fxp::BuildRaw(static_cast<int32_t>(shadowYawDeg) << 16));
        context_.carShadowRenderer->Render(shadowPos, yaw, false);
    }

    void RenderCar(const CameraFrameState& camera)
    {
        if (!CanRenderCar()) return;

        AppState::Set(AppState::Stage::LoopCar, frameCounter_);
        CarRenderFrameState carFrame = BuildCarRenderFrameState(camera);
        if (!carFrame.car) return;

        RenderCarShadowIfEnabled(carFrame);
        carFrame.car->SyncRenderState(carFrame.renderPosition, carYawDeg_);
        SubmitCarRender(*carFrame.car);
    }

    SRL::Math::Types::Vector3D ResolveCarRenderPosition(const CameraFrameState& camera)
    {
        SRL::Math::Types::Vector3D carRenderPos = context_.carWorldPosition;
        if (!IsFiniteCarPos(lastValidCarRenderPos_))
        {
            lastValidCarRenderPos_ = context_.carWorldPosition;
        }
        if (!IsFiniteCarPos(carRenderPos))
        {
            carRenderPos = lastValidCarRenderPos_;
        }
        else
        {
            lastValidCarRenderPos_ = carRenderPos;
        }

        ApplyCarCameraDepthBias(camera, carRenderPos);
        ApplyCarVisualLift(carRenderPos);
        return carRenderPos;
    }

    void ApplyCarCameraDepthBias(const CameraFrameState& camera,
                                 SRL::Math::Types::Vector3D& ioCarRenderPos) const
    {
        // Small camera depth bias to reduce seam overdraw on car body.
        // Disable at very low speed to avoid visual side-slip impression at launch.
        if (CarRuntimeDebugSnapshot().speedProxy <= 20) return;

        const int32_t dxRaw = camera.location.X.RawValue() - ioCarRenderPos.X.RawValue();
        const int32_t dzRaw = camera.location.Z.RawValue() - ioCarRenderPos.Z.RawValue();
        const int32_t adx = (dxRaw < 0) ? -dxRaw : dxRaw;
        const int32_t adz = (dzRaw < 0) ? -dzRaw : dzRaw;
        const int32_t maxAxis = (adx > adz) ? adx : adz;
        if (maxAxis <= 0) return;

        constexpr int32_t kCarDepthBiasUnits = 3;
        const int32_t biasRaw = (kCarDepthBiasUnits << 16);
        const int32_t offXRaw = static_cast<int32_t>((static_cast<int64_t>(dxRaw) * biasRaw) / maxAxis);
        const int32_t offZRaw = static_cast<int32_t>((static_cast<int64_t>(dzRaw) * biasRaw) / maxAxis);
        ioCarRenderPos.X += SRL::Math::Types::Fxp::BuildRaw(offXRaw);
        ioCarRenderPos.Z += SRL::Math::Types::Fxp::BuildRaw(offZRaw);
    }

    void ApplyCarVisualLift(SRL::Math::Types::Vector3D& ioCarRenderPos) const
    {
        // Visual lift for seam overlap testing.
        // This does not change gameplay physics state.
        constexpr int32_t kCarVisualLiftUnits = 0;
        ioCarRenderPos.Y -= SRL::Math::Types::Fxp::BuildRaw(kCarVisualLiftUnits << 16);
    }

    void RenderCarShadowIfEnabled(const CarRenderFrameState& carFrame)
    {
        constexpr bool kEnableCarShadowRendering = true;
        constexpr bool kUseBlobShadow = false;
        if constexpr (!kEnableCarShadowRendering) return;

        if constexpr (kUseBlobShadow)
        {
            DrawCarShadowBlob(carFrame);
        }
        if (context_.renderCarShadowModel && context_.carShadowRenderer)
        {
            DrawCarShadowModel(carFrame);
        }
    }

    void SubmitCarRender(Game::CarSystem& car)
    {
        context_.renderPipeline->Reset();
        car.SubmitRender(*context_.renderPipeline);
        context_.renderPipeline->Flush();
        if (MeshRenderer* renderer = car.Renderer())
        {
            lastRenderedCarFacesThisFrame_ = renderer->LastRenderFaceCount();
            lastRenderedCarMeshesThisFrame_ = renderer->LastRenderMeshCount();
        }
    }

    void ConfigureScene3dView(const CameraFrameState& camera) const
    {
        using SRL::Math::Types::Angle;
        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(camera.location, camera.lookTarget, Angle::FromDegrees(0.0));
    }

    void CaptureTrackDrawStageTraces()
    {
        hwrStageTrace_.trackDraw = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.trackDraw = MaybeCaptureLowWorkRamSnapshot();
    }

    void CaptureTrackEndStageTraces()
    {
        hwrStageTrace_.trackEnd = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.trackEnd = MaybeCaptureLowWorkRamSnapshot();
        hwrStageTrace_.track = hwrStageTrace_.trackEnd;
        lwrStageTrace_.track = lwrStageTrace_.trackEnd;
    }

    void CaptureIdleTrackAndCarTraces()
    {
        hwrStageTrace_.trackDraw = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.trackDraw = MaybeCaptureLowWorkRamSnapshot();
        hwrStageTrace_.trackEnd = hwrStageTrace_.trackDraw;
        lwrStageTrace_.trackEnd = lwrStageTrace_.trackDraw;
        hwrStageTrace_.track = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.track = MaybeCaptureLowWorkRamSnapshot();
        hwrStageTrace_.car = hwrStageTrace_.track;
        lwrStageTrace_.car = lwrStageTrace_.track;
    }

    void CaptureCarStageTraces()
    {
        hwrStageTrace_.car = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.car = MaybeCaptureLowWorkRamSnapshot();
    }

    void RenderFrame(const CameraFrameState& camera)
    {
        lastRenderedCarFacesThisFrame_ = 0u;
        lastRenderedCarMeshesThisFrame_ = 0u;
        if (!camera.ready)
        {
            if constexpr (kEnableCameraRuntimeLogs)
            {
                SRL::Debug::Print(1, 23, "CAM wait snapshot");
            }
            CaptureIdleTrackAndCarTraces();
            return;
        }

        ConfigureScene3dView(camera);
        RenderTrackFrame(camera);

        ConfigureScene3dView(camera);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Car);
        RenderCar(camera);
        CaptureCarStageTraces();
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Unknown);
    }

    void CaptureIdleRenderTraces()
    {
        CaptureIdleTrackAndCarTraces();
    }

    bool IsTrackFrameEnabled() const
    {
        return context_.trackSystem &&
               context_.trackSystemReady &&
               context_.renderTrack;
    }

    void RenderTrackFrame(const CameraFrameState& camera)
    {
        if (!IsTrackFrameEnabled())
        {
            CaptureTrackRenderDisabledTraces();
            return;
        }

        context_.trackSystem->SetObservedCarSegmentId(latestActiveSegmentId_);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::TrackCore);
        context_.trackSystem->BeginFrame(frameCounter_);
        AppState::Set(AppState::Stage::LoopTrack, frameCounter_);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::TrackPrepare);
        context_.trackSystem->RenderFrame(true,
                                          context_.trackSegOffset,
                                          context_.lightDirection,
                                          camera.location,
                                          camera.lookTarget,
                                          context_.carWorldPosition);
        CaptureTrackDrawStageTraces();
        SetWorkRamDebugTag(SRL::Memory::DebugTag::TrackCore);
        context_.trackSystem->EndFrame();
        CaptureTrackEndStageTraces();
    }

    void CaptureTrackRenderDisabledTraces()
    {
        hwrStageTrace_.trackDraw = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.trackDraw = MaybeCaptureLowWorkRamSnapshot();
        hwrStageTrace_.trackEnd = hwrStageTrace_.trackDraw;
        lwrStageTrace_.trackEnd = lwrStageTrace_.trackDraw;
        hwrStageTrace_.track = hwrStageTrace_.trackEnd;
        lwrStageTrace_.track = lwrStageTrace_.trackEnd;
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
        // Async sim mode: do not block frame completion on Slave sim.
        (void)DrainSimulationJobIfInFlight(false);

        ++frameCounter_;
        const FramePresentationSnapshot framePresentation = BuildFramePresentationSnapshot();

        PresentFrameHudAndTelemetry(framePresentation);
        SynchronizeFrameCore();
        UpdateFrameEndOverlays();
    }

    uint32_t GetSubmittedTrackFacesThisFrame() const
    {
        if (context_.trackSystemReady && context_.renderTrack && context_.trackSystem)
        {
            return context_.trackSystem->Telemetry().submittedTrackFaces;
        }
        return 0u;
    }

    uint32_t GetSubmittedCarFacesThisFrame() const
    {
        return CanRenderCar() ? lastRenderedCarFacesThisFrame_ : 0u;
    }

    FramePresentationSnapshot BuildFramePresentationSnapshot() const
    {
        FramePresentationSnapshot snapshot{};
        snapshot.submittedTrackFaces = GetSubmittedTrackFacesThisFrame();
        snapshot.submittedCarFaces = GetSubmittedCarFacesThisFrame();
        snapshot.runtimeStatsEnabled = context_.enableRuntimeStatsLogs;
        snapshot.sh2 = BuildSh2SplitTelemetrySnapshot();
        return snapshot;
    }

    void PresentFrameHudAndTelemetry(const FramePresentationSnapshot& framePresentation)
    {
        hwrStageTrace_.preFinish = MaybeCaptureHighWorkRamSnapshot();
        lwrStageTrace_.preFinish = MaybeCaptureLowWorkRamSnapshot();
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Finish);
        context_.hudSystem->PresentPeriodicFrameStats(frameCounter_,
                                                      context_.enableRuntimeStatsLogs,
                                                      context_.logTrack,
                                                      context_.logCar,
                                                      context_.faceCount,
                                                      context_.vertexCount,
                                                      framePresentation.submittedTrackFaces,
                                                      framePresentation.submittedCarFaces);
        if (!framePresentation.runtimeStatsEnabled) return;

        PrintSegmentOverlapDiagnostics(framePresentation.submittedTrackFaces,
                                       framePresentation.submittedCarFaces);
        PrintSh2SplitTelemetry(framePresentation.sh2);
    }

    void SynchronizeFrameCore()
    {
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
    }

    void UpdateFrameEndOverlays()
    {
        if (context_.enableRuntimeStatsLogs || context_.enableMinimalFpsOverlay)
        {
            UpdateRealtimeFpsOverlay();
        }

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

    void PrintSegmentOverlapDiagnostics(uint32_t submittedTrackFaces,
                                        uint32_t submittedCarFaces)
    {
        OverlayDiagnosticsSnapshot overlay{};
        if (!BuildOverlayDiagnosticsSnapshot(submittedTrackFaces, submittedCarFaces, overlay)) return;

        PrintSegmentWindowOverlay(overlay.segment);
        PrintSegmentSpatialOverlay(overlay);
        PrintShadowSpatialOverlay();
        PrintFaceAndShadowOverlay(overlay);
        PrintGroundProbeOverlay(overlay);
        PrintPhysicsQueryOverlay(overlay);
        constexpr bool kEnableExtendedDrivetrainOverlay = false;
        if constexpr (kEnableExtendedDrivetrainOverlay)
        {
            const Game::CarSystem::DrivetrainDebugSnapshot drivetrain =
                (context_.carSystem && context_.carSystem->get())
                    ? context_.carSystem->get()->BuildDrivetrainDebugSnapshot()
                    : Game::CarSystem::DrivetrainDebugSnapshot{};
            SRL::Debug::Print(0, 13, "GEAR:%c RPM:%d KM:%d    ",
                              drivetrain.gearChar,
                              static_cast<int>(drivetrain.engineRpm),
                              static_cast<int>(drivetrain.speedKmh));
            SRL::Debug::Print(1, 17, "OVR gear:%c rpm:%d km:%d   ",
                              drivetrain.gearChar,
                              static_cast<int>(drivetrain.engineRpm),
                              static_cast<int>(drivetrain.speedKmh));
            SRL::Debug::Print(1, 30, "OVR drv th:%d br:%u sp:%d km:%d g:%c rp:%d",
                              static_cast<int>(drivetrain.throttle),
                              static_cast<unsigned>(drivetrain.braking ? 1u : 0u),
                              static_cast<int>(drivetrain.speedProxy),
                              static_cast<int>(drivetrain.speedKmh),
                              drivetrain.gearChar,
                              static_cast<int>(drivetrain.engineRpm));
            SRL::Debug::Print(0, 14, "DRV th:%d br:%u sp:%d km:%d g:%c rp:%d",
                              static_cast<int>(drivetrain.throttle),
                              static_cast<unsigned>(drivetrain.braking ? 1u : 0u),
                              static_cast<int>(drivetrain.speedProxy),
                              static_cast<int>(drivetrain.speedKmh),
                              drivetrain.gearChar,
                              static_cast<int>(drivetrain.engineRpm));
            SRL::Debug::Print(1, 31, "OVR dyn st:%d yr:%d ys:%d pdx:%d ndz:%d",
                              static_cast<int>(drivetrain.steeringCommand),
                              static_cast<int>(drivetrain.yawRateDeg),
                              static_cast<int>(drivetrain.yawStepDeg),
                              static_cast<int>(drivetrain.planarDx),
                              static_cast<int>(drivetrain.netDz));
            SRL::Debug::Print(0, 31, "DYN st:%d yr:%d ys:%d pdx:%d ndz:%d",
                              static_cast<int>(drivetrain.steeringCommand),
                              static_cast<int>(drivetrain.yawRateDeg),
                              static_cast<int>(drivetrain.yawStepDeg),
                              static_cast<int>(drivetrain.planarDx),
                              static_cast<int>(drivetrain.netDz));
        }
        PrintInputOverlay(overlay);
        PrintSegmentEventOverlay(overlay.segment);
    }

    void PrintSegmentWindowOverlay(const SegmentOverlaySnapshot& overlay) const
    {
        SRL::Debug::Print(1, 18, "OVR car:%d near:%d ws:%d d:%d n:%u",
                          static_cast<int>(overlay.carSegmentId),
                          static_cast<int>(overlay.nearestSegmentId),
                          static_cast<int>(overlay.windowStartId),
                          static_cast<int>(overlay.windowDir),
                          static_cast<unsigned>(overlay.windowCount));
        SRL::Debug::Print(1, 19, "OVR seq:%d,%d,%d,%d,%d",
                          static_cast<int>(overlay.seq[0]),
                          static_cast<int>(overlay.seq[1]),
                          static_cast<int>(overlay.seq[2]),
                          static_cast<int>(overlay.seq[3]),
                          static_cast<int>(overlay.seq[4]));
        SRL::Debug::Print(1, 20, "OVR car X:%c%d Y:%c%d Z:%c%d",
                          overlay.carXSgn,
                          static_cast<int>(std::abs(overlay.carX)),
                          overlay.carYSgn,
                          static_cast<int>(std::abs(overlay.carY)),
                          overlay.carZSgn,
                          static_cast<int>(std::abs(overlay.carZ)));
        SRL::Debug::Print(1, 22, "OVR cam X:%c%d Y:%c%d Z:%c%d",
                          overlay.camXSgn,
                          static_cast<int>(std::abs(overlay.camX)),
                          overlay.camYSgn,
                          static_cast<int>(std::abs(overlay.camY)),
                          overlay.camZSgn,
                          static_cast<int>(std::abs(overlay.camZ)));
    }

    void PrintSegmentSpatialOverlay(const OverlayDiagnosticsSnapshot& overlay) const
    {
        SRL::Debug::Print(1, 26, "OVR dir y:%d fx:%d fz:%d cx:%d cz:%d",
                          static_cast<int>(carYawDeg_),
                          static_cast<int>(overlay.segment.fwdX),
                          static_cast<int>(overlay.segment.fwdZ),
                          static_cast<int>(overlay.segment.camDirX),
                          static_cast<int>(overlay.segment.camDirZ));
        SRL::Debug::Print(1, 27, "OVR y seg:%d dy:%d gr:%d gf:%d gt:%d sf:%u fm:%u fc:%d",
                          static_cast<int>(overlay.segment.segY),
                          static_cast<int>(overlay.segment.deltaY),
                          static_cast<int>(overlay.carDebug.groundRearY),
                          static_cast<int>(overlay.carDebug.groundFrontY),
                          static_cast<int>(overlay.carDebug.groundTargetY),
                          static_cast<unsigned>(overlay.carDebug.groundSurfaceType),
                          static_cast<unsigned>(overlay.carDebug.groundFamilyId),
                          static_cast<int>(overlay.carDebug.groundFaceIndex));
    }

    void PrintShadowSpatialOverlay() const
    {
        const int32_t shX = FxpToIntDebug(shadowDebug_.worldPos.X);
        const int32_t shY = FxpToIntDebug(shadowDebug_.worldPos.Y);
        const int32_t shZ = FxpToIntDebug(shadowDebug_.worldPos.Z);
        const char shXSgn = (shX < 0) ? '-' : '+';
        const char shYSgn = (shY < 0) ? '-' : '+';
        const char shZSgn = (shZ < 0) ? '-' : '+';
        SRL::Debug::Print(1, 25, "OVR shd X:%c%d Y:%c%d Z:%c%d y:%d",
                          shXSgn,
                          static_cast<int>(std::abs(shX)),
                          shYSgn,
                          static_cast<int>(std::abs(shY)),
                          shZSgn,
                          static_cast<int>(std::abs(shZ)),
                          static_cast<int>(shadowDebug_.yawDeg));
    }

    void PrintFaceAndShadowOverlay(const OverlayDiagnosticsSnapshot& overlay) const
    {
        SRL::Debug::Print(1, 21, "OVR face tr:%u ca:%u tt:%u",
                          static_cast<unsigned>(overlay.submittedTrackFaces),
                          static_cast<unsigned>(overlay.submittedCarFaces),
                          static_cast<unsigned>(overlay.submittedFacesTotal));
        SRL::Debug::Print(1, 24, "OVR sba ld:%u rd:%u m:%u f:%u",
                          static_cast<unsigned>(context_.sbaLoaded ? 1u : 0u),
                          static_cast<unsigned>((context_.renderCarShadowModel && context_.carShadowRenderer) ? 1u : 0u),
                          static_cast<unsigned>(context_.sbaMeshCount),
                          static_cast<unsigned>(context_.sbaFaceCount));
    }

    void PrintGroundProbeOverlay(const OverlayDiagnosticsSnapshot& overlay) const
    {
        SRL::Debug::Print(1, 29, "OVR gp m:%u dx:%d dz:%d wh:%u wx:%d wz:%d",
                          static_cast<unsigned>(overlay.carDebug.groundMask),
                          static_cast<int>(overlay.segment.deltaX),
                          static_cast<int>(overlay.segment.deltaZ),
                          static_cast<unsigned>(overlay.carDebug.wallHit ? 1u : 0u),
                          static_cast<int>(overlay.carDebug.wallPushX),
                          static_cast<int>(overlay.carDebug.wallPushZ));
    }

    void PrintPhysicsQueryOverlay(const OverlayDiagnosticsSnapshot& overlay) const
    {
        if constexpr (!kEnablePhysicsSafeTelemetry)
        {
            return;
        }
        SRL::Debug::Print(1, 23, "OVR q q:%u g:%u ch:%u cm:%u w:%u/%u",
                          static_cast<unsigned>(overlay.queryCalls),
                          static_cast<unsigned>(overlay.queryGlobalPasses),
                          static_cast<unsigned>(overlay.queryCacheHits),
                          static_cast<unsigned>(overlay.queryCacheMisses),
                          static_cast<unsigned>(overlay.wallQueryHits),
                          static_cast<unsigned>(overlay.wallQueryCalls));
    }

    void PrintInputOverlay(const OverlayDiagnosticsSnapshot& overlay) const
    {
        SRL::Debug::Print(1, 28, "OVR inp dL:%u dR:%u L:%u R:%u C:%u B:%u",
                          static_cast<unsigned>(overlay.input.steerLeftHeld ? 1u : 0u),
                          static_cast<unsigned>(overlay.input.steerRightHeld ? 1u : 0u),
                          static_cast<unsigned>(overlay.input.shiftDownHeld ? 1u : 0u),
                          static_cast<unsigned>(overlay.input.shiftUpHeld ? 1u : 0u),
                          static_cast<unsigned>(overlay.input.brakeHeld ? 1u : 0u),
                          static_cast<unsigned>(overlay.input.accelerateHeld ? 1u : 0u));
    }

    void PrintSegmentEventOverlay(const SegmentOverlaySnapshot& overlay)
    {
        const bool carSegChanged =
            (overlay.carSegmentId > 0) &&
            (diagPrevCarSegmentId_ > 0) &&
            (overlay.carSegmentId != diagPrevCarSegmentId_);
        const bool startChanged =
            (overlay.windowStartId > 0) &&
            (diagPrevWindowStartId_ > 0) &&
            (overlay.windowStartId != diagPrevWindowStartId_);
        if constexpr (!kEnablePhysicsSafeTelemetry)
        {
            if (carSegChanged || startChanged)
            {
                SRL::Debug::Print(1, 23, "OVR evt car:%d>%d ws:%d>%d",
                                  static_cast<int>(diagPrevCarSegmentId_),
                                  static_cast<int>(overlay.carSegmentId),
                                  static_cast<int>(diagPrevWindowStartId_),
                                  static_cast<int>(overlay.windowStartId));
            }
            else
            {
                SRL::Debug::Print(1, 23, "OVR evt -");
            }
        }

        if (overlay.carSegmentId > 0) diagPrevCarSegmentId_ = static_cast<int16_t>(overlay.carSegmentId);
        if (overlay.windowStartId > 0) diagPrevWindowStartId_ = static_cast<int16_t>(overlay.windowStartId);
    }

    static int32_t FxpToIntDebug(const SRL::Math::Types::Fxp& v)
    {
        return v.RawValue() >> 16;
    }

    bool BuildOverlayDiagnosticsSnapshot(uint32_t submittedTrackFaces,
                                         uint32_t submittedCarFaces,
                                         OverlayDiagnosticsSnapshot& out) const
    {
        if (!BuildSegmentOverlaySnapshot(out.segment)) return false;

        out.carDebug = CarRuntimeDebugSnapshot();
        out.input = (context_.carSystem && context_.carSystem->get())
            ? context_.carSystem->get()->LastGameplayInput()
            : Game::CarSystem::GameplayInputSnapshot{};
        out.submittedTrackFaces = submittedTrackFaces;
        out.submittedCarFaces = submittedCarFaces;
        out.submittedFacesTotal = submittedTrackFaces + submittedCarFaces;

        if (context_.trackSystem)
        {
            out.queryCalls = context_.trackSystem->SurfaceQueryCallsThisFrame();
            out.queryGlobalPasses = context_.trackSystem->SurfaceQueryGlobalPassesThisFrame();
            out.queryCacheHits = context_.trackSystem->SurfaceQueryCacheHitsThisFrame();
            out.queryCacheMisses = context_.trackSystem->SurfaceQueryCacheMissesThisFrame();
            out.wallQueryCalls = context_.trackSystem->WallQueryCallsThisFrame();
            out.wallQueryHits = context_.trackSystem->WallQueryHitsThisFrame();
        }

        return true;
    }

    bool BuildSegmentOverlaySnapshot(SegmentOverlaySnapshot& out) const
    {
        if (!context_.trackSystemReady || !context_.trackSystem) return false;

        const bool windowValid =
            context_.trackSystem->GetRenderWindowDebugSnapshot(out.windowStartId,
                                                               out.windowDir,
                                                               out.windowCount);

        out.carSegmentId = static_cast<int32_t>(latestActiveSegmentId_);
        int32_t nearestSegmentId = -1;
        SRL::Math::Types::Vector3D nearestCenter{};
        (void)context_.trackSystem->FindNearestSegment(context_.carWorldPosition,
                                                       context_.trackSegOffset,
                                                       nearestSegmentId,
                                                       nearestCenter);
        out.nearestSegmentId = nearestSegmentId;

        SRL::Math::Types::Vector3D carSegmentCenter{};
        const bool carCenterValid =
            (out.carSegmentId > 0) &&
            context_.trackSystem->FindSegmentCenterById(out.carSegmentId,
                                                        context_.trackSegOffset,
                                                        carSegmentCenter);

        out.carY = FxpToIntDebug(context_.carWorldPosition.Y);
        out.carX = FxpToIntDebug(context_.carWorldPosition.X);
        out.carZ = FxpToIntDebug(context_.carWorldPosition.Z);
        out.camX = FxpToIntDebug(lastValidCameraLocation_.X);
        out.camY = FxpToIntDebug(lastValidCameraLocation_.Y);
        out.camZ = FxpToIntDebug(lastValidCameraLocation_.Z);
        out.segY = carCenterValid ? FxpToIntDebug(carSegmentCenter.Y) : 0;
        out.deltaY = carCenterValid
            ? FxpToIntDebug(context_.carWorldPosition.Y - carSegmentCenter.Y)
            : 0;
        out.deltaX = carCenterValid
            ? FxpToIntDebug(context_.carWorldPosition.X - carSegmentCenter.X)
            : 0;
        out.deltaZ = carCenterValid
            ? FxpToIntDebug(context_.carWorldPosition.Z - carSegmentCenter.Z)
            : 0;
        out.camDirX = FxpToIntDebug(lastValidLookTarget_.X - lastValidCameraLocation_.X);
        out.camDirZ = FxpToIntDebug(lastValidLookTarget_.Z - lastValidCameraLocation_.Z);
        out.carXSgn = (out.carX >= 0) ? '+' : '-';
        out.carYSgn = (out.carY >= 0) ? '+' : '-';
        out.carZSgn = (out.carZ >= 0) ? '+' : '-';
        out.camXSgn = (out.camX >= 0) ? '+' : '-';
        out.camYSgn = (out.camY >= 0) ? '+' : '-';
        out.camZSgn = (out.camZ >= 0) ? '+' : '-';

        const auto yawAngle =
            SRL::Math::Types::Angle::FromDegrees(
                SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(carYawDeg_) << 16));
        out.fwdX = FxpToIntDebug(SRL::Math::Trigonometry::Sin(yawAngle));
        out.fwdZ = FxpToIntDebug(
            SRL::Math::Types::Fxp::BuildRaw(-SRL::Math::Trigonometry::Cos(yawAngle).RawValue()));

        if (windowValid)
        {
            for (size_t i = 0; i < out.seq.size(); ++i)
            {
                int32_t id = -1;
                if (context_.trackSystem->GetRenderWindowSegmentIdAt(i, id))
                {
                    out.seq[i] = id;
                }
            }
        }

        return true;
    }

    Sh2SplitTelemetrySnapshot BuildSh2SplitTelemetrySnapshot() const
    {
        Sh2SplitTelemetrySnapshot snapshot{};
        if (!context_.trackSystemReady || !context_.trackSystem) return snapshot;

        snapshot.trackMasterTicks = context_.trackSystem->FrameTicksThisFrame();
        snapshot.trackSlaveProducerTicks = context_.trackSystem->Telemetry().producer.slaveLastJobTicks;
        snapshot.trackSlaveSortTicks = context_.trackSystem->SlaveSortTicksThisFrame();
        snapshot.trackSlavePlanTicks = context_.trackSystem->SlavePlanTicksThisFrame();
        snapshot.simSlaveTicks = simState_.slaveLastJobTicksThisFrame;
        snapshot.simMasterWaitTicks = simState_.masterWaitTicksThisFrame;

        snapshot.masterBusyTicks = static_cast<uint32_t>(snapshot.trackMasterTicks);
        snapshot.masterWaitTicks = static_cast<uint32_t>(snapshot.simMasterWaitTicks);
        snapshot.slaveWorkTicks =
            static_cast<uint32_t>(snapshot.trackSlaveProducerTicks) +
            static_cast<uint32_t>(snapshot.trackSlaveSortTicks) +
            static_cast<uint32_t>(snapshot.trackSlavePlanTicks) +
            static_cast<uint32_t>(snapshot.simSlaveTicks);
        snapshot.busyTotal = snapshot.masterBusyTicks + snapshot.slaveWorkTicks;
        snapshot.masterBusyPct = (snapshot.busyTotal > 0u)
            ? static_cast<uint32_t>((snapshot.masterBusyTicks * 100u) / snapshot.busyTotal)
            : 0u;
        snapshot.slaveWorkPct = (snapshot.busyTotal > 0u)
            ? static_cast<uint32_t>((snapshot.slaveWorkTicks * 100u) / snapshot.busyTotal)
            : 0u;
        snapshot.masterWaitPctOfSim = (snapshot.simSlaveTicks > 0u)
            ? static_cast<uint32_t>((snapshot.masterWaitTicks * 100u) / static_cast<uint32_t>(snapshot.simSlaveTicks))
            : 0u;

        if constexpr (kEnablePhysicsSafeTelemetry)
        {
            snapshot.queryCalls = context_.trackSystem
                ? context_.trackSystem->SurfaceQueryCallsThisFrame()
                : 0u;
            snapshot.queryGlobal = context_.trackSystem
                ? context_.trackSystem->SurfaceQueryGlobalPassesThisFrame()
                : 0u;
            snapshot.queryScmap = context_.trackSystem
                ? context_.trackSystem->SurfaceQueryScmapSkipsThisFrame()
                : 0u;
        }
        snapshot.valid = true;
        return snapshot;
    }

    void PrintSh2SplitTelemetry(const Sh2SplitTelemetrySnapshot& snapshot)
    {
        if (!snapshot.valid) return;

        SRL::Debug::Print(1, 24, "SH2 busy M:%u%% S:%u%% mb:%u sw:%u",
                          static_cast<unsigned>(snapshot.masterBusyPct),
                          static_cast<unsigned>(snapshot.slaveWorkPct),
                          static_cast<unsigned>(snapshot.masterBusyTicks),
                          static_cast<unsigned>(snapshot.slaveWorkTicks));
        if constexpr (kEnablePhysicsSafeTelemetry)
        {
            SRL::Debug::Print(1, 25, "SH2 sim s:%u w:%u q:%u g:%u m:%u",
                              static_cast<unsigned>(snapshot.simSlaveTicks),
                              static_cast<unsigned>(snapshot.masterWaitTicks),
                              static_cast<unsigned>(snapshot.queryCalls),
                              static_cast<unsigned>(snapshot.queryGlobal),
                              static_cast<unsigned>(snapshot.queryScmap));
        }
        else
        {
            SRL::Debug::Print(1, 25, "SH2 sim slv:%u wait:%u (%u%%) ds:%u tb:%u",
                              static_cast<unsigned>(snapshot.simSlaveTicks),
                              static_cast<unsigned>(snapshot.masterWaitTicks),
                              static_cast<unsigned>(snapshot.masterWaitPctOfSim),
                              static_cast<unsigned>(simState_.slaveDispatchCount),
                              static_cast<unsigned>(simState_.slaveDispatchSkipsTrackBusy));
        }
    }

    void UpdateRealtimeFpsOverlay()
    {
        constexpr bool kEnableRealtimeFpsOverlay = true;
        if constexpr (!kEnableRealtimeFpsOverlay)
        {
            return;
        }

#ifdef SRL_MODE_NTSC
        constexpr uint32_t kDisplayRefreshHz = 60u;
#else
        constexpr uint32_t kDisplayRefreshHz = 50u;
#endif

        const uint32_t vblankNow = SRL_AppGetVblankCounter();
        if (!fpsState_.vblankValid)
        {
            fpsState_.vblankValid = true;
            fpsState_.lastVblank = vblankNow;
            return;
        }

        uint32_t vblankDelta = vblankNow - fpsState_.lastVblank;
        fpsState_.lastVblank = vblankNow;
        if (vblankDelta == 0u)
        {
            // Should not happen in steady state, but keep the metric stable.
            vblankDelta = 1u;
        }

        ++fpsState_.sampleFrames;
        fpsState_.sampleVblanks += vblankDelta;
        const uint32_t frameTimeX100 = static_cast<uint32_t>(
            (static_cast<uint64_t>(vblankDelta) * 100000u + (kDisplayRefreshHz / 2u)) /
            static_cast<uint64_t>(kDisplayRefreshHz));
        constexpr uint32_t kTarget30FrameTimeX100 = 100000u / 30u; // 33.33 ms
        constexpr uint32_t kTarget60FrameTimeX100 = 100000u / 60u; // 16.67 ms
        if (frameTimeX100 > kTarget30FrameTimeX100)
        {
            ++fpsState_.framesOver30Budget;
        }
        if (frameTimeX100 > kTarget60FrameTimeX100)
        {
            ++fpsState_.framesOver60Budget;
        }

        constexpr uint32_t kSampleWindowFrames = 60u;
        if (fpsState_.sampleVblanks == 0u)
        {
            return;
        }

        const uint64_t fpsNum = static_cast<uint64_t>(kDisplayRefreshHz) *
                                static_cast<uint64_t>(10u) *
                                static_cast<uint64_t>(fpsState_.sampleFrames);
        const uint32_t fpsX10 = static_cast<uint32_t>(
            (fpsNum + static_cast<uint64_t>(fpsState_.sampleVblanks / 2u)) /
            static_cast<uint64_t>(fpsState_.sampleVblanks));

        const uint64_t frameMsNum = static_cast<uint64_t>(10000u) * static_cast<uint64_t>(fpsState_.sampleVblanks);
        const uint64_t frameMsDen = static_cast<uint64_t>(kDisplayRefreshHz) *
                                    static_cast<uint64_t>(fpsState_.sampleFrames);
        const uint32_t frameMsX10 = static_cast<uint32_t>(
            (frameMsNum + (frameMsDen / 2u)) / std::max<uint64_t>(1u, frameMsDen));

        const uint64_t vbNum = static_cast<uint64_t>(fpsState_.sampleVblanks) * static_cast<uint64_t>(100u);
        const uint32_t vbX100 = static_cast<uint32_t>(
            (vbNum + static_cast<uint64_t>(fpsState_.sampleFrames / 2u)) /
            static_cast<uint64_t>(fpsState_.sampleFrames));

        const uint32_t drop30Pct = (fpsState_.sampleFrames > 0u)
            ? static_cast<uint32_t>((static_cast<uint64_t>(fpsState_.framesOver30Budget) * 100u) /
                                    static_cast<uint64_t>(fpsState_.sampleFrames))
            : 0u;
        const uint32_t drop60Pct = (fpsState_.sampleFrames > 0u)
            ? static_cast<uint32_t>((static_cast<uint64_t>(fpsState_.framesOver60Budget) * 100u) /
                                    static_cast<uint64_t>(fpsState_.sampleFrames))
            : 0u;

        SRL::Debug::Print(0, 16, "FPS:%u.%u ms:%u.%u vb:%u.%02u d30:%u%% d60:%u%%",
                          static_cast<unsigned>(fpsX10 / 10u),
                          static_cast<unsigned>(fpsX10 % 10u),
                          static_cast<unsigned>(frameMsX10 / 10u),
                          static_cast<unsigned>(frameMsX10 % 10u),
                          static_cast<unsigned>(vbX100 / 100u),
                          static_cast<unsigned>(vbX100 % 100u),
                          static_cast<unsigned>(drop30Pct),
                          static_cast<unsigned>(drop60Pct));

        if (fpsState_.sampleFrames >= kSampleWindowFrames)
        {
            fpsState_.sampleFrames = 0u;
            fpsState_.sampleVblanks = 0u;
            fpsState_.framesOver30Budget = 0u;
            fpsState_.framesOver60Budget = 0u;
        }
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

    static SRL::Math::Types::Vector3D NormalizeFlatDirectionRaw(
        int32_t dxRaw,
        int32_t dzRaw,
        const SRL::Math::Types::Vector3D& fallback)
    {
        const int32_t adx = (dxRaw < 0) ? -dxRaw : dxRaw;
        const int32_t adz = (dzRaw < 0) ? -dzRaw : dzRaw;
        const int32_t maxAxis = (adx > adz) ? adx : adz;
        if (maxAxis <= 0) return fallback;

        const int64_t nxRaw = (static_cast<int64_t>(dxRaw) << 16) / maxAxis;
        const int64_t nzRaw = (static_cast<int64_t>(dzRaw) << 16) / maxAxis;
        return SRL::Math::Types::Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(nxRaw)),
            SRL::Math::Types::Fxp::BuildRaw(0),
            SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(nzRaw)));
    }

    void PublishEmptyCameraPathFrameContext()
    {
        if (!context_.cameraSystem)
        {
            return;
        }
        CameraSystem::PathFrameContext pathCtx{};
        pathCtx.valid = false;
        context_.cameraSystem->SetPathFrameContext(pathCtx);
    }

    void AlignCameraPathStartupYawIfNeeded(size_t nearestIndex)
    {
        if (autoLapRoute_.startupYawAligned ||
            autoLapTestEnabled_ ||
            nearestIndex >= autoLapRoute_.yawDeg.size())
        {
            return;
        }

        carYawDeg_ = NormalizeYawDeg360(static_cast<int32_t>(autoLapRoute_.yawDeg[nearestIndex]));
        SyncCameraHeadingFromCar();
        autoLapRoute_.startupYawAligned = true;
    }

    void PopulateCameraPathGeometryContext(CameraSystem::PathFrameContext& pathCtx,
                                           size_t prevIndex,
                                           size_t nearestIndex,
                                           size_t nextIndex,
                                           const SRL::Math::Types::Vector3D& fallbackForward) const
    {
        const int32_t dxRaw = autoLapRoute_.centers[nextIndex].X.RawValue() -
                              autoLapRoute_.centers[nearestIndex].X.RawValue();
        const int32_t dzRaw = autoLapRoute_.centers[nextIndex].Z.RawValue() -
                              autoLapRoute_.centers[nearestIndex].Z.RawValue();
        pathCtx.forwardWorld = NormalizeFlatDirectionRaw(dxRaw, dzRaw, fallbackForward);
        const int32_t dyRaw = autoLapRoute_.centers[nextIndex].Y.RawValue() -
                              autoLapRoute_.centers[nearestIndex].Y.RawValue();

        const int32_t prevDxRaw = autoLapRoute_.centers[nearestIndex].X.RawValue() -
                                  autoLapRoute_.centers[prevIndex].X.RawValue();
        const int32_t prevDzRaw = autoLapRoute_.centers[nearestIndex].Z.RawValue() -
                                  autoLapRoute_.centers[prevIndex].Z.RawValue();
        const SRL::Math::Types::Vector3D prevDir =
            NormalizeFlatDirectionRaw(prevDxRaw, prevDzRaw, pathCtx.forwardWorld);
        const SRL::Math::Types::Fxp one = SRL::Math::Types::Fxp::BuildRaw(1 << 16);
        SRL::Math::Types::Fxp dot =
            (prevDir.X * pathCtx.forwardWorld.X) +
            (prevDir.Z * pathCtx.forwardWorld.Z);
        if (dot > one) dot = one;
        if (dot < -one) dot = -one;
        SRL::Math::Types::Fxp curvature = one - dot;
        if (curvature < SRL::Math::Types::Fxp::BuildRaw(0)) curvature = SRL::Math::Types::Fxp::BuildRaw(0);
        if (curvature > one) curvature = one;
        pathCtx.curvatureAbsRaw = curvature.RawValue();

        const int32_t turnCrossRaw = static_cast<int32_t>(
            ((static_cast<int64_t>(prevDir.X.RawValue()) * pathCtx.forwardWorld.Z.RawValue()) -
             (static_cast<int64_t>(prevDir.Z.RawValue()) * pathCtx.forwardWorld.X.RawValue())) >> 16);
        if (turnCrossRaw > (1 << 10))
        {
            pathCtx.turnSign = 1;
        }
        else if (turnCrossRaw < -(1 << 10))
        {
            pathCtx.turnSign = -1;
        }
        else
        {
            pathCtx.turnSign = 0;
        }

        const int32_t absDxRaw = std::abs(dxRaw);
        const int32_t absDzRaw = std::abs(dzRaw);
        const int32_t absDyRaw = std::abs(dyRaw);
        const int32_t horizRaw = std::max<int32_t>(1, std::max(absDxRaw, absDzRaw));
        int32_t slopeRaw = static_cast<int32_t>(
            (static_cast<int64_t>(absDyRaw) << 16) / static_cast<int64_t>(horizRaw));
        slopeRaw = std::clamp<int32_t>(slopeRaw, 0, (1 << 16));
        pathCtx.slopeAbsRaw = slopeRaw;
    }

    void PopulateCameraPathSpeedContext(CameraSystem::PathFrameContext& pathCtx)
    {
        if (!cameraPathRuntime_.prevCarWorldPositionValid)
        {
            cameraPathRuntime_.prevCarWorldPosition = context_.carWorldPosition;
            cameraPathRuntime_.prevCarWorldPositionValid = true;
            pathCtx.speedNormRaw = 0;
            return;
        }

        const int32_t moveDxRaw =
            context_.carWorldPosition.X.RawValue() - cameraPathRuntime_.prevCarWorldPosition.X.RawValue();
        const int32_t moveDzRaw =
            context_.carWorldPosition.Z.RawValue() - cameraPathRuntime_.prevCarWorldPosition.Z.RawValue();
        const int32_t maxAxis = std::max(std::abs(moveDxRaw), std::abs(moveDzRaw));
        constexpr int32_t kSpeedForMaxNormRaw = (18 << 16);
        int32_t speedNormRaw = static_cast<int32_t>(
            (static_cast<int64_t>(maxAxis) << 16) / kSpeedForMaxNormRaw);
        speedNormRaw = std::clamp<int32_t>(speedNormRaw, 0, (1 << 16));
        pathCtx.speedNormRaw = speedNormRaw;
        cameraPathRuntime_.prevCarWorldPosition = context_.carWorldPosition;
    }

    void UpdateCameraPathFrameContext()
    {
        if (!context_.cameraSystem)
        {
            return;
        }

        CameraSystem::PathFrameContext pathCtx{};
        pathCtx.valid = false;
        const SRL::Math::Types::Vector3D fallbackForward(0.0, 0.0, 1.0f);

        if (!CameraSystem::kPathGuidedChaseEnabled)
        {
            if (!autoLapTestEnabled_ && HasRetainedAutoLapRouteStorage())
            {
                ReleaseAutoLapRouteStorage();
            }
            cameraPathRuntime_.prevCarWorldPositionValid = false;
            PublishEmptyCameraPathFrameContext();
            return;
        }

        if (!context_.trackSystem || !context_.trackSystemReady)
        {
            PublishEmptyCameraPathFrameContext();
            return;
        }

        if (!autoLapRoute_.built)
        {
            BuildAutoLapRoute(context_, context_.carWorldPosition);
        }
        if (autoLapRoute_.centers.size() < 2u)
        {
            PublishEmptyCameraPathFrameContext();
            return;
        }

        const size_t routeCount = autoLapRoute_.centers.size();
        size_t nearestIndex = 0u;
        if (!FindNearestAutoLapRoutePointIndex(context_.carWorldPosition, nearestIndex))
        {
            PublishEmptyCameraPathFrameContext();
            return;
        }

        const size_t prevIndex = (nearestIndex + routeCount - 1u) % routeCount;
        const size_t nextIndex = (nearestIndex + 1u) % routeCount;
        AlignCameraPathStartupYawIfNeeded(nearestIndex);
        PopulateCameraPathGeometryContext(pathCtx, prevIndex, nearestIndex, nextIndex, fallbackForward);
        PopulateCameraPathSpeedContext(pathCtx);

        pathCtx.valid = true;
        context_.cameraSystem->SetPathFrameContext(pathCtx);
    }

    void RebuildAutoLapRouteYawData()
    {
        autoLapRoute_.yawDeg.clear();
        autoLapRoute_.offDeg.clear();
        autoLapRoute_.baseYawDeg = 0;

        const size_t pointCount = autoLapRoute_.centers.size();
        if (pointCount < 2u) return;

        autoLapRoute_.yawDeg.reserve(pointCount);
        for (size_t i = 0u; i < pointCount; ++i)
        {
            const size_t j = (i + 1u) % pointCount;
            const int32_t dxRaw =
                autoLapRoute_.centers[j].X.RawValue() - autoLapRoute_.centers[i].X.RawValue();
            const int32_t dzRaw =
                autoLapRoute_.centers[j].Z.RawValue() - autoLapRoute_.centers[i].Z.RawValue();
            const int32_t fallbackYaw = autoLapRoute_.yawDeg.empty()
                                            ? 0
                                            : static_cast<int32_t>(autoLapRoute_.yawDeg.back());
            const int32_t yawDeg = YawFromDeltaRaw(dxRaw, dzRaw, fallbackYaw);
            autoLapRoute_.yawDeg.push_back(static_cast<int16_t>(yawDeg));
        }

        autoLapRoute_.baseYawDeg = static_cast<int16_t>(autoLapRoute_.yawDeg.front());
        autoLapRoute_.offDeg.reserve(pointCount);
        for (size_t i = 0u; i < pointCount; ++i)
        {
            const int32_t offDeg = ShortestDeltaDeg(
                static_cast<int32_t>(autoLapRoute_.baseYawDeg),
                static_cast<int32_t>(autoLapRoute_.yawDeg[i]));
            autoLapRoute_.offDeg.push_back(static_cast<int16_t>(offDeg));
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
        if (!autoLapRoute_.built)
        {
            BuildAutoLapRoute(context, ioCarWorldPosition);
        }
        if (autoLapRoute_.centers.size() < 2) return;
        if (autoLapRoute_.yawDeg.size() != autoLapRoute_.centers.size() ||
            autoLapRoute_.offDeg.size() != autoLapRoute_.centers.size())
        {
            RebuildAutoLapRouteYawData();
        }
        // Keep only a tiny clearance above asphalt to prevent z-fighting.
        const auto rideHeight = SRL::Math::Types::Fxp::BuildRaw(-(1 << 13));
        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());

        if (!autoLapRoute_.initialized)
        {
            size_t bestIdx = 0u;
            if (!FindNearestAutoLapRoutePointIndex(ioCarWorldPosition, bestIdx)) return;

            autoLapRoute_.index = static_cast<uint16_t>(bestIdx);
            ioCarWorldPosition.X = autoLapRoute_.centers[autoLapRoute_.index].X;
            ioCarWorldPosition.Z = autoLapRoute_.centers[autoLapRoute_.index].Z;
            ioCarWorldPosition.Y =
                ResolveAutoLapRouteGroundYAt(context, autoLapRoute_.index, ioCarWorldPosition.Y) + rideHeight;
            autoLapRoute_.initialized = true;
            if (autoLapRoute_.yawDeg.size() == autoLapRoute_.centers.size() &&
                autoLapRoute_.index < autoLapRoute_.yawDeg.size())
            {
                ioCarYawDeg = autoLapRoute_.yawDeg[autoLapRoute_.index];
            }
            if (!autoLapRoute_.ids.empty() && autoLapRoute_.index < autoLapRoute_.ids.size())
            {
                AdvanceObservedAutoLapSegmentToward(segmentCount, autoLapRoute_.ids[autoLapRoute_.index]);
            }
        }

        const size_t routePointCount = autoLapRoute_.centers.size();
        size_t currentIndex = static_cast<size_t>(autoLapRoute_.index);
        size_t nextIndex = (currentIndex + 1u) % routePointCount;
        const SRL::Math::Types::Vector3D& currentCenter = autoLapRoute_.centers[currentIndex];
        const SRL::Math::Types::Vector3D& nextCenter = autoLapRoute_.centers[nextIndex];
        const SRL::Math::Types::Fxp currentGroundY =
            ResolveAutoLapRouteGroundYAt(context, currentIndex, ioCarWorldPosition.Y);
        const SRL::Math::Types::Fxp nextGroundY =
            ResolveAutoLapRouteGroundYAt(context, nextIndex, ioCarWorldPosition.Y);
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
            ioCarWorldPosition.Y = currentGroundY + ((nextGroundY - currentGroundY) * alpha) + rideHeight;
        }
        else
        {
            ioCarWorldPosition.Y = nextGroundY + rideHeight;
        }

        for (size_t guard = 0u; guard < 8u; ++guard)
        {
            if (!HasReachedOrPassedAutoLapWaypoint(currentIndex, nextIndex, ioCarWorldPosition)) break;
            currentIndex = nextIndex;
            nextIndex = (currentIndex + 1u) % routePointCount;
            ioCarWorldPosition.Y =
                ResolveAutoLapRouteGroundYAt(context, currentIndex, ioCarWorldPosition.Y) + rideHeight;
        }
        autoLapRoute_.index = static_cast<uint16_t>(currentIndex);

        if (!autoLapRoute_.ids.empty() && autoLapRoute_.index < autoLapRoute_.ids.size())
        {
            AdvanceObservedAutoLapSegmentToward(segmentCount, autoLapRoute_.ids[autoLapRoute_.index]);
        }
        UpdateAutoLapHeading(currentIndex, nextIndex, routePointCount, ioCarYawDeg);
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
        for (size_t i = 0; i < autoLapRoute_.guideLines.size(); ++i)
        {
            autoLapRoute_.guideLines[i].clear();
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
            if constexpr (kEnableAutoPathLogs)
            {
                SRL::Debug::Print(1, 23, "AUTO PATH read fail");
            }
            return false;
        }

        PathNya::ParseResult parsed{};
        if (!PathNya::Parse(bytes.data(), bytes.size(), parsed))
        {
            if constexpr (kEnableAutoPathLogs)
            {
                SRL::Debug::Print(1, 23, "AUTO PATH parse fail %s",
                                  loadedCandidate ? loadedCandidate : "none");
                SRL::Debug::Print(1, 24, "AUTO PATH parse sz:%u",
                                  static_cast<unsigned>(bytes.size()));
            }
            return false;
        }

        for (size_t lineIndex = 0; lineIndex < autoLapRoute_.guideLines.size(); ++lineIndex)
        {
            const auto& srcLine = parsed.lines[lineIndex];
            auto& dstLine = autoLapRoute_.guideLines[lineIndex];
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

        if constexpr (kEnableAutoPathLogs)
        {
            SRL::Debug::Print(1, 23, "AUTO PATH ok %s",
                              loadedCandidate ? loadedCandidate : "none");
            SRL::Debug::Print(1, 24, "AUTO PATH v:%u l0:%u l1:%u l2:%u",
                              static_cast<unsigned>(parsed.version),
                              static_cast<unsigned>(autoLapRoute_.guideLines[0].size()),
                              static_cast<unsigned>(autoLapRoute_.guideLines[1].size()),
                              static_cast<unsigned>(autoLapRoute_.guideLines[2].size()));
        }
        return true;
    }

    bool BuildAutoLapRouteFromPathGuide(const Context& context,
                                        const SRL::Math::Types::Vector3D& referenceCarWorldPosition)
    {
        if (!LoadAutoLapGuideLines()) return false;

        int32_t selectedLineIndex = SelectBestAutoLapGuideLineIndex(referenceCarWorldPosition);
        if (selectedLineIndex < 0)
        {
            if constexpr (kEnableAutoPathLogs)
            {
                SRL::Debug::Print(1, 24, "AUTO PATH line empty");
            }
            return false;
        }
        autoLapRoute_.selectedGuideLine = static_cast<int8_t>(selectedLineIndex);
        const auto& selectedLine = autoLapRoute_.guideLines[static_cast<size_t>(selectedLineIndex)];

        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());
        if (segmentCount <= 0) return false;

        const auto simplifiedMiddleLine =
            SimplifyAutoLapGuideLine(selectedLine, static_cast<size_t>(segmentCount));
        const auto& routeLine = (simplifiedMiddleLine.size() >= 2u) ? simplifiedMiddleLine : selectedLine;

        autoLapRoute_.centers.reserve(routeLine.size());
        autoLapRoute_.ids.reserve(routeLine.size());

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
            autoLapRoute_.centers.push_back(routePoint);

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
            autoLapRoute_.ids.push_back(static_cast<int16_t>(mappedSegmentId));
        }

        // If PATH points were exported in reverse order, the car will turn in
        // the opposite direction at curves. Normalize to forward segment flow.
        int32_t directionScore = 0;
        if (autoLapRoute_.ids.size() >= 2u)
        {
            for (size_t i = 0; i < autoLapRoute_.ids.size(); ++i)
            {
                const int32_t fromId = autoLapRoute_.ids[i];
                const int32_t toId = autoLapRoute_.ids[(i + 1u) % autoLapRoute_.ids.size()];
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
            std::reverse(autoLapRoute_.centers.begin(), autoLapRoute_.centers.end());
            std::reverse(autoLapRoute_.ids.begin(), autoLapRoute_.ids.end());
            if constexpr (kEnableAutoPathLogs)
            {
                SRL::Debug::Print(1, 26, "AUTO PATH dir:REV fix");
            }
        }
        else
        {
            if constexpr (kEnableAutoPathLogs)
            {
                SRL::Debug::Print(1, 26, "AUTO PATH dir:FWD");
            }
        }

        if constexpr (kEnableAutoPathLogs)
        {
            SRL::Debug::Print(1, 25, "AUTO PATH l:%d raw:%u out:%u",
                              static_cast<int>(selectedLineIndex),
                              static_cast<unsigned>(selectedLine.size()),
                              static_cast<unsigned>(routeLine.size()));
        }
        RebuildAutoLapRouteYawData();
        return !autoLapRoute_.centers.empty() &&
               autoLapRoute_.centers.size() == autoLapRoute_.ids.size() &&
               autoLapRoute_.centers.size() == autoLapRoute_.yawDeg.size() &&
               autoLapRoute_.centers.size() == autoLapRoute_.offDeg.size();
    }

    void ReleaseAutoLapGuideLines()
    {
        for (size_t i = 0; i < autoLapRoute_.guideLines.size(); ++i)
        {
            using GuideLineVector = std::remove_reference_t<decltype(autoLapRoute_.guideLines[i])>;
            GuideLineVector{}.swap(autoLapRoute_.guideLines[i]);
        }
    }

    void ReleaseAutoLapRouteStorage()
    {
        ClearAutoLapRouteBuffers();
        ReleaseAutoLapGuideLines();
        ResetAutoLapRouteState();
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

    bool HasRetainedAutoLapRouteStorage() const
    {
        return autoLapRoute_.built ||
               autoLapRoute_.initialized ||
               autoLapRoute_.ids.capacity() > 0u ||
               autoLapRoute_.centers.capacity() > 0u ||
               autoLapRoute_.yawDeg.capacity() > 0u ||
               autoLapRoute_.offDeg.capacity() > 0u;
    }

    void ResetAutoLapRouteState()
    {
        autoLapRoute_.initialized = false;
        autoLapRoute_.built = false;
        autoLapRoute_.startupYawAligned = false;
        autoLapRoute_.index = 0;
        autoLapRoute_.baseYawDeg = 0;
        autoLapRoute_.currentOffDeg = 0;
        autoLapRoute_.selectedGuideLine = -1;
    }

    void ClearAutoLapRouteBuffers()
    {
        autoLapRoute_.ids.clear();
        autoLapRoute_.centers.clear();
        autoLapRoute_.yawDeg.clear();
        autoLapRoute_.offDeg.clear();
        TrackLowWorkVector<int16_t>{}.swap(autoLapRoute_.ids);
        TrackLowWorkVector<SRL::Math::Types::Vector3D>{}.swap(autoLapRoute_.centers);
        TrackLowWorkVector<int16_t>{}.swap(autoLapRoute_.yawDeg);
        TrackLowWorkVector<int16_t>{}.swap(autoLapRoute_.offDeg);
    }

    bool FindNearestAutoLapRoutePointIndex(const SRL::Math::Types::Vector3D& worldPosition,
                                           size_t& outIndex) const
    {
        if (autoLapRoute_.centers.empty())
        {
            return false;
        }

        size_t nearestIndex = 0u;
        bool foundNearest = false;
        SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        for (size_t i = 0u; i < autoLapRoute_.centers.size(); ++i)
        {
            const auto& routePoint = autoLapRoute_.centers[i];
            const auto dx = (routePoint.X - worldPosition.X).Abs();
            const auto dz = (routePoint.Z - worldPosition.Z).Abs();
            const auto score = dx + dz;
            if (!foundNearest || score < bestScore)
            {
                foundNearest = true;
                bestScore = score;
                nearestIndex = i;
            }
        }

        if (!foundNearest)
        {
            return false;
        }

        outIndex = nearestIndex;
        return true;
    }

    int32_t SelectBestAutoLapGuideLineIndex(const SRL::Math::Types::Vector3D& referenceCarWorldPosition) const
    {
        int32_t selectedLineIndex = -1;
        SRL::Math::Types::Fxp selectedScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        for (int32_t lineIndex = 0; lineIndex < static_cast<int32_t>(autoLapRoute_.guideLines.size()); ++lineIndex)
        {
            const auto& line = autoLapRoute_.guideLines[lineIndex];
            if (line.size() < 2u) continue;

            SRL::Math::Types::Fxp lineScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
            for (size_t i = 0; i < line.size(); ++i)
            {
                const auto worldPoint = line[i] + context_.trackSegOffset;
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

        return selectedLineIndex;
    }

    void BuildFallbackAutoLapRoute(const Context& context)
    {
        if constexpr (kEnableAutoPathLogs)
        {
            SRL::Debug::Print(1, 24, "AUTO PATH fallback seg centers");
        }

        SRL::Math::Types::Vector3D center{};
        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());
        for (int32_t id = 1; id <= segmentCount; ++id)
        {
            if (!context.trackSystem->FindSegmentCenterById(id, context.trackSegOffset, center)) continue;
            autoLapRoute_.ids.push_back(id);
            autoLapRoute_.centers.push_back(center);
        }
        RebuildAutoLapRouteYawData();
        autoLapRoute_.built = !autoLapRoute_.ids.empty();
        autoLapRoute_.initialized = false;
    }

    int32_t WrapAutoLapSegmentId(int32_t segmentCount, int32_t segmentId) const
    {
        if (segmentCount <= 0) return -1;
        int32_t normalized = (segmentId - 1) % segmentCount;
        if (normalized < 0) normalized += segmentCount;
        return normalized + 1;
    }

    void AdvanceObservedAutoLapSegmentToward(int32_t segmentCount, int32_t desiredSegmentId)
    {
        if (desiredSegmentId <= 0 || segmentCount <= 0)
        {
            latestActiveSegmentId_ = static_cast<int16_t>(desiredSegmentId);
            return;
        }

        desiredSegmentId = WrapAutoLapSegmentId(segmentCount, desiredSegmentId);
        if (latestActiveSegmentId_ <= 0)
        {
            latestActiveSegmentId_ = static_cast<int16_t>(desiredSegmentId);
            return;
        }

        const int32_t currentSegmentId = WrapAutoLapSegmentId(segmentCount, latestActiveSegmentId_);
        if (currentSegmentId <= 0)
        {
            latestActiveSegmentId_ = static_cast<int16_t>(desiredSegmentId);
            return;
        }

        int32_t forwardDistance = (desiredSegmentId - currentSegmentId) % segmentCount;
        if (forwardDistance < 0) forwardDistance += segmentCount;
        if (forwardDistance == 0)
        {
            latestActiveSegmentId_ = static_cast<int16_t>(currentSegmentId);
            return;
        }

        if (forwardDistance < (segmentCount / 2))
        {
            latestActiveSegmentId_ =
                static_cast<int16_t>(WrapAutoLapSegmentId(segmentCount, currentSegmentId + 1));
            return;
        }

        // Ignore backward/noisy remaps from the decimated PATH so the
        // streaming window does not thrash or try to catch up by several
        // segments in one frame.
        latestActiveSegmentId_ = static_cast<int16_t>(currentSegmentId);
    }

    SRL::Math::Types::Fxp ResolveAutoLapRouteGroundYAt(const Context& context,
                                                       size_t routeIndex,
                                                       const SRL::Math::Types::Fxp& fallbackY) const
    {
        static constexpr uint16_t kAsphaltFamilyId = 1u; // F01064.tga

        if (routeIndex < autoLapRoute_.centers.size())
        {
            SRL::Math::Types::Fxp asphaltY{};
            if (context.trackSystem->FindSurfaceYByFamilyId(
                    autoLapRoute_.centers[routeIndex],
                    context.trackSegOffset,
                    kAsphaltFamilyId,
                    asphaltY))
            {
                return asphaltY;
            }
        }

        if (routeIndex < autoLapRoute_.centers.size() &&
            !autoLapRoute_.ids.empty() &&
            routeIndex < autoLapRoute_.ids.size())
        {
            const int32_t routeSegmentId = static_cast<int32_t>(autoLapRoute_.ids[routeIndex]);
            SRL::Math::Types::Vector3D segmentCenter{};
            if (routeSegmentId > 0 &&
                context.trackSystem->FindSegmentCenterById(
                    routeSegmentId,
                    context.trackSegOffset,
                    segmentCenter))
            {
                return segmentCenter.Y;
            }
        }

        return (routeIndex < autoLapRoute_.centers.size())
            ? autoLapRoute_.centers[routeIndex].Y
            : fallbackY;
    }

    bool HasReachedOrPassedAutoLapWaypoint(size_t fromIndex,
                                           size_t toIndex,
                                           const SRL::Math::Types::Vector3D& worldPosition) const
    {
        const auto& from = autoLapRoute_.centers[fromIndex];
        const auto& to = autoLapRoute_.centers[toIndex];
        const int32_t remX = to.X.RawValue() - worldPosition.X.RawValue();
        const int32_t remZ = to.Z.RawValue() - worldPosition.Z.RawValue();
        const int32_t absRemX = (remX < 0) ? -remX : remX;
        const int32_t absRemZ = (remZ < 0) ? -remZ : remZ;
        if (absRemX <= (8 << 16) && absRemZ <= (8 << 16)) return true;

        const int64_t segX = static_cast<int64_t>(to.X.RawValue()) -
                             static_cast<int64_t>(from.X.RawValue());
        const int64_t segZ = static_cast<int64_t>(to.Z.RawValue()) -
                             static_cast<int64_t>(from.Z.RawValue());
        const int64_t toCarX = static_cast<int64_t>(worldPosition.X.RawValue()) -
                               static_cast<int64_t>(to.X.RawValue());
        const int64_t toCarZ = static_cast<int64_t>(worldPosition.Z.RawValue()) -
                               static_cast<int64_t>(to.Z.RawValue());
        return ((segX * toCarX) + (segZ * toCarZ)) >= 0;
    }

    void UpdateAutoLapHeading(size_t currentIndex,
                              size_t nextIndex,
                              size_t routePointCount,
                              int32_t& ioCarYawDeg)
    {
        const auto normalizeYawDeg = [](int32_t yawDeg) -> int32_t
        {
            return NormalizeYawDeg360(yawDeg);
        };

        const size_t headingA = currentIndex;
        constexpr size_t kYawLookAheadPoints = 2u;
        const size_t headingB = (headingA + kYawLookAheadPoints) % routePointCount;

        int32_t pathDxRaw = autoLapRoute_.centers[headingB].X.RawValue() -
                            autoLapRoute_.centers[headingA].X.RawValue();
        int32_t pathDzRaw = autoLapRoute_.centers[headingB].Z.RawValue() -
                            autoLapRoute_.centers[headingA].Z.RawValue();
        if (pathDxRaw == 0 && pathDzRaw == 0)
        {
            pathDxRaw = autoLapRoute_.centers[nextIndex].X.RawValue() -
                        autoLapRoute_.centers[headingA].X.RawValue();
            pathDzRaw = autoLapRoute_.centers[nextIndex].Z.RawValue() -
                        autoLapRoute_.centers[headingA].Z.RawValue();
        }

        const int32_t targetYawDeg = YawFromDeltaRaw(pathDxRaw, pathDzRaw, ioCarYawDeg);
        ioCarYawDeg = normalizeYawDeg(targetYawDeg);
        if (autoLapRoute_.yawDeg.size() == autoLapRoute_.centers.size() &&
            !autoLapRoute_.yawDeg.empty())
        {
            autoLapRoute_.currentOffDeg = static_cast<int16_t>(
                ShortestDeltaDeg(static_cast<int32_t>(autoLapRoute_.baseYawDeg), ioCarYawDeg));
        }
        else
        {
            autoLapRoute_.currentOffDeg = 0;
        }
    }

    // Build preferred route for the player car.
    void BuildAutoLapRoute(const Context& context,
                           const SRL::Math::Types::Vector3D& referenceCarWorldPosition)
    {
        autoLapRoute_.ids.clear();
        autoLapRoute_.centers.clear();
        autoLapRoute_.yawDeg.clear();
        autoLapRoute_.offDeg.clear();
        autoLapRoute_.baseYawDeg = 0;
        autoLapRoute_.selectedGuideLine = -1;
        autoLapRoute_.startupYawAligned = false;
        if (!context.trackSystem) return;

        if (BuildAutoLapRouteFromPathGuide(context, referenceCarWorldPosition))
        {
            ReleaseAutoLapGuideLines();
            autoLapRoute_.built = true;
            autoLapRoute_.initialized = false;
            return;
        }

        ReleaseAutoLapGuideLines();
        BuildFallbackAutoLapRoute(context);
    }

    class SimulationTask final : public SRL::Types::ITask
    {
    public:
        void Configure(const SimulationPayload* input, SimulationPayload* output)
        {
            input_ = input;
            output_ = output;
        }

        uint16_t LastTicks() const { return lastTicks_; }

    private:
        void Do() override
        {
            if (!input_ || !output_) return;
            Sh2FrtProfiler::EnsureInitialized();
            const uint16_t startTicks = Sh2FrtProfiler::Now();
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
            lastTicks_ = Sh2FrtProfiler::Elapsed(startTicks, Sh2FrtProfiler::Now());
        }

        const SimulationPayload* input_ = nullptr;
        SimulationPayload* output_ = nullptr;
        volatile uint16_t lastTicks_ = 0;
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

    struct RealtimeFpsState
    {
        bool vblankValid = false;
        uint32_t lastVblank = 0u;
        uint8_t sampleFrames = 0u;
        uint16_t sampleVblanks = 0u;
        uint8_t framesOver30Budget = 0u;
        uint8_t framesOver60Budget = 0u;
    };

    struct SimulationRuntimeState
    {
        SimulationPayload input[2]{};
        SimulationPayload output[2]{};
        bool jobInFlight = false;
        bool hasCompleted = false;
        uint8_t writeIdx = 0;
        uint8_t inFlightIdx = 0;
        uint8_t completedIdx = 0;
        uint8_t slaveBackoffFrames = 0;
        uint32_t slaveDispatchCount = 0;
        uint32_t slaveDispatchSkipsTrackBusy = 0;
        uint32_t slaveDispatchSkipsBackoff = 0;
        uint32_t drainSoftTimeouts = 0;
        uint32_t drainHardWaits = 0;
        uint16_t masterWaitTicksThisFrame = 0;
        uint16_t slaveLastJobTicksThisFrame = 0;
    };

    struct CarPrepareRuntimeState
    {
        int32_t inputYaw[2]{};
        int32_t outputYaw[2]{};
        bool jobInFlight = false;
        bool hasCompleted = false;
        uint8_t writeIdx = 0;
        uint8_t inFlightIdx = 0;
        uint8_t completedIdx = 0;
    };

    struct ShadowDebugState
    {
        SRL::Math::Types::Vector3D worldPos{0.0, 0.0, 0.0};
        int32_t yawDeg = 0;
    };

    struct LowWorkOverlayState
    {
        uint32_t lastFreeBytes = 0;
        bool freeValid = false;
        TrackSystem::LowWorkCategoryBreakdown lastBreakdown{};
        bool breakdownValid = false;
        LowWorkTagGroupOverlay lastTagGroup{};
        bool tagGroupValid = false;
        uint32_t lastPayloadBytes = 0;
        uint32_t lastOverheadBytes = 0;
        uint32_t lastFreeBlocks = 0;
        bool allocatorValid = false;
    };

    struct AutoLapRouteState
    {
        bool initialized = false;
        bool built = false;
        bool startupYawAligned = false;
        uint16_t index = 0;
        TrackLowWorkVector<int16_t> ids{};
        TrackLowWorkVector<SRL::Math::Types::Vector3D> centers{};
        TrackLowWorkVector<int16_t> yawDeg{};
        TrackLowWorkVector<int16_t> offDeg{};
        int16_t baseYawDeg = 0;
        int16_t currentOffDeg = 0;
        int8_t selectedGuideLine = -1;
        std::array<TrackLowWorkVector<SRL::Math::Types::Vector3D>, 3> guideLines{};
    };

    struct CameraPathRuntimeState
    {
        SRL::Math::Types::Vector3D prevCarWorldPosition{
            SRL::Math::Types::Fxp::BuildRaw(0),
            SRL::Math::Types::Fxp::BuildRaw(0),
            SRL::Math::Types::Fxp::BuildRaw(0)};
        bool prevCarWorldPositionValid = false;
    };

    Context context_{};
    SRL::Input::Digital pad_{0};
    CameraRig::OrbitState orbitState_{};    
    int32_t carYawDeg_ = 0;
    uint32_t frameCounter_ = 0;
    RealtimeFpsState fpsState_{};
    SimulationTask simulationTask_{};
    SimulationRuntimeState simState_{};
    CarRenderPrepareTask carPrepareTask_{};
    CarPrepareRuntimeState carPrepareState_{};
    int16_t latestActiveSegmentId_ = -1;
    ShadowDebugState shadowDebug_{};
    int32_t cameraSlopeLiftRaw_ = 0;
    int16_t diagPrevCarSegmentId_ = -1;
    int16_t diagPrevWindowStartId_ = -1;
    uint32_t lastRenderedCarFacesThisFrame_ = 0u;
    uint32_t lastRenderedCarMeshesThisFrame_ = 0u;
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
    bool autoLapInputToggleEnabled_ = true;
    int16_t autoLapTargetSegmentId_ = 1;
    // PATH auto-lap speed multiplier test: 4x over baseline (6 -> 24).
    int16_t autoLapStepUnits_ = 12; // 2x do passo base (6)
    AutoLapRouteState autoLapRoute_{};
    CameraPathRuntimeState cameraPathRuntime_{};
    HwrStageTrace hwrStageTrace_{};
    uint16_t hwrTraceCooldownFrames_ = 0;
    LwrStageTrace lwrStageTrace_{};
    uint16_t lwrTraceCooldownFrames_ = 0;
    uint16_t lowWorkFreeOverlayCooldownFrames_ = 0;
    LowWorkOverlayState lowWorkOverlay_{};
    bool yHeldPrev_ = false;
    uint8_t carForwardOffsetRepeatFrames_ = 0u;
};

