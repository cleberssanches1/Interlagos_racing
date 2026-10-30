#pragma once

#ifndef MASTER_FRAME_PHASE_TELEMETRY
#define MASTER_FRAME_PHASE_TELEMETRY 0
#endif

#ifndef AUTO_LAP_PHYSICS_DRIVER
#define AUTO_LAP_PHYSICS_DRIVER 1
#endif

#ifndef AUTO_LAP_NPC_RAILS
#define AUTO_LAP_NPC_RAILS 0
#endif

// 1 = strip NPC runtime .text so HWR heap fits under 0x060C0000 (track belt).
// Set 0 only after map shows heapFree >= ~16KiB with heavy code linked.
#ifndef AUTO_LAP_NPC_SLIM
#define AUTO_LAP_NPC_SLIM 1
#endif

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#if MASTER_FRAME_PHASE_TELEMETRY
#include <limits>
#endif
#include <memory>
#include <utility>
#include <vector>

#include <srl.hpp>
#include <srl_slave.hpp>

#include "background_manager.hpp"
#include "application_state.hpp"
#include "auto_lap_route_build_ops.hpp"
#include "auto_lap_drive_policy.hpp"
#include "auto_lap_npc_rail_policy.hpp"
#include "auto_lap_speed_policy.hpp"
#include "auto_lap_route_selection_policy.hpp"
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
#include "auto_lap_camera_focus_policy.hpp"
#include "auto_lap_car_contact_policy.hpp"
#include "auto_lap_traffic_policy.hpp"
#include "npc_auto_lap_physical_driver.hpp"
#include "npc_rail_follower.hpp"
#include "npc_rail_route.hpp"
#endif
#include "auto_lap_route_transition_ops.hpp"
#include "auto_lap_route_runtime_state.hpp"
#include "auto_lap_route_lifecycle_ops.hpp"
#include "camera_path_runtime_state.hpp"
#include "camera_surface_guard.hpp"
#include "camera_system.hpp"
#include "car_physics_shared.hpp"
#include "car_render_state_assembler.hpp"
#include "car_prepare_runtime_state.hpp"
#include "car_system.hpp"
#include "cd_asset_transition_ops.hpp"
#if MASTER_FRAME_PHASE_TELEMETRY
#include "frame_phase_telemetry.hpp"
#endif
#include "frame_worker_tasks.hpp"
#include "frame_reuse_observability_capture_ops.hpp"
#include "game_loop_debug_ops.hpp"
#include "game_loop_debug_state.hpp"
#include "game_loop_car_shadow_runtime_assembler.hpp"
#include "game_loop_low_work_overlay_assembly_ops.hpp"
#include "game_loop_low_work_overlay_capture_ops.hpp"
#include "game_loop_memory_debug_packet_assembler.hpp"
#include "game_loop_memory_debug_presenter_ops.hpp"
#include "game_loop_low_work_overlay_presenter_ops.hpp"
#include "game_loop_memory_overlay_text_assembler.hpp"
#include "game_loop_memory_overlay_text_view_assembler.hpp"
#include "game_loop_memory_trace_ops.hpp"
#include "game_loop_memory_trace_runtime_debug_ops.hpp"
#include "game_loop_memory_trace_text_low_work_view_assembler.hpp"
#include "game_loop_memory_trace_text_view_assembler.hpp"
#include "game_loop_observability_contracts.hpp"
#include "game_loop_overlay_debug_presenter_ops.hpp"
#include "game_loop_overlay_runtime_assembler.hpp"
#include "game_loop_presentation_debug_assembler.hpp"
#include "game_loop_presentation_debug_presenter_ops.hpp"
#include "game_loop_presentation_ops.hpp"
#include "game_loop_presenter_boundary_text_driving_hud_bridge_assembler.hpp"
#include "game_loop_presenter_boundary_text_hud_presenter_ops.hpp"
#include "game_loop_reuse_observability_debug_bundle_presenter_ops.hpp"
#include "game_loop_simulation_reuse_runtime_state_ops.hpp"
#include "game_loop_track_reuse_runtime_state_ops.hpp"
#include "game_loop_track_render_presentation_observability_presenter_ops.hpp"
#include "game_loop_track_render_runtime_observability_ops.hpp"
#include "game_loop_runtime_state.hpp"
#include "game_loop_track_render_telemetry_view_assembler.hpp"
#include "hud_system.hpp"
#include "interfaces.hpp"
#include "memory_budget_transition_ops.hpp"
#include "memory_budget_runtime_bridge.hpp"
#include "path_nya_loader.hpp"
#include "physics_feature_flags.hpp"
#include "realtime_fps_runtime_state.hpp"
#include "render_pipeline.hpp"
#include "runtime_component_boundaries.hpp"
#include "sh2_frt_profiler.hpp"
#include "shadow_debug_state.hpp"
#include "simulation_scheduler_dispatch_assembler.hpp"
#include "simulation_scheduler_state.hpp"
#include "simulation_scheduler_transition_ops.hpp"
#include "simulation_scheduler_telemetry_assembler.hpp"
#include "track_render_transition_ops.hpp"
#include "track_system.hpp"
#include "time_attack_hud.hpp"

extern "C" uint32_t SRL_AppGetVblankCounter();

class GameLoopSystem
{
public:
    struct Context
    {
        enum : uint32_t
        {
            kEnableBgBit = 1u << 0,
            kRenderTrackBit = 1u << 1,
            kRenderCarBit = 1u << 2,
            kRenderAxesBit = 1u << 3,
            kTrackSystemReadyBit = 1u << 4,
            kVerboseFrameLogsBit = 1u << 5,
            kLogTrackBit = 1u << 6,
            kLogCarBit = 1u << 7,
            kEnableRuntimeStatsLogsBit = 1u << 8,
            kEnableMinimalFpsOverlayBit = 1u << 9,
            kEnableSlaveForCarPrepareBit = 1u << 10,
            kEnableSlaveForSimulationBit = 1u << 11,
            kSlaveSimulationLockstepBit = 1u << 12,
            kEnableManualGouraudCopyBit = 1u << 13,
            kAutoLapEnabledOnStartBit = 1u << 14,
            kAllowAutoLapInputToggleBit = 1u << 15,
            kRenderCarShadowModelBit = 1u << 16,
            kSbaLoadedBit = 1u << 17,
            kTimeAttackEnabledBit = 1u << 18
        };

        bool* cartOkFlag = nullptr;
        uint32_t flags =
            (kEnableBgBit |
             kRenderTrackBit |
             kRenderCarBit |
             kLogTrackBit |
             kEnableRuntimeStatsLogsBit |
             kEnableMinimalFpsOverlayBit |
             kSlaveSimulationLockstepBit |
             kAutoLapEnabledOnStartBit |
             kAllowAutoLapInputToggleBit);
        uint32_t faceCount = 0;
        uint32_t vertexCount = 0;
        SRL::Math::Types::Vector3D trackSegOffset{};
        SRL::Math::Types::Vector3D modelOffset{};
        SRL::Math::Types::Vector3D carWorldPosition{};
        // Fixed world light (re-applied every frame — slPutPolygonX can dirty SGL light state).
        SRL::Math::Types::Vector3D lightDirection{};
        SRL::Types::HighColor lightColor = SRL::Types::HighColor::FromRGB555(31, 31, 31);
        BackgroundManager* bgManager = nullptr;
        CameraSystem* cameraSystem = nullptr;
        TrackSystem* trackSystem = nullptr;
        std::unique_ptr<Game::CarSystem>* carSystem = nullptr;
        MeshRenderer* carShadowRenderer = nullptr;
        uint16_t sbaMeshCount = 0;
        uint16_t sbaFaceCount = 0;
        RenderPipeline* renderPipeline = nullptr;
        HudSystem* hudSystem = nullptr;
        Game::ITrackCollisionQuery* trackCollision = nullptr;
        Game::ICarPhysics* carPhysics = nullptr;
        Game::IGameplayTick* gameplayTick = nullptr;
        Game::IAudioEvents* audioEvents = nullptr;
        uint32_t* timeAttackRecordCourseTicks = nullptr;
        uint16_t timeAttackStartSegment = 13u;
        uint8_t timeAttackLaps = 3u;

        bool HasFlag(uint32_t bit) const { return (flags & bit) != 0u; }
        void SetFlag(uint32_t bit, bool enabled)
        {
            if (enabled) flags |= bit;
            else flags &= ~bit;
        }

        bool EnableBg() const { return HasFlag(kEnableBgBit); }
        void SetEnableBg(bool enabled) { SetFlag(kEnableBgBit, enabled); }
        bool RenderTrack() const { return HasFlag(kRenderTrackBit); }
        void SetRenderTrack(bool enabled) { SetFlag(kRenderTrackBit, enabled); }
        bool RenderCar() const { return HasFlag(kRenderCarBit); }
        void SetRenderCar(bool enabled) { SetFlag(kRenderCarBit, enabled); }
        bool RenderAxes() const { return HasFlag(kRenderAxesBit); }
        void SetRenderAxes(bool enabled) { SetFlag(kRenderAxesBit, enabled); }
        bool TrackSystemReady() const { return HasFlag(kTrackSystemReadyBit); }
        void SetTrackSystemReady(bool enabled) { SetFlag(kTrackSystemReadyBit, enabled); }
        bool VerboseFrameLogs() const { return HasFlag(kVerboseFrameLogsBit); }
        void SetVerboseFrameLogs(bool enabled) { SetFlag(kVerboseFrameLogsBit, enabled); }
        bool LogTrack() const { return HasFlag(kLogTrackBit); }
        void SetLogTrack(bool enabled) { SetFlag(kLogTrackBit, enabled); }
        bool LogCar() const { return HasFlag(kLogCarBit); }
        void SetLogCar(bool enabled) { SetFlag(kLogCarBit, enabled); }
        bool EnableRuntimeStatsLogs() const { return HasFlag(kEnableRuntimeStatsLogsBit); }
        void SetEnableRuntimeStatsLogs(bool enabled) { SetFlag(kEnableRuntimeStatsLogsBit, enabled); }
        bool EnableMinimalFpsOverlay() const { return HasFlag(kEnableMinimalFpsOverlayBit); }
        void SetEnableMinimalFpsOverlay(bool enabled) { SetFlag(kEnableMinimalFpsOverlayBit, enabled); }
        bool EnableSlaveForCarPrepare() const { return HasFlag(kEnableSlaveForCarPrepareBit); }
        void SetEnableSlaveForCarPrepare(bool enabled) { SetFlag(kEnableSlaveForCarPrepareBit, enabled); }
        bool EnableSlaveForSimulation() const { return HasFlag(kEnableSlaveForSimulationBit); }
        void SetEnableSlaveForSimulation(bool enabled) { SetFlag(kEnableSlaveForSimulationBit, enabled); }
        bool SlaveSimulationLockstep() const { return HasFlag(kSlaveSimulationLockstepBit); }
        void SetSlaveSimulationLockstep(bool enabled) { SetFlag(kSlaveSimulationLockstepBit, enabled); }
        bool EnableManualGouraudCopy() const { return HasFlag(kEnableManualGouraudCopyBit); }
        void SetEnableManualGouraudCopy(bool enabled) { SetFlag(kEnableManualGouraudCopyBit, enabled); }
        bool AutoLapEnabledOnStart() const { return HasFlag(kAutoLapEnabledOnStartBit); }
        void SetAutoLapEnabledOnStart(bool enabled) { SetFlag(kAutoLapEnabledOnStartBit, enabled); }
        bool AllowAutoLapInputToggle() const { return HasFlag(kAllowAutoLapInputToggleBit); }
        void SetAllowAutoLapInputToggle(bool enabled) { SetFlag(kAllowAutoLapInputToggleBit, enabled); }
        bool RenderCarShadowModel() const { return HasFlag(kRenderCarShadowModelBit); }
        void SetRenderCarShadowModel(bool enabled) { SetFlag(kRenderCarShadowModelBit, enabled); }
        bool SbaLoaded() const { return HasFlag(kSbaLoadedBit); }
        void SetSbaLoaded(bool enabled) { SetFlag(kSbaLoadedBit, enabled); }
        bool TimeAttackEnabled() const { return HasFlag(kTimeAttackEnabledBit); }
        void SetTimeAttackEnabled(bool enabled) { SetFlag(kTimeAttackEnabledBit, enabled); }
    };

    explicit GameLoopSystem(const Context& context)
        : context_(context)
    {
        SetAutoLapTestEnabled(context.AutoLapEnabledOnStart());
        SetAutoLapInputToggleEnabled(context.AllowAutoLapInputToggle());
    }

    static void SetWorkRamDebugTag(SRL::Memory::DebugTag tag)
    {
        SRL::Memory::HighWorkRam::SetDebugTag(tag);
        SRL::Memory::LowWorkRam::SetDebugTag(tag);
    }

    // Run the main frame loop with fixed subsystem ordering.
    int RunForever()
    {
#if MASTER_FRAME_PHASE_TELEMETRY
        Sh2FrtProfiler::EnsureInitialized();
#endif
        if (AutoLapPhysicsEnabled())
        {
            const uint8_t startFail = InitializeAutoLapPhysicalStart();
            if (startFail != AutoLapNpcRailPolicy::StartFail::kOk)
            {
                // Do not let the pilot accelerate from the segment bounds center
                // when PATH or a strict asphalt contact is unavailable.
                SetAutoLapTestEnabled(false);
                ClearAutoLapNpcRailRoutes();
                SRL::Debug::Print(1, 24, "AUTO START fail:%u",
                                  static_cast<unsigned>(startFail));
            }
            // NPC rails arm later via stability gate in the frame loop — never
            // in this start preface (log1/telemetrias_3 regressions).
        }
        if (context_.TimeAttackEnabled())
        {
            TimeAttack::Config config{};
            config.laps = context_.timeAttackLaps;
            config.startSegment = context_.timeAttackStartSegment;
            config.segmentCount = context_.trackSystem->SegmentCount();
            timeAttack_.Start(config, SRL_AppGetVblankCounter(),
                              context_.timeAttackRecordCourseTicks);
        }
        while (1)
        {
#if MASTER_FRAME_PHASE_TELEMETRY
            CommitPreviousMasterFrameTiming();
            masterFrameTiming_ = {};
            BeginMasterFramePhaseTelemetry();
#endif
            AppState::Set(AppState::Stage::LoopFrameBegin, frameCounter_);
            AppState::PresentOverlay(2);
            if (!ValidateFramePreconditions()) continue;
            ResetPerFrameStageTraces();
            ResetPerFrameSimulationTelemetry();
            ResetPerFrameTrackRenderHintCache();
            SetWorkRamDebugTag(SRL::Memory::DebugTag::Unknown);
            CaptureWorkRamStage(hwrStageTrace_.begin, lwrStageTrace_.begin, true);

#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp inputTicksStart = CaptureMasterPhaseStamp();
            masterFrameTiming_.beginTicks =
                Sh2FrtProfiler::Elapsed(masterFrameStartStamp_.frt, inputTicksStart.frt);
#endif
            const FrameInputState input = PollFrameInput();
            ConsumeCompletedJobs();
            // Resolve the just-published pair before dispatching the next
            // Slave job; otherwise an async job can suppress every contact.
            ResolveAutoLapCarContact();
            if (context_.TimeAttackEnabled()) timeAttack_.Tick(SRL_AppGetVblankCounter());
#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp inputTicksEnd = CaptureMasterPhaseStamp();
            masterFrameTiming_.inputTicks =
                Sh2FrtProfiler::Elapsed(inputTicksStart.frt, inputTicksEnd.frt);
            RecordMasterPhase(FramePhaseTelemetry::Phase::Input,
                              inputTicksStart,
                              inputTicksEnd);
#endif

#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp gameplayTicksStart = CaptureMasterPhaseStamp();
#endif
            SetWorkRamDebugTag(SRL::Memory::DebugTag::Gameplay);
            Game::GameplayFrameState frameState = BuildGameplayFrameState(input);
            ExecuteGameplayFrame(frameState);
            // Keep chase heading synced with gameplay/physics yaw every frame.
            SyncCameraHeadingFromCar();
            CaptureWorkRamStage(hwrStageTrace_.gameplay, lwrStageTrace_.gameplay);

            if (AutoLapLegacyEnabled())
            {
                SetWorkRamDebugTag(SRL::Memory::DebugTag::AutoLap);
                UpdateAutoLapRoute(context_, context_.carWorldPosition, carYawDeg_);
                SyncCameraHeadingFromCar();
            }
            TickAutoLapNpcRails();
            CaptureWorkRamStage(hwrStageTrace_.autoLap, lwrStageTrace_.autoLap);
#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp gameplayTicksEnd = CaptureMasterPhaseStamp();
            masterFrameTiming_.gameplayTicks =
                Sh2FrtProfiler::Elapsed(gameplayTicksStart.frt, gameplayTicksEnd.frt);
            RecordMasterPhase(FramePhaseTelemetry::Phase::Gameplay,
                              gameplayTicksStart,
                              gameplayTicksEnd);
#endif

#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp backgroundTicksStart = CaptureMasterPhaseStamp();
#endif
            SetWorkRamDebugTag(SRL::Memory::DebugTag::Background);
            ScheduleCarPrepareIfEnabled();
            UpdateBackground();
            // A Slave result may become ready while background work runs.  A
            // second non-blocking consume lets this frame's camera/render use
            // the freshest pose without turning async simulation into lockstep.
            ConsumeCompletedJobs();
            SyncCameraHeadingFromCar();
            CaptureWorkRamStage(hwrStageTrace_.background, lwrStageTrace_.background);
#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp backgroundTicksEnd = CaptureMasterPhaseStamp();
            masterFrameTiming_.backgroundTicks =
                Sh2FrtProfiler::Elapsed(backgroundTicksStart.frt, backgroundTicksEnd.frt);
            RecordMasterPhase(FramePhaseTelemetry::Phase::Background,
                              backgroundTicksStart,
                              backgroundTicksEnd);
#endif

#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp cameraTicksStart = CaptureMasterPhaseStamp();
#endif
            const CameraFrameState camera = ResolveCameraFrameState();
#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp cameraTicksEnd = CaptureMasterPhaseStamp();
            masterFrameTiming_.cameraTicks =
                Sh2FrtProfiler::Elapsed(cameraTicksStart.frt, cameraTicksEnd.frt);
            RecordMasterPhase(FramePhaseTelemetry::Phase::Camera,
                              cameraTicksStart,
                              cameraTicksEnd);
            CaptureCurrentPoseAge();
            const FramePhaseTelemetry::Stamp hudTicksStart = CaptureMasterPhaseStamp();
#endif
            SetWorkRamDebugTag(SRL::Memory::DebugTag::Hud);
            UpdateHud(camera);
            CaptureWorkRamStage(hwrStageTrace_.hud, lwrStageTrace_.hud);
#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp hudTicksEnd = CaptureMasterPhaseStamp();
            masterFrameTiming_.hudTicks =
                Sh2FrtProfiler::Elapsed(hudTicksStart.frt, hudTicksEnd.frt);
            RecordMasterPhase(FramePhaseTelemetry::Phase::Hud,
                              hudTicksStart,
                              hudTicksEnd);
#endif
#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp renderTicksStart = CaptureMasterPhaseStamp();
#endif
            RenderFrame(camera);
            RenderAxes();
#if MASTER_FRAME_PHASE_TELEMETRY
            const FramePhaseTelemetry::Stamp renderTicksEnd = CaptureMasterPhaseStamp();
            masterFrameTiming_.renderTicks =
                Sh2FrtProfiler::Elapsed(renderTicksStart.frt, renderTicksEnd.frt);
#endif

            FinishFrame();
        }
    }

private:
    enum : uint8_t
    {
        kAutoLapTestEnabledBit = 1u << 0,
        kAutoLapInputToggleEnabledBit = 1u << 1,
        kYHeldPrevBit = 1u << 2
    };

    bool HasStateFlag(uint8_t bit) const { return (stateFlags_ & bit) != 0u; }
    void SetStateFlag(uint8_t bit, bool enabled)
    {
        if (enabled) stateFlags_ |= bit;
        else stateFlags_ &= static_cast<uint8_t>(~bit);
    }
    bool AutoLapTestEnabled() const { return HasStateFlag(kAutoLapTestEnabledBit); }
    bool AutoLapPhysicsEnabled() const { return AutoLapTestEnabled() && (AUTO_LAP_PHYSICS_DRIVER != 0); }
    bool AutoLapLegacyEnabled() const { return AutoLapTestEnabled() && !AutoLapPhysicsEnabled(); }
    void SetAutoLapTestEnabled(bool enabled) { SetStateFlag(kAutoLapTestEnabledBit, enabled); }
    bool AutoLapInputToggleEnabled() const { return HasStateFlag(kAutoLapInputToggleEnabledBit); }
    void SetAutoLapInputToggleEnabled(bool enabled) { SetStateFlag(kAutoLapInputToggleEnabledBit, enabled); }
    bool YHeldPrev() const { return HasStateFlag(kYHeldPrevBit); }
    void SetYHeldPrev(bool enabled) { SetStateFlag(kYHeldPrevBit, enabled); }

    static constexpr int32_t kAutoLapYawBiasDeg = 0;

#if defined(SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY) && SRL_ENABLE_DETAILED_WORKRAM_TELEMETRY
    static constexpr bool kEnableDetailedWorkRamTelemetry = true;
#else
    static constexpr bool kEnableDetailedWorkRamTelemetry = false;
#endif
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

    using SegmentOverlaySnapshot = GameLoopRuntime::SegmentOverlaySnapshot;
    using OverlayDiagnosticsSnapshot = GameLoopRuntime::OverlayDiagnosticsSnapshot;
    using OverlayEventState = GameLoopRuntime::OverlayEventState;
    using Sh2SplitTelemetrySnapshot = GameLoopRuntime::Sh2SplitTelemetrySnapshot;
    using FramePresentationSnapshot = GameLoopRuntime::FramePresentationSnapshot;
    using RealtimeFpsMetricsSnapshot = GameLoopRuntime::RealtimeFpsMetricsSnapshot;
    using CameraPathRouteIndices = GameLoopRuntime::CameraPathRouteIndices;
    using FrameInputState = GameLoopRuntime::FrameInputState;
    using CameraFrameState = GameLoopRuntime::CameraFrameState;
    using HwrStageTrace = GameLoopRuntime::HwrStageTrace;
    using LwrStageTrace = GameLoopRuntime::LwrStageTrace;
    using LowWorkTagGroupOverlay = GameLoopRuntime::LowWorkTagGroupOverlay;
    using LowWorkOverlayState = GameLoopRuntime::LowWorkOverlayState;
    using DisabledLowWorkOverlayState = GameLoopRuntime::DisabledLowWorkOverlayState;

    using WorkRamTraceCooldownType = std::conditional_t<kEnableDetailedWorkRamTelemetry, uint16_t, uint8_t>;
    using LowWorkOverlayCooldownType = std::conditional_t<kEnableLowWorkFreeOverlay, uint16_t, uint8_t>;
    using LowWorkOverlayStorage = std::conditional_t<kEnableLowWorkFreeOverlay,
                                                     LowWorkOverlayState,
                                                     DisabledLowWorkOverlayState>;

    using SimulationPayload = Game::SimulationPayload;

    void CaptureWorkRamStage(HwrStageTrace::Snapshot& outHigh,
                             LwrStageTrace::Snapshot& outLow,
                             bool lowDetailed = false)
    {
        outHigh = GameLoopRuntime::MaybeCaptureHighWorkRamSnapshot<kEnableDetailedWorkRamTelemetry>();
        outLow = GameLoopRuntime::MaybeCaptureLowWorkRamSnapshot<kEnableDetailedWorkRamTelemetry>(lowDetailed);
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

    void ResetPerFrameTrackRenderHintCache()
    {
        trackProducerHintCached_ = false;
        trackProducerJobInFlightHint_ = false;
    }

#if MASTER_FRAME_PHASE_TELEMETRY
    struct MasterFrameTiming
    {
        uint16_t beginTicks = 0u;
        uint16_t inputTicks = 0u;
        uint16_t gameplayTicks = 0u;
        uint16_t backgroundTicks = 0u;
        uint16_t cameraTicks = 0u;
        uint16_t hudTicks = 0u;
        uint16_t trackStreamTicks = 0u;
        uint16_t trackDrawTicks = 0u;
        uint16_t trackFrameTicks = 0u;
        uint16_t trackRenderTicks = 0u;
        uint16_t trackEndTicks = 0u;
        uint16_t carTicks = 0u;
        uint16_t renderTicks = 0u;
        uint16_t finishPrepTicks = 0u;
        uint16_t syncTicks = 0u;
        uint16_t overlayTicks = 0u;
        uint16_t frameTicks = 0u;
        uint16_t unaccountedTicks = 0u;
        uint16_t frameVblanks = 0u;
        uint8_t poseAgeFrames = 0u;
        uint8_t culledSegments = 0u;
        bool valid = false;
    };

    struct MasterFrameTimingPeaks
    {
        uint16_t beginTicks = 0u;
        uint16_t inputTicks = 0u;
        uint16_t gameplayTicks = 0u;
        uint16_t backgroundTicks = 0u;
        uint16_t cameraTicks = 0u;
        uint16_t hudTicks = 0u;
        uint16_t trackStreamTicks = 0u;
        uint16_t trackDrawTicks = 0u;
        uint16_t trackRenderTicks = 0u;
        uint16_t trackEndTicks = 0u;
        uint16_t carTicks = 0u;
        uint16_t renderTicks = 0u;
        uint16_t finishPrepTicks = 0u;
        uint16_t syncTicks = 0u;
        uint16_t overlayTicks = 0u;
        uint16_t unaccountedTicks = 0u;
        uint8_t poseAgeFrames = 0u;
        uint8_t culledSegments = 0u;
    };

    struct LongFrameContext
    {
        int16_t segmentId = -1;
        uint8_t slides = 0u;
        uint8_t slidePrepares = 0u;
        uint8_t prefetchBuilds = 0u;
        uint8_t textureReuses = 0u;
        FramePhaseTelemetry::StallEvent stall{};
        bool valid = false;
    };

#ifdef SRL_MODE_NTSC
    static constexpr uint32_t kTelemetryRefreshHz = 60u;
#else
    static constexpr uint32_t kTelemetryRefreshHz = 50u;
#endif

    static constexpr uint16_t kLongStallThresholdVblanks =
        static_cast<uint16_t>((kTelemetryRefreshHz + 9u) / 10u);
    static constexpr uint16_t kFrtWrapRiskVblanks =
        FramePhaseTelemetry::FrtWrapRiskVblanks(kTelemetryRefreshHz);

    static FramePhaseTelemetry::Stamp CaptureMasterPhaseStamp()
    {
        FramePhaseTelemetry::Stamp stamp{};
        stamp.vblank = SRL_AppGetVblankCounter();
        stamp.frt = Sh2FrtProfiler::Now();
        return stamp;
    }

    void BeginMasterFramePhaseTelemetry()
    {
        FramePhaseTelemetry::BeginFrame(framePhaseTelemetry_, frameCounter_);
        masterFrameStartStamp_ = CaptureMasterPhaseStamp();
    }

    void RecordMasterPhase(FramePhaseTelemetry::Phase phase,
                           const FramePhaseTelemetry::Stamp& start,
                           const FramePhaseTelemetry::Stamp& end)
    {
        FramePhaseTelemetry::RecordSpan(framePhaseTelemetry_,
                                        phase,
                                        start,
                                        end,
                                        kLongStallThresholdVblanks);
    }

    static uint16_t SaturatingAddTicks(uint16_t lhs, uint16_t rhs)
    {
        const uint32_t sum = static_cast<uint32_t>(lhs) + rhs;
        return static_cast<uint16_t>(
            sum > static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())
                ? std::numeric_limits<uint16_t>::max()
                : sum);
    }

    void CommitPreviousMasterFrameTiming()
    {
        if (!masterFrameTiming_.valid) return;
        masterFrameTimingPeaks_.beginTicks = std::max(
            masterFrameTimingPeaks_.beginTicks, masterFrameTiming_.beginTicks);
        masterFrameTimingPeaks_.inputTicks = std::max(
            masterFrameTimingPeaks_.inputTicks, masterFrameTiming_.inputTicks);
        masterFrameTimingPeaks_.gameplayTicks = std::max(
            masterFrameTimingPeaks_.gameplayTicks, masterFrameTiming_.gameplayTicks);
        masterFrameTimingPeaks_.backgroundTicks = std::max(
            masterFrameTimingPeaks_.backgroundTicks, masterFrameTiming_.backgroundTicks);
        masterFrameTimingPeaks_.cameraTicks = std::max(
            masterFrameTimingPeaks_.cameraTicks, masterFrameTiming_.cameraTicks);
        masterFrameTimingPeaks_.hudTicks = std::max(
            masterFrameTimingPeaks_.hudTicks, masterFrameTiming_.hudTicks);
        masterFrameTimingPeaks_.trackStreamTicks = std::max(
            masterFrameTimingPeaks_.trackStreamTicks, masterFrameTiming_.trackStreamTicks);
        masterFrameTimingPeaks_.trackDrawTicks = std::max(
            masterFrameTimingPeaks_.trackDrawTicks, masterFrameTiming_.trackDrawTicks);
        masterFrameTimingPeaks_.trackRenderTicks = std::max(
            masterFrameTimingPeaks_.trackRenderTicks, masterFrameTiming_.trackRenderTicks);
        masterFrameTimingPeaks_.trackEndTicks = std::max(
            masterFrameTimingPeaks_.trackEndTicks, masterFrameTiming_.trackEndTicks);
        masterFrameTimingPeaks_.carTicks = std::max(
            masterFrameTimingPeaks_.carTicks, masterFrameTiming_.carTicks);
        masterFrameTimingPeaks_.renderTicks = std::max(
            masterFrameTimingPeaks_.renderTicks, masterFrameTiming_.renderTicks);
        masterFrameTimingPeaks_.finishPrepTicks = std::max(
            masterFrameTimingPeaks_.finishPrepTicks, masterFrameTiming_.finishPrepTicks);
        masterFrameTimingPeaks_.syncTicks = std::max(
            masterFrameTimingPeaks_.syncTicks, masterFrameTiming_.syncTicks);
        masterFrameTimingPeaks_.overlayTicks = std::max(
            masterFrameTimingPeaks_.overlayTicks, masterFrameTiming_.overlayTicks);
        masterFrameTimingPeaks_.unaccountedTicks = std::max(
            masterFrameTimingPeaks_.unaccountedTicks, masterFrameTiming_.unaccountedTicks);
        masterFrameTimingPeaks_.poseAgeFrames = std::max(
            masterFrameTimingPeaks_.poseAgeFrames, masterFrameTiming_.poseAgeFrames);
        masterFrameTimingPeaks_.culledSegments = std::max(
            masterFrameTimingPeaks_.culledSegments, masterFrameTiming_.culledSegments);
    }

    void FinalizeMasterFramePhaseTelemetry(const FramePhaseTelemetry::Stamp& frameEnd)
    {
        RecordMasterPhase(FramePhaseTelemetry::Phase::Frame,
                          masterFrameStartStamp_,
                          frameEnd);
        masterFrameTiming_.frameTicks =
            Sh2FrtProfiler::Elapsed(masterFrameStartStamp_.frt, frameEnd.frt);
        masterFrameTiming_.frameVblanks =
            FramePhaseTelemetry::ElapsedVblanks(masterFrameStartStamp_, frameEnd);

        if (masterFrameTiming_.frameVblanks >= kLongStallThresholdVblanks)
        {
            lastLongFrameContext_.segmentId = latestActiveSegmentId_;
            lastLongFrameContext_.slides = 0u;
            lastLongFrameContext_.slidePrepares = 0u;
            lastLongFrameContext_.prefetchBuilds = 0u;
            lastLongFrameContext_.textureReuses = 0u;
            if (context_.trackSystem)
            {
                lastLongFrameContext_.slides = context_.trackSystem->SlidesThisFrame();
                lastLongFrameContext_.slidePrepares =
                    context_.trackSystem->SlidePreparesThisFrame();
                lastLongFrameContext_.prefetchBuilds =
                    context_.trackSystem->PrefetchBuildAttemptsThisFrame();
                const TrackSystem::TextureRecycleHudSample recycle =
                    context_.trackSystem->TextureRecycleHudSampleThisFrame();
                const uint16_t reuses = static_cast<uint16_t>(recycle.reuse32 + recycle.reuse64);
                lastLongFrameContext_.textureReuses = static_cast<uint8_t>(
                    reuses > 99u ? 99u : reuses);
            }
            lastLongFrameContext_.stall = framePhaseTelemetry_.last;
            lastLongFrameContext_.valid = true;
        }

        const uint32_t knownTicks =
            static_cast<uint32_t>(masterFrameTiming_.beginTicks) +
            static_cast<uint32_t>(masterFrameTiming_.inputTicks) +
            static_cast<uint32_t>(masterFrameTiming_.gameplayTicks) +
            static_cast<uint32_t>(masterFrameTiming_.backgroundTicks) +
            static_cast<uint32_t>(masterFrameTiming_.cameraTicks) +
            static_cast<uint32_t>(masterFrameTiming_.hudTicks) +
            static_cast<uint32_t>(masterFrameTiming_.renderTicks) +
            static_cast<uint32_t>(masterFrameTiming_.finishPrepTicks) +
            static_cast<uint32_t>(masterFrameTiming_.syncTicks) +
            static_cast<uint32_t>(masterFrameTiming_.overlayTicks);
        masterFrameTiming_.unaccountedTicks =
            FramePhaseTelemetry::ComputeUnaccountedTicks(
                masterFrameTiming_.frameTicks,
                knownTicks,
                masterFrameTiming_.frameVblanks,
                kFrtWrapRiskVblanks);
        masterFrameTiming_.valid = true;
    }

    void CaptureCurrentPoseAge()
    {
        if (!lastCommittedSimulationFrameValid_ || frameCounter_ < lastCommittedSimulationFrameId_)
        {
            masterFrameTiming_.poseAgeFrames = 0u;
            return;
        }
        const uint32_t age = frameCounter_ - lastCommittedSimulationFrameId_;
        masterFrameTiming_.poseAgeFrames = static_cast<uint8_t>(age > 99u ? 99u : age);
    }
#endif

    template <bool tEnabled = kEnableLowWorkFreeOverlay,
              typename TOverlay = LowWorkOverlayStorage>
    void UpdateLowWorkFreeOverlayEnabled()
    {
#if TRACK_LWR_STAGE_TRACE
        // The detailed allocator overlay is intentionally off. Sample the
        // fixed stage counters independently at a low HUD cadence instead.
        static uint8_t stageTraceCooldown = 0u;
        if (stageTraceCooldown == 0u)
        {
            if (context_.trackSystem && context_.TrackSystemReady())
            {
                context_.trackSystem->PrintLwrStageProbes();
            }
            stageTraceCooldown = 60u;
        }
        else
        {
            --stageTraceCooldown;
        }
#endif
        if constexpr (!tEnabled)
        {
            return;
        }
        else
        {
            if constexpr (kEnableLowWorkFreeOverlayRequireRuntimeStats)
            {
                if (!context_.EnableRuntimeStatsLogs())
                {
                    return;
                }
            }

            if (lowWorkFreeOverlayCooldownFrames_ > 0u)
            {
                --lowWorkFreeOverlayCooldownFrames_;
                return;
            }
            lowWorkFreeOverlayCooldownFrames_ = kLowWorkFreeOverlayCadenceFrames;
            TOverlay& overlay = lowWorkOverlay_;

            GameLoopMemoryPresentationDomain::PresentCapturedLowWorkOverlayByMode(
                kEnableLowWorkFreeOverlayFull,
                overlay,
                context_.trackSystem,
                context_.TrackSystemReady());
        }
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

    // ---- Domain: component boundaries ------------------------------------

    Game::CarSystem* ActiveCarSystem() const
    {
        return (context_.carSystem && context_.carSystem->get()) ? context_.carSystem->get() : nullptr;
    }

    RuntimeBoundaries::FrameOrchestratorPorts BuildFrameOrchestratorPorts() const
    {
        RuntimeBoundaries::FrameOrchestratorPorts ports{};
        ports.background = context_.bgManager;
        ports.camera = context_.cameraSystem;
        ports.track = context_.trackSystem;
        ports.car = context_.carSystem;
        ports.renderPipeline = context_.renderPipeline;
        ports.hud = context_.hudSystem;
        return ports;
    }

    RuntimeBoundaries::SimulationPorts BuildSimulationPorts() const
    {
        RuntimeBoundaries::SimulationPorts ports{};
        ports.gameplayTick = context_.gameplayTick;
        ports.carPhysics = context_.carPhysics;
        ports.trackCollision = context_.trackCollision;
        return ports;
    }

    RuntimeBoundaries::AudioPorts BuildAudioPorts() const
    {
        RuntimeBoundaries::AudioPorts ports{};
        ports.audioEvents = context_.audioEvents;
        return ports;
    }

    RuntimeBoundaries::CarPorts BuildCarPorts() const
    {
        RuntimeBoundaries::CarPorts ports{};
        ports.car = context_.carSystem;
        return ports;
    }

    RuntimeBoundaries::TrackPorts BuildTrackPorts() const
    {
        RuntimeBoundaries::TrackPorts ports{};
        ports.track = context_.trackSystem;
        ports.trackCollision = context_.trackCollision;
        return ports;
    }

    RuntimeBoundaries::PresentationPorts BuildPresentationPorts() const
    {
        RuntimeBoundaries::PresentationPorts ports{};
        ports.background = context_.bgManager;
        ports.camera = context_.cameraSystem;
        ports.renderPipeline = context_.renderPipeline;
        ports.hud = context_.hudSystem;
        return ports;
    }

    // ---- Domain: frame gating and input ----------------------------------

    bool CanRenderCar() const
    {
        Game::CarSystem* car = ActiveCarSystem();
        return context_.RenderCar() && car && car->Valid();
    }

    int32_t CurrentCameraYawDeg() const
    {
        // Spectator focus changes only the view; gameplay remains on car 1.
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (cameraNpcFocus_ && NpcFocusTargetValid())
            return NormalizeYawDeg360(
                autoLapNpcDrivers_[0].StreamingYawDeg(autoLapNpcFollowers_[0]));
#endif
        return NormalizeYawDeg360(carYawDeg_);
    }

#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
    bool NpcFocusTargetValid() const
    {
        if (!AutoLapPhysicsEnabled() || !npcRailsArmed_ ||
            !autoLapNpcFollowers_[0].Active() ||
            !autoLapNpcDrivers_[0].Active() || !context_.trackSystem)
            return false;
        return autoLapNpcDrivers_[0].HasSafeAnchor();
    }

    bool NpcFocusCanSwitch() const
    {
        return NpcFocusTargetValid() &&
               autoLapNpcDrivers_[0].Frame().debugGroundSupport != 0u;
    }

    bool SegmentInRenderWindow(int32_t segmentId) const
    {
        if (!context_.trackSystem || segmentId <= 0) return false;
        int32_t windowStart = 0;
        int8_t windowDirection = 0;
        uint16_t windowCount = 0u;
        if (!context_.trackSystem->GetRenderWindowDebugSnapshot(
                windowStart, windowDirection, windowCount)) return false;
        for (size_t i = 0u; i < windowCount; ++i)
        {
            int32_t residentId = -1;
            if (context_.trackSystem->GetRenderWindowSegmentIdAt(i, residentId) &&
                residentId == segmentId) return true;
        }
        return false;
    }

    bool CanFocusAutoLapNpc() const
    {
        return AutoLapCameraFocusPolicy::MaySwitch(
            NpcFocusCanSwitch(),
            SegmentInRenderWindow(autoLapNpcDrivers_[0].StreamingSegmentId()));
    }

    bool CanFocusAutoLapPlayer() const
    {
        return AutoLapCameraFocusPolicy::MaySwitch(
            AutoLapPhysicsEnabled() && latestActiveSegmentId_ > 0,
            SegmentInRenderWindow(latestActiveSegmentId_));
    }

    void RequestAutoLapCameraFocus(bool npc)
    {
        const bool targetValid = npc
            ? NpcFocusCanSwitch()
            : (AutoLapPhysicsEnabled() && latestActiveSegmentId_ > 0);
        if (!targetValid || !context_.trackSystem) return;
        if (npc ? CanFocusAutoLapNpc() : CanFocusAutoLapPlayer())
        {
            SetCameraNpcFocus(npc);
            return;
        }
        cameraFocusTransferPending_ = true;
        cameraFocusTransferNpc_ = npc;
        cameraFocusTransferNeedsRelocate_ = true;
        context_.trackSystem->BeginFocusTransfer();
    }

    void AdvanceAutoLapCameraFocusTransfer()
    {
        if (!cameraFocusTransferPending_ || !context_.trackSystem) return;
        const bool targetValid = cameraFocusTransferNpc_
            ? NpcFocusTargetValid()
            : (AutoLapPhysicsEnabled() && latestActiveSegmentId_ > 0);
        if (!targetValid)
        {
            // If the requested NPC disappears mid-transfer, restore the
            // previous camera's window through the same incremental path.
            if (cameraFocusTransferNpc_ &&
                AutoLapPhysicsEnabled() && latestActiveSegmentId_ > 0)
            {
                cameraFocusTransferNpc_ = false;
                cameraFocusTransferNeedsRelocate_ = true;
                context_.trackSystem->BeginFocusTransfer();
                return;
            }
            context_.trackSystem->EndFocusTransfer(false);
            cameraFocusTransferPending_ = false;
            cameraFocusTransferNeedsRelocate_ = false;
            return;
        }
        const int32_t targetSegmentId = cameraFocusTransferNpc_
            ? autoLapNpcDrivers_[0].StreamingSegmentId()
            : latestActiveSegmentId_;
        if (!context_.trackSystem->FocusHandoffReadyForSegment(targetSegmentId)) return;
        context_.trackSystem->EndFocusTransfer(true);
        cameraFocusTransferPending_ = false;
        cameraFocusTransferNeedsRelocate_ = false;
        SetCameraNpcFocus(cameraFocusTransferNpc_);
    }

    void DrawAutoLapCameraFocusTransferCover()
    {
        if (!cameraFocusTransferPending_) return;
        // One opaque VDP1 command covers a remote-window rebuild and any
        // subsequent short catch-up if the destination car moved meanwhile.
        const int16_t halfWidth = static_cast<int16_t>(SRL::TV::Width / 2u);
        const int16_t halfHeight = static_cast<int16_t>(SRL::TV::Height / 2u);
        SPRITE cover{};
        cover.CTRL = FUNC_Polygon;
        cover.PMOD = 0x0080 | ((CL32KRGB & 7) << 3);
        cover.COLR = SRL::Types::HighColor::Colors::Black;
        cover.XA = -halfWidth; cover.YA = -halfHeight;
        cover.XB = halfWidth;  cover.YB = -halfHeight;
        cover.XC = halfWidth;  cover.YC = halfHeight;
        cover.XD = -halfWidth; cover.YD = halfHeight;
        // Z=0 is outside SGL's sprite display range and silently hid the
        // previous cover. A positive near Z puts this above track polygons.
        (void)slSetSprite(&cover, 1 << 16);
        SRL::Debug::Print(10, 13, "CAMERA %u CARREGANDO", cameraFocusTransferNpc_ ? 2u : 1u);
    }

    void SetCameraNpcFocus(bool enabled)
    {
        if (cameraNpcFocus_ == enabled) return;
        cameraNpcFocus_ = enabled;
        if (context_.trackSystem) context_.trackSystem->RequestFocusHandoff();
        if (context_.cameraSystem) context_.cameraSystem->ResetFollowHistory();
        CameraSurfaceGuard::Reset(cameraSurfaceGuardState_);
        cameraPathRuntime_.SetPrevCarWorldPositionValid(false);
    }
#endif

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

    // Assemble pad state and debug toggles before gameplay/simulation.
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

#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        AdvanceAutoLapCameraFocusTransfer();
        if (AutoLapPhysicsEnabled() && xHeld && !cameraXHeldPrev_ && !zHeld)
        {
            if (!cameraFocusTransferPending_ && cameraNpcFocus_)
            {
                RequestAutoLapCameraFocus(false);
            }
            else if (!cameraFocusTransferPending_)
            {
                RequestAutoLapCameraFocus(true);
            }
        }
        if (cameraNpcFocus_ && !NpcFocusTargetValid()) SetCameraNpcFocus(false);
        cameraXHeldPrev_ = xHeld;
#endif

        int32_t yawForCamera = carYawDeg_;
        const bool allowManualYawInput =
            !AutoLapTestEnabled() && (context_.carPhysics == nullptr);
        context_.cameraSystem->UpdateFromPad(
            pad_, yawForCamera, orbitState_, allowManualYawInput,
            !AutoLapPhysicsEnabled());
        if (allowManualYawInput)
        {
            carYawDeg_ = yawForCamera;
        }

        if (Game::CarSystem* car = ActiveCarSystem())
        {
            if (xHeld && !AutoLapTestEnabled())
            {
                if (carForwardOffsetRepeatFrames_ > 0) --carForwardOffsetRepeatFrames_;
                if (carForwardOffsetRepeatFrames_ == 0)
                {
                    int32_t nextOffsetDeg = car->VisualYawOffsetDegrees();
                    bool changed = false;
                    if (input.lHeld) { nextOffsetDeg -= 5; changed = true; }
                    if (input.rHeld) { nextOffsetDeg += 5; changed = true; }
                    if (changed)
                    {
                        car->SetVisualYawOffsetDegrees(nextOffsetDeg);
                        carForwardOffsetRepeatFrames_ = 2;
                    }
                }
                SRL::Debug::Print(1, 27, "CAR FWD off:%d    ", car->VisualYawOffsetDegrees());
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
        if (input.yHeld && !YHeldPrev())
        {
            // Simulation owns the single slSlaveFunc. A track producer job would replace it.
            const bool toggleTrackSlave =
                zHeld && context_.trackSystem && !context_.EnableSlaveForSimulation();
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
                if (AutoLapInputToggleEnabled())
                {
                    SetAutoLapTestEnabled(!AutoLapTestEnabled());
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
                    SetCameraNpcFocus(false);
#endif
                    autoLapDriveState_ = {};
                    AutoLapRouteDomain::ClearAutoLapStartupYawAlignment(autoLapRoute_);
                    ClearAutoLapNpcRailRoutes();
                    if (AutoLapTestEnabled())
                    {
                        // Camera guidance may have cached a different line.
                        // Rebuild so the pilot selects the middle PATH line.
                        AutoLapRouteDomain::ResetAutoLapPendingBuild(autoLapRoute_);
                    }
                    else
                    {
                        autoLapRoute_.currentOffDeg = 0;
                        cameraPathRuntime_.SetPrevCarWorldPositionValid(false);
                        if (!CameraSystem::kPathGuidedChaseEnabled)
                        {
                            AutoLapRouteDomain::ReleaseAutoLapRouteStorage(autoLapRoute_);
                        }
                    }
                    SRL::Debug::Print(1, 23, "CAR MOVE:%u", AutoLapTestEnabled() ? 1u : 0u);
                }
            }
        }
        SetYHeldPrev(input.yHeld);
        return input;
    }

    // ---- Domain: simulation orchestration --------------------------------

    void ApplyResolvedFrameState(const Game::GameplayFrameState& frameState,
                                 const SRL::Math::Types::Vector3D& worldPosition,
                                 int32_t yawDeg)
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        npcPublishedSimFrameId_ = frameState.frameId;
        npcPublishedSimFrameValid_ = true;
#endif
#if MASTER_FRAME_PHASE_TELEMETRY
        lastCommittedSimulationFrameId_ = frameState.frameId;
        lastCommittedSimulationFrameValid_ = true;
#endif
        context_.carWorldPosition = worldPosition;
        carYawDeg_ = yawDeg;
        SyncCameraHeadingFromCar();
        latestActiveSegmentId_ = frameState.activeSegmentId;
        // autolap48: keep a live belt anchor while GS flickers to 0.
        if (frameState.debugGroundSupport != 0u && frameState.activeSegmentId > 0)
        {
            playerLastValidSegmentId_ = frameState.activeSegmentId;
            playerSafePose_ = worldPosition;
            playerSafeYawDeg_ = static_cast<int16_t>(yawDeg);
            playerSafePoseValid_ = true;
        }
        if (context_.TimeAttackEnabled())
            timeAttack_.Observe(frameState.frameId, frameState.activeSegmentId,
                                SRL_AppGetVblankCounter());
        if (Game::CarSystem* car = ActiveCarSystem())
        {
            car->ApplySimulationFrameState(frameState);
        }
        if (context_.audioEvents)
        {
            // Keep PCM/SGL sound driver calls on the Master SH2.
            context_.audioEvents->OnFrame(frameState);
        }
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (playerRecoveryAwaitingPublish_ &&
            frameState.frameId == playerRecoveryFrameId_)
        {
            // The respawn is authoritative only after its simulation result
            // arrives. A distant destination cannot be reached by the normal
            // eight-segment target-jump guard, so reuse the focus handoff.
            playerRecoveryAwaitingPublish_ = false;
            if (AutoLapCameraFocusPolicy::NeedsRecoveryHandoff(
                    true, !cameraNpcFocus_, cameraFocusTransferPending_,
                    latestActiveSegmentId_,
                    SegmentInRenderWindow(latestActiveSegmentId_)))
                RequestAutoLapCameraFocus(false);
        }
#endif
    }

    bool IsTrackProducerJobInFlightHint()
    {
        if (trackProducerHintCached_)
        {
            return trackProducerJobInFlightHint_;
        }

        GameLoopRuntime::TrackRenderProducerHintPacket producerHint{};
        if (!GameLoopRuntime::TryBuildTrackRenderProducerHintPacket(context_.trackSystem,
                                                                    context_.TrackSystemReady(),
                                                                    context_.RenderTrack(),
                                                                    producerHint))
        {
            trackProducerHintCached_ = true;
            trackProducerJobInFlightHint_ = false;
            return false;
        }
        trackProducerHintCached_ = true;
        trackProducerJobInFlightHint_ = producerHint.producerJobInFlight;
        return trackProducerJobInFlightHint_;
    }

    void BackoffSimulationSlaveDispatch()
    {
        simState_.slaveBackoffFrames = std::max<uint8_t>(simState_.slaveBackoffFrames, kSimSlaveBackoffFrames);
    }

    // Ensure SimulationTask is fully drained before entering the track render
    // window that may submit SlaveTrackDrawProducer jobs.
    bool DrainSimulationJobIfInFlight(bool mandatoryWait)
    {
        if (!simState_.JobInFlight()) return true;

        Game::SimulationSchedulerPolicy drainPolicy{};
        drainPolicy.softSpinLimit = kSimDrainSoftSpinLimit;
        drainPolicy.hardSpinLimit = kSimDrainHardSpinLimit;
        SimulationSchedulerDomain::SimulationDrainPacket drainPacket{};
        SimulationSchedulerDomain::SeedSimulationDrainPacket(drainPolicy,
                                                             mandatoryWait,
                                                             drainPacket);

        if (!simulationTask_.IsDone())
        {
            if (!drainPacket.mandatoryWait) return false;

            Sh2FrtProfiler::EnsureInitialized();
            const uint16_t waitStartTicks = Sh2FrtProfiler::Now();
            uint32_t spins = 0;
            while (!simulationTask_.IsDone() && spins < drainPacket.softSpinLimit)
            {
                ++spins;
            }

            if (!simulationTask_.IsDone())
            {
                ++simState_.drainSoftTimeouts;
                BackoffSimulationSlaveDispatch();
                while (!simulationTask_.IsDone() && spins < drainPacket.hardSpinLimit)
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
        }

        SimulationSchedulerDomain::MarkSimulationCompleted(simState_, simState_.inFlightIdx);
        // Slave wrote the payload through the uncached mirror. Refresh the
        // cached copy before the Master reads the pose, pitch and yaw.
        {
            Game::SimulationPayload* const slot = &simState_.output[simState_.inFlightIdx];
            *slot = *Game::Sh2CacheThrough(slot);
        }
        simState_.slaveLastJobTicksThisFrame = simulationTask_.LastTicks();
        const SimulationPayload& authoritativeOutput =
            GameLoopRuntime::CommitCompletedSimulationReuseAuthoritativeOutput(
                simState_,
                simulationReuseState_);
        ApplyResolvedFrameState(authoritativeOutput.frameState,
                                authoritativeOutput.frameState.carWorldPosition,
                                authoritativeOutput.frameState.carYawDeg);
        return true;
    }

    void ConsumeCompletedJobs()
    {
        SimulationSchedulerDomain::SimulationCompletionPacket completionPacket{};
        SimulationSchedulerDomain::SeedSimulationCompletionPacket(simState_, completionPacket);

        if (completionPacket.jobInFlight)
        {
            if (simulationTask_.IsDone())
            {
                (void)DrainSimulationJobIfInFlight(false);
            }
        }
        else if (completionPacket.hasCompleted)
        {
            if (const Game::SimulationPayload* authoritativeOutput =
                    GameLoopRuntime::TryCommitCompletedSimulationReuseAuthoritativeOutput(
                        simState_,
                        completionPacket,
                        simulationReuseState_))
            {
                ApplyResolvedFrameState(authoritativeOutput->frameState,
                                        authoritativeOutput->frameState.carWorldPosition,
                                        authoritativeOutput->frameState.carYawDeg);
            }
        }
        if (carPrepareState_.JobInFlight() && carPrepareTask_.IsDone())
        {
            SimulationSchedulerDomain::MarkPrepareCompleted(carPrepareState_,
                                                            carPrepareState_.inFlightIdx);
        }
    }

    uint8_t InitializeAutoLapPhysicalStart()
    {
        namespace StartFail = AutoLapNpcRailPolicy::StartFail;

        Game::CarSystem* car = ActiveCarSystem();
        if (!car || !context_.trackSystem || !context_.TrackSystemReady())
        {
            return StartFail::kCarOrTrack;
        }
        if (!EnsureAutoLapRouteReady(context_, context_.carWorldPosition))
        {
            // Built/count help separate "PATH missing" from "centers wiped".
            SRL::Debug::Print(1, 23, "AUTO ENSURE b:%u n:%u",
                              autoLapRoute_.Built() ? 1u : 0u,
                              static_cast<unsigned>(autoLapRoute_.centers.size()));
            return StartFail::kRouteReady;
        }
        if (autoLapRoute_.selectedGuideLine < 0 ||
            autoLapRoute_.centers.size() < 3u ||
            !AutoLapRouteDomain::HasConsistentMappedRoute(autoLapRoute_) ||
            !AutoLapRouteDomain::HasConsistentYawRoute(autoLapRoute_))
        {
            return StartFail::kRouteData;
        }

        size_t nearest = 0u;
        if (!AutoLapRouteDomain::FindNearestRoutePointIndex(
                autoLapRoute_, context_.carWorldPosition, nearest))
        {
            return StartFail::kNearestPoint;
        }

        const auto& routePoint = autoLapRoute_.centers[nearest];
        static constexpr uint8_t kAsphaltSurfaceType = 1u;
        SRL::Math::Types::Fxp groundY{};
        int32_t groundSegmentId = -1;
        const int32_t routeSegmentId = static_cast<int32_t>(autoLapRoute_.ids[nearest]);
        if (!context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                routePoint, context_.trackSegOffset, &kAsphaltSurfaceType, 1u,
                groundY, &groundSegmentId, routeSegmentId, false))
        {
            return StartFail::kAsphaltTcol;
        }

        // Place the car on the selected PATH line before its first physics
        // frame. TCOL supplies Y; the guide supplies only XZ and heading.
        context_.carWorldPosition = routePoint;
        context_.carWorldPosition.Y = groundY + Game::CarPhysics::GetRideHeightOffset();
        carYawDeg_ = NormalizeYawDeg360(autoLapRoute_.yawDeg[nearest]);
        autoLapRoute_.index = static_cast<uint16_t>(nearest);
        AutoLapRouteDomain::MarkAutoLapInitialized(autoLapRoute_);
        autoLapDriveState_ = {};
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        playerStallState_ = {};
        playerRecoveryRetryCountdown_ = 0u;
        playerRecoveryAwaitingPublish_ = false;
#endif
        if (groundSegmentId > 0)
            latestActiveSegmentId_ = static_cast<int16_t>(groundSegmentId);
        car->SyncRenderState(context_.carWorldPosition, carYawDeg_);
        SyncCameraHeadingFromCar();
        cameraPathRuntime_.SetPrevCarWorldPositionValid(false);
        return StartFail::kOk;
    }

#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
    bool ProbeAutoLapPassPoint(AutoLapDrivePolicy::Point point,
                               SRL::Math::Types::Fxp approximateY,
                               int32_t forwardXRaw, int32_t forwardZRaw) const
    {
        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector3D;
        static constexpr uint8_t kAsphalt = 1u;
        Vector3D sample(Fxp::BuildRaw(point.xRaw), approximateY,
                        Fxp::BuildRaw(point.zRaw));
        Fxp surfaceY{};
        int32_t segmentId = -1;
        if (!context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                sample, context_.trackSegOffset, &kAsphalt, 1u,
                surfaceY, &segmentId, latestActiveSegmentId_, false)) return false;
        sample.Y = surfaceY + Game::CarPhysics::GetRideHeightOffset();
        const Vector3D forward(Fxp::BuildRaw(forwardXRaw), Fxp::BuildRaw(0),
                               Fxp::BuildRaw(forwardZRaw));
        Vector3D wallPush{};
        if (context_.trackSystem->FindPlanarWallPush(
                sample, context_.trackSegOffset, forward,
                Fxp::BuildRaw(30 << 16), wallPush, nullptr, segmentId,
                false, false, &sample) &&
            (wallPush.X.RawValue() != 0 || wallPush.Z.RawValue() != 0)) return false;
        return true;
    }

    bool TryAutoLapPassSide(const AutoLapDrivePolicy::Point& routeTarget,
                            const AutoLapTrafficPolicy::Vehicle& other,
                            SRL::Math::Types::Fxp otherY,
                            int32_t routeForwardXRaw,
                            int32_t routeForwardZRaw,
                            int8_t side) const
    {
        using AutoLapTrafficPolicy::OffsetTarget;
        const auto entry = OffsetTarget(routeTarget,
            routeForwardXRaw, routeForwardZRaw, side);
        const auto beside = OffsetTarget({other.xRaw, other.zRaw},
            routeForwardXRaw, routeForwardZRaw, side);
        AutoLapDrivePolicy::Point exit = beside;
        exit.xRaw += static_cast<int32_t>(
            (static_cast<int64_t>(routeForwardXRaw) * (100 << 16)) >> 16);
        exit.zRaw += static_cast<int32_t>(
            (static_cast<int64_t>(routeForwardZRaw) * (100 << 16)) >> 16);
        return ProbeAutoLapPassPoint(entry, context_.carWorldPosition.Y,
                                    routeForwardXRaw, routeForwardZRaw) &&
               ProbeAutoLapPassPoint(beside, otherY,
                                    routeForwardXRaw, routeForwardZRaw) &&
               ProbeAutoLapPassPoint(exit, otherY,
                                    routeForwardXRaw, routeForwardZRaw);
    }
#endif

    void BuildAutoLapDriveInput(Game::CarSystem& car,
                                Game::CarSystem::GameplayInputSnapshot& outInput)
    {
        if (!context_.trackSystem || !context_.TrackSystemReady() ||
            !EnsureAutoLapRouteReady(context_, context_.carWorldPosition) ||
            autoLapRoute_.selectedGuideLine < 0 || autoLapRoute_.centers.size() < 3u)
        {
            // An unaudited segment-center fallback must not drive the car.
            // Releasing the controls leaves collision/ground ownership intact.
            outInput.SetAnalogSteering(0);
            return;
        }

        if (!autoLapRoute_.Initialized())
        {
            size_t nearest = 0u;
            if (!AutoLapRouteDomain::FindNearestRoutePointIndex(
                    autoLapRoute_, context_.carWorldPosition, nearest)) return;
            autoLapRoute_.index = static_cast<uint16_t>(nearest);
            AutoLapRouteDomain::MarkAutoLapInitialized(autoLapRoute_);
        }

        const size_t count = autoLapRoute_.centers.size();
        size_t current = static_cast<size_t>(autoLapRoute_.index);
        // Keep progress local to the published car pose. A stopped car cannot
        // move the stream by advancing a route index independently of physics.
        for (size_t guard = 0u; guard < 3u; ++guard)
        {
            const size_t next = (current + 1u) % count;
            if (!AutoLapRouteDomain::HasReachedOrPassedWaypoint(
                    autoLapRoute_, current, next, context_.carWorldPosition)) break;
            current = next;
        }
        autoLapRoute_.index = static_cast<uint16_t>(current);
        const size_t next = (current + 1u) % count;
        const size_t afterNext = (next + 1u) % count;
        const auto& routeCurrent = autoLapRoute_.centers[current];
        const auto& routeNext = autoLapRoute_.centers[next];
        const auto& routeAfterNext = autoLapRoute_.centers[afterNext];
        const auto yawAngle = SRL::Math::Types::Angle::FromDegrees(
            SRL::Math::Types::Fxp::BuildRaw(NormalizeYawDeg360(carYawDeg_) << 16));
        const auto forwardX = SRL::Math::Trigonometry::Sin(yawAngle);
        const auto forwardZ = SRL::Math::Trigonometry::Cos(yawAngle);
        const auto& carDebug = car.RuntimeDebug();

        AutoLapDrivePolicy::Input drive{};
        drive.car = {context_.carWorldPosition.X.RawValue(),
                     context_.carWorldPosition.Z.RawValue()};
        drive.current = {routeCurrent.X.RawValue(), routeCurrent.Z.RawValue()};
        drive.next = {routeNext.X.RawValue(), routeNext.Z.RawValue()};
        drive.afterNext = {routeAfterNext.X.RawValue(), routeAfterNext.Z.RawValue()};
        drive.forwardXRaw = forwardX.RawValue();
        drive.forwardZRaw = -forwardZ.RawValue();
        drive.speedKmh = carDebug.speedKmh;
        drive.plannedSpeedKmh = AutoLapSpeedPolicy::RouteSpeed(
            autoLapRoute_, current, drive.car.xRaw, drive.car.zRaw);
        AutoLapSpeedPolicy::RouteLookahead(
            autoLapRoute_, current, drive.car.xRaw, drive.car.zRaw,
            drive.speedKmh, drive.routeTarget.xRaw, drive.routeTarget.zRaw);
        drive.hasRouteTarget = true;
        drive.upcomingTurnDeg = static_cast<int16_t>(std::max(
            std::abs(ShortestDeltaDeg(autoLapRoute_.yawDeg[current], autoLapRoute_.yawDeg[next])),
            std::abs(ShortestDeltaDeg(autoLapRoute_.yawDeg[next], autoLapRoute_.yawDeg[afterNext]))));
        drive.surfaceType = carDebug.groundSurfaceType;
        drive.groundSupported = carDebug.GroundSupport();
        drive.wallHit = carDebug.WallHit();
        // autolap48: plant BEFORE AutoLapDrivePolicy::Step so missingGround
        // never latches the >8 accel cut (policy clears only when GS=true).
        // autolap50: skip while hard-recover is armed — soft+hard fought and looped.
        bool playerPlantAssist = false;
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        const bool hardRecoverArmed =
            playerRecoveryAwaitingPublish_ || playerRecoveryRetryCountdown_ > 0u;
#else
        const bool hardRecoverArmed = false;
#endif
        // G2: near mid-rail XZ — brief GS flicker should not cut accel.
        bool playerFlickerGrace = false;
        if (!drive.groundSupported &&
            AutoLapPhysicsEnabled() &&
            autoLapRoute_.Built() &&
            !autoLapRoute_.centers.empty() &&
            autoLapDriveState_.missingGroundFrames < 16u)
        {
            const size_t count = autoLapRoute_.centers.size();
            const size_t idx =
                static_cast<size_t>(autoLapRoute_.index) % count;
            const auto& pt = autoLapRoute_.centers[idx];
            const int32_t dx = drive.car.xRaw - pt.X.RawValue();
            const int32_t dz = drive.car.zRaw - pt.Z.RawValue();
            const int32_t adx = dx < 0 ? -dx : dx;
            const int32_t adz = dz < 0 ? -dz : dz;
            constexpr int32_t kNearRailLim = 96 << 16;
            if (adx < kNearRailLim && adz < kNearRailLim)
            {
                playerFlickerGrace = true;
                drive.groundSupported = true;
            }
        }
        if (!drive.groundSupported &&
            autoLapDriveState_.missingGroundFrames >= 4u &&
            !hardRecoverArmed)
        {
            playerPlantAssist = TryPlayerSoftPlantAsphalt(car);
            if (playerPlantAssist)
            {
                drive.groundSupported = true;
                autoLapDriveState_.missingGroundFrames = 0u;
            }
        }
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        AutoLapTrafficPolicy::Vehicle ego{};
        AutoLapTrafficPolicy::Vehicle other{};
        bool haveNpc = npcRailsArmed_ && autoLapNpcFollowers_[0].Active();
        bool passing = false;
        if (haveNpc)
        {
            const auto& npc = autoLapNpcFollowers_[0];
            ego = {drive.car.xRaw, drive.car.zRaw,
                   drive.forwardXRaw, drive.forwardZRaw, drive.speedKmh};
            const auto npcYaw = SRL::Math::Types::Angle::FromDegrees(
                SRL::Math::Types::Fxp::BuildRaw(
                    static_cast<int32_t>(npc.yawDeg) << 16));
            other = {npc.pos.X.RawValue(), npc.pos.Z.RawValue(),
                     SRL::Math::Trigonometry::Sin(npcYaw).RawValue(),
                     -SRL::Math::Trigonometry::Cos(npcYaw).RawValue(),
                     autoLapNpcDrivers_[0].Frame().carSpeedKmh};
            const int64_t pathDx = static_cast<int64_t>(drive.next.xRaw) - drive.current.xRaw;
            const int64_t pathDz = static_cast<int64_t>(drive.next.zRaw) - drive.current.zRaw;
            const int64_t absDx = pathDx < 0 ? -pathDx : pathDx;
            const int64_t absDz = pathDz < 0 ? -pathDz : pathDz;
            const int64_t pathLength = absDx > absDz ? absDx : absDz;
            if (pathLength >= (1 << 16))
            {
                const int32_t pathFx = static_cast<int32_t>((pathDx << 16) / pathLength);
                const int32_t pathFz = static_cast<int32_t>((pathDz << 16) / pathLength);
                const auto routeTarget = drive.routeTarget;
                if (playerPassState_.cooldown > 0u) --playerPassState_.cooldown;
                if (playerPassState_.side != 0 &&
                    (AutoLapTrafficPolicy::PassedObstacle(ego, other) ||
                     ++playerPassState_.activeFrames > 180u))
                {
                    playerPassState_ = {};
                    playerPassState_.cooldown = 30u;
                }
                if (playerPassState_.side != 0 &&
                    playerPassState_.checkCountdown == 0u &&
                    !TryAutoLapPassSide(routeTarget, other, npc.pos.Y,
                                        pathFx, pathFz, playerPassState_.side))
                {
                    playerPassState_ = {};
                    playerPassState_.cooldown = 30u;
                }
                if (playerPassState_.side == 0 && playerPassState_.cooldown == 0u &&
                    AutoLapTrafficPolicy::StoppedObstacleAhead(ego, other))
                {
                    const int8_t preferred = AutoLapTrafficPolicy::PreferredPassSide(ego, other);
                    if (TryAutoLapPassSide(routeTarget, other, npc.pos.Y,
                                           pathFx, pathFz, preferred))
                        playerPassState_.side = preferred;
                    else if (TryAutoLapPassSide(routeTarget, other, npc.pos.Y,
                                                pathFx, pathFz, -preferred))
                        playerPassState_.side = -preferred;
                    if (playerPassState_.side != 0)
                        playerPassState_.checkCountdown = 8u;
                    else
                        playerPassState_.cooldown = 24u;
                }
                if (playerPassState_.side != 0)
                {
                    drive.hasOverrideTarget = true;
                    drive.overrideTarget = AutoLapTrafficPolicy::OffsetTarget(
                        routeTarget, pathFx, pathFz, playerPassState_.side);
                    passing = true;
                    if (playerPassState_.checkCountdown > 0u)
                        --playerPassState_.checkCountdown;
                }
            }
        }
        else playerPassState_ = {};
#endif
        auto command =
            AutoLapDrivePolicy::Step(drive, autoLapDriveState_);
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (haveNpc && !passing)
        {
            const int16_t limit = AutoLapTrafficPolicy::LimitSpeed(
                ego, other, command.targetSpeedKmh, playerTrafficState_);
            AutoLapTrafficPolicy::ApplySpeedLimit(command, drive.speedKmh, limit);
        }
        else if (passing)
        {
            playerTrafficState_ = {};
            const auto relative = AutoLapTrafficPolicy::RelativeTo(ego, other);
            const int32_t sideRaw = relative.sideRaw < 0 ? -relative.sideRaw : relative.sideRaw;
            AutoLapTrafficPolicy::ApplySpeedLimit(
                command, drive.speedKmh, sideRaw < (75 << 16) ? 18 : 42);
        }
        if (playerContactRecoveryFrames_ > 0u)
        {
            --playerContactRecoveryFrames_;
            AutoLapTrafficPolicy::ApplySpeedLimit(
                command, drive.speedKmh, playerContactSpeedLimitKmh_);
        }
#endif
        // Extra grace vs policy's >8 cut; plant/flicker already cleared the latch.
        if (autoLapDriveState_.missingGroundFrames > 16u &&
            !playerPlantAssist && !playerFlickerGrace)
            command.accelerate = false;
        if (drive.wallHit && drive.speedKmh < 8)
        {
            command.accelerate = false;
            command.brake = false;
        }
        if ((playerPlantAssist || playerFlickerGrace) &&
            !command.accelerate && drive.speedKmh < 40)
            command.accelerate = true;
        outInput.SetAccelerateHeld(command.accelerate);
        outInput.SetBrakeHeld(command.brake);
        outInput.SetAnalogSteering(command.steering);
        // Persistent Auto Lap drive readout: ground support, missing-ground
        // frames, accelerate command, and speed. GS=0 for >8 frames forces
        // accelerate off in AutoLapDrivePolicy.
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        lastAlGround_ = drive.groundSupported;
        lastAlAccel_ = command.accelerate;
        lastAlSpeedKmh_ = drive.speedKmh;
#endif
        if ((frameCounter_ & 7u) == 0u)
        {
            SRL::Debug::Print(1, 22, "AL g:%u m:%u a:%u s:%d",
                              drive.groundSupported ? 1u : 0u,
                              static_cast<unsigned>(autoLapDriveState_.missingGroundFrames),
                              command.accelerate ? 1u : 0u,
                              static_cast<int>(drive.speedKmh));
        }
    }

    Game::GameplayFrameState BuildGameplayFrameState(const FrameInputState& input)
    {
        Game::GameplayFrameState frameState{};

        Game::CarSystem* car = ActiveCarSystem();
        if (car)
        {
            Game::CarSystem::GameplayInputSnapshot gameplayInput{};
            if (AutoLapPhysicsEnabled())
            {
                BuildAutoLapDriveInput(*car, gameplayInput);
            }
            else if (!AutoLapTestEnabled() &&
                     (!context_.TimeAttackEnabled() || !timeAttack_.BlocksControls()))
            {
                gameplayInput.SetAccelerateHeld(input.bHeld);
                gameplayInput.SetBrakeHeld(input.cHeld);
                gameplayInput.SetSteerLeftHeld(input.leftHeld);
                gameplayInput.SetSteerRightHeld(input.rightHeld);
                gameplayInput.SetShiftDownHeld(input.lHeld);
                gameplayInput.SetShiftUpHeld(input.rHeld);
                gameplayInput.SetShiftLockHeld(input.xHeld);
            }
            car->PrepareGameplayFrameState(AutoLapLegacyEnabled() ? nullptr : &gameplayInput,
                                           frameCounter_,
                                           context_.carWorldPosition,
                                           carYawDeg_,
                                           AutoLapLegacyEnabled(),
                                           frameState);
            // G1.1: seed TCOL probes from mid-rail PATH segment (stable ±4 window).
            if (AutoLapPhysicsEnabled() && autoLapRoute_.Built() &&
                AutoLapRouteDomain::HasConsistentMappedRoute(autoLapRoute_) &&
                !autoLapRoute_.centers.empty() &&
                autoLapRoute_.ids.size() == autoLapRoute_.centers.size())
            {
                const size_t count = autoLapRoute_.centers.size();
                const size_t idx =
                    static_cast<size_t>(autoLapRoute_.index) % count;
                const int32_t railSeg =
                    static_cast<int32_t>(autoLapRoute_.ids[idx]);
                if (railSeg > 0)
                    frameState.activeSegmentId = railSeg;
            }
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
            if (AutoLapPhysicsEnabled())
            {
                frameState.autoLapContactKickXRaw = pendingCarContactKickXRaw_;
                frameState.autoLapContactKickZRaw = pendingCarContactKickZRaw_;
                RecoverStalledAutoLapCar(frameState, car->RuntimeDebug().speedKmh);
            }
#endif
        }
        else
        {
            frameState.frameId = frameCounter_;
            frameState.carWorldPosition = context_.carWorldPosition;
            frameState.carYawDeg = carYawDeg_;
        }
        return frameState;
    }

#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
    // Resolve a stable mid-rail index: lastValid segment → nearest PATH → route.index.
    size_t ResolvePlayerRailRecoverIndex() const
    {
        const size_t count = autoLapRoute_.centers.size();
        if (count == 0u) return 0u;
        if (playerLastValidSegmentId_ > 0 &&
            autoLapRoute_.ids.size() == count)
        {
            for (size_t i = 0u; i < count; ++i)
            {
                if (static_cast<int32_t>(autoLapRoute_.ids[i]) ==
                    playerLastValidSegmentId_)
                {
                    return i;
                }
            }
        }
        size_t nearest = static_cast<size_t>(autoLapRoute_.index) % count;
        if (AutoLapRouteDomain::FindNearestRoutePointIndex(
                autoLapRoute_, context_.carWorldPosition, nearest))
        {
            return nearest % count;
        }
        return nearest % count;
    }

    void RecoverStalledAutoLapCar(Game::GameplayFrameState& frameState,
                                  int16_t speedKmh)
    {
        // An async Slave job can make ExecuteGameplayFrame discard this frame.
        // Do not advance stall/recovery state until a new frame can be accepted.
        if (simState_.JobInFlight()) return;
        if (playerRecoveryRetryCountdown_ > 0u)
        {
            --playerRecoveryRetryCountdown_;
            return;
        }
        // autolap50: do not hard-recover while a previous respawn is in flight —
        // that stacked RelocateFocusWindow + teleports into a search loop.
        if (playerRecoveryAwaitingPublish_) return;

        const bool lostGround =
            autoLapDriveState_.missingGroundFrames >= 24u;
        const bool stalled = AutoLapTrafficPolicy::TickStall(
            playerStallState_, context_.carWorldPosition.X.RawValue(),
            context_.carWorldPosition.Z.RawValue(), speedKmh, true);
        if (!lostGround && !stalled) return;
        if (!context_.trackSystem || !context_.trackSystem->TrackCollisionMapReady() ||
            autoLapRoute_.centers.empty() ||
            !AutoLapRouteDomain::HasConsistentMappedRoute(autoLapRoute_)) return;

        static constexpr uint8_t kAsphalt = 1u;
        const size_t count = autoLapRoute_.centers.size();
        const size_t startIndex = ResolvePlayerRailRecoverIndex();
        // One relocate to the anchor rail, then probe a few points ahead in
        // the *same* window — never RelocateFocusWindow per candidate (autolap50).
        const int32_t anchorSeed =
            static_cast<int32_t>(autoLapRoute_.ids[startIndex]);
        if (anchorSeed > 0)
            (void)context_.trackSystem->RelocateFocusWindow(anchorSeed);

        size_t chosen = startIndex;
        SRL::Math::Types::Vector3D candidate = autoLapRoute_.centers[startIndex];
        int32_t segmentId = anchorSeed;
        bool havePose = false;

        for (size_t advance = 0u; advance <= 4u; ++advance)
        {
            const size_t index = (startIndex + advance) % count;
            const auto& point = autoLapRoute_.centers[index];
            if (advance > 0u && npcRailsArmed_ && autoLapNpcFollowers_[0].Active())
            {
                const auto& npc = autoLapNpcFollowers_[0].pos;
                const int64_t dx = static_cast<int64_t>(point.X.RawValue()) - npc.X.RawValue();
                const int64_t dz = static_cast<int64_t>(point.Z.RawValue()) - npc.Z.RawValue();
                if (dx > -(140LL << 16) && dx < (140LL << 16) &&
                    dz > -(140LL << 16) && dz < (140LL << 16)) continue;
            }
            const int32_t seed = static_cast<int32_t>(autoLapRoute_.ids[index]);
            SRL::Math::Types::Fxp groundY{};
            int32_t plantedSeg = -1;
            if (!context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                    point, context_.trackSegOffset, &kAsphalt, 1u,
                    groundY, &plantedSeg, seed > 0 ? seed : anchorSeed,
                    true, nullptr, nullptr, nullptr, -1, false) ||
                plantedSeg <= 0)
            {
                continue;
            }
            SRL::Math::Types::Vector3D tryPos = point;
            tryPos.Y = groundY + Game::CarPhysics::GetRideHeightOffset();
            const auto yaw = SRL::Math::Types::Angle::FromDegrees(
                SRL::Math::Types::Fxp::BuildRaw(
                    static_cast<int32_t>(autoLapRoute_.yawDeg[index]) << 16));
            const SRL::Math::Types::Vector3D forward(
                SRL::Math::Trigonometry::Sin(yaw),
                SRL::Math::Types::Fxp::BuildRaw(0),
                -SRL::Math::Trigonometry::Cos(yaw));
            SRL::Math::Types::Vector3D wallPush{};
            if (context_.trackSystem->FindPlanarWallPush(
                    tryPos, context_.trackSegOffset, forward,
                    SRL::Math::Types::Fxp::BuildRaw(30 << 16),
                    wallPush, nullptr, plantedSeg, false, false, &tryPos) &&
                (wallPush.X.RawValue() != 0 || wallPush.Z.RawValue() != 0))
            {
                continue;
            }
            chosen = index;
            candidate = tryPos;
            segmentId = plantedSeg;
            havePose = true;
            break;
        }

        if (!havePose)
        {
            // Stable fallback: exact PATH point under the single relocated window.
            candidate = autoLapRoute_.centers[startIndex];
            candidate.Y = candidate.Y + Game::CarPhysics::GetRideHeightOffset();
            segmentId = anchorSeed > 0 ? anchorSeed : playerLastValidSegmentId_;
            chosen = startIndex;
        }

        frameState.resetRequested = true;
        frameState.respawnPosition = candidate;
        frameState.respawnYawDeg = autoLapRoute_.yawDeg[chosen];
        frameState.activeSegmentId = segmentId;
        playerRecoveryFrameId_ = frameState.frameId;
        playerRecoveryAwaitingPublish_ = true;
        autoLapRoute_.index = static_cast<uint16_t>(chosen);
        autoLapDriveState_ = {};
        playerTrafficState_ = {};
        playerPassState_ = {};
        playerStallState_ = {};
        playerLastValidSegmentId_ = segmentId;
        playerSafePose_ = candidate;
        playerSafeYawDeg_ = static_cast<int16_t>(autoLapRoute_.yawDeg[chosen]);
        playerSafePoseValid_ = true;
        if (context_.cameraSystem) context_.cameraSystem->ResetFollowHistory();
        cameraPathRuntime_.SetPrevCarWorldPositionValid(false);
        // Long grace — prevents autolap50 teleport search loop.
        playerRecoveryRetryCountdown_ = 60u;
    }
#endif

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
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
            pendingCarContactKickXRaw_ = 0;
            pendingCarContactKickZRaw_ = 0;
#endif
        }
        if (frameState.resetRequested)
        {
            frameState.carWorldPosition = frameState.respawnPosition;
            frameState.carYawDeg = frameState.respawnYawDeg;
            frameState.resetRequested = false;
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

        SimulationSchedulerDomain::SimulationFrameContext dispatchContext{};
        Game::SimulationSchedulerPolicy dispatchPolicy{};
        dispatchPolicy.lockstep = context_.SlaveSimulationLockstep();
        SimulationSchedulerDomain::SeedSimulationFrameContext(
            frameState,
            dispatchPolicy,
            context_.SlaveSimulationLockstep()
                ? Game::SimulationDispatchMode::SlaveLockstep
                : Game::SimulationDispatchMode::SlaveAsync,
            true,
            context_.SlaveSimulationLockstep(),
            IsTrackProducerJobInFlightHint(),
            carPrepareState_.JobInFlight(),
            dispatchContext);

        SimulationSchedulerDomain::SimulationDispatchPacket dispatchPacket{};
        SimulationSchedulerDomain::SeedSimulationDispatchPacket(dispatchContext,
                                                               simState_,
                                                               dispatchPacket);
        if (dispatchPacket.blockedByInFlightJob || dispatchPacket.blockedByCarPrepare)
        {
            return false;
        }
        if (dispatchPacket.blockedByTrackBusy)
        {
            ++simState_.slaveDispatchSkipsTrackBusy;
            BackoffSimulationSlaveDispatch();
            return false;
        }
        if (!SimulationSchedulerDomain::CanDispatchSimulationPacket(dispatchPacket))
        {
            return false;
        }

        const uint8_t slot = dispatchPacket.targetSlot;
        simState_.input[slot] = dispatchPacket.payload;
        simulationTask_.Configure(&simState_.input[slot],
                                  &simState_.output[slot],
                                  context_.gameplayTick,
                                  context_.carPhysics,
                                  context_.trackCollision);
        SRL::Slave::ExecuteOnSlave(simulationTask_);
        SimulationSchedulerDomain::MarkSimulationDispatched(simState_, slot);
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        pendingCarContactKickXRaw_ = 0;
        pendingCarContactKickZRaw_ = 0;
#endif
        return true;
    }

    void ExecuteGameplayFrame(Game::GameplayFrameState& frameState)
    {
        const bool useSlaveSim =
            context_.EnableSlaveForSimulation() &&
            (context_.gameplayTick || context_.carPhysics);
        // Overlap only once TCOL is the query source. Before that, Sample walks
        // the meshes the Master is streaming, and the two CPUs would share the heap.
        const bool slaveQueryIsolated =
            context_.trackSystem && context_.trackSystem->TrackCollisionMapReady();

        if (useSlaveSim && !slaveQueryIsolated)
        {
            RunGameplayFrameSynchronously(frameState);
            return;
        }

        if (useSlaveSim)
        {
            if (context_.SlaveSimulationLockstep())
            {
                // True lockstep: camera/render must consume the same frame state
                // produced by simulation to avoid chase drift.
                if (simState_.JobInFlight())
                {
                    (void)DrainSimulationJobIfInFlight(true);
                    if (simState_.HasCompleted())
                    {
                        goto sim_lockstep_completed;
                    }
                }

                if (TryDispatchSimulationOnSlave(frameState))
                {
                    (void)DrainSimulationJobIfInFlight(true);
                    if (simState_.HasCompleted())
                    {
                        goto sim_lockstep_completed;
                    }
                }

                // Fallback safety: if Slave dispatch/drain fails, keep frame coherent.
                RunGameplayFrameSynchronously(frameState);
                return;

            sim_lockstep_completed:
                SimulationSchedulerDomain::ClearSimulationCompleted(simState_);
                return;
            }

            // Non-lockstep mode: never block Master waiting for Slave sim.
            // Consume completion opportunistically and keep rendering with last
            // committed state while a new sim job is in-flight.
            (void)DrainSimulationJobIfInFlight(false);
            if (simState_.JobInFlight())
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

    // ---- Domain: presentation and camera ---------------------------------

    void ScheduleCarPrepareIfEnabled()
    {
        if (!CanRenderCar() || !context_.EnableSlaveForCarPrepare()) return;
        if (carPrepareState_.JobInFlight()) return;
        if (IsTrackProducerJobInFlightHint()) return;

        const uint8_t slot = carPrepareState_.writeIdx;
        carPrepareState_.inputYaw[slot] = carYawDeg_;
        carPrepareTask_.Configure(&carPrepareState_.inputYaw[slot], &carPrepareState_.outputYaw[slot]);
        SRL::Slave::ExecuteOnSlave(carPrepareTask_);
        SimulationSchedulerDomain::MarkPrepareDispatched(carPrepareState_, slot);
    }

    void UpdateBackground()
    {
        if (!context_.EnableBg() || !context_.bgManager) return;
        AppState::Set(AppState::Stage::LoopBackground, frameCounter_);
        context_.bgManager->Update(context_.cameraSystem->State(), CurrentCameraYawDeg());
    }

    SRL::Math::Types::Vector3D ResolveCameraWallOcclusion(
        const SRL::Math::Types::Vector3D& desiredCameraLocation,
        const SRL::Math::Types::Vector3D& lookTarget) const
    {
        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector3D;

        (void)lookTarget;
        if (!context_.trackSystem || !context_.TrackSystemReady())
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
                                                          false,
                                                          /*commitPrevPosition=*/false,
                                                          &samplePos))
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

    SRL::Math::Types::Vector3D ResolveCameraSurfaceGuard(
        const SRL::Math::Types::Vector3D& desiredCameraLocation)
    {
        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector3D;

        if constexpr (!Game::PhysicsFeatureFlags::kEnableCameraSurfaceGuard)
        {
            return desiredCameraLocation;
        }
        if (!context_.trackSystem || !context_.TrackSystemReady() ||
            (context_.cameraSystem &&
             context_.cameraSystem->GetChasePreset() == CameraSystem::ChasePreset::FirstPerson))
        {
            CameraSurfaceGuard::Reset(cameraSurfaceGuardState_);
            return desiredCameraLocation;
        }

        // The ground query keeps its sticky face on TrackSystem. Skip it while
        // the Slave is inside the same query for the car.
        if (CameraSurfaceGuard::ShouldQuery(cameraSurfaceGuardState_) &&
            !simState_.JobInFlight())
        {
            Fxp surfaceY = desiredCameraLocation.Y;
            int32_t segmentId = -1;
            int16_t faceIndex = -1;
            const int32_t seedSegmentId =
                (cameraSurfaceGuardState_.segmentId > 0)
                    ? static_cast<int32_t>(cameraSurfaceGuardState_.segmentId)
                    : static_cast<int32_t>(
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
                        cameraNpcFocus_ && NpcFocusTargetValid()
                            ? autoLapNpcDrivers_[0].StreamingSegmentId()
                            :
#endif
                        latestActiveSegmentId_);
            const bool found = context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                desiredCameraLocation,
                context_.trackSegOffset,
                Game::CarPhysics::Tunables::kDriveableSurfaceTypes.data(),
                Game::CarPhysics::Tunables::kDriveableSurfaceTypes.size(),
                surfaceY,
                &segmentId,
                seedSegmentId,
                false,
                nullptr,
                nullptr,
                &faceIndex,
                cameraSurfaceGuardState_.faceIndex,
                false);
            if (found)
            {
                CameraSurfaceGuard::Observe(cameraSurfaceGuardState_,
                                            surfaceY.RawValue(),
                                            segmentId,
                                            faceIndex);
            }
            else
            {
                CameraSurfaceGuard::Miss(cameraSurfaceGuardState_);
            }
        }

        Vector3D resolved = desiredCameraLocation;
        resolved.Y = Fxp::BuildRaw(CameraSurfaceGuard::ResolveYRaw(
            cameraSurfaceGuardState_, desiredCameraLocation.Y.RawValue()));
        return resolved;
    }

    // Resolve camera snapshot for the current authoritative car state.
    CameraFrameState ResolveCameraFrameState()
    {
        bool focusNpc = false;
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        focusNpc = cameraNpcFocus_ && NpcFocusTargetValid();
        if (cameraNpcFocus_ && !focusNpc) SetCameraNpcFocus(false);
#endif
        if (focusNpc) PublishEmptyCameraPathFrameContext();
        else UpdateCameraPathFrameContext();
        const SRL::Math::Types::Vector3D focusPosition =
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
            focusNpc ? autoLapNpcDrivers_[0].StreamingPosition(autoLapNpcFollowers_[0]) :
#endif
            context_.carWorldPosition;
        // Feed road attitude from car (grade/pitch already computed). No track probes.
        if (context_.cameraSystem)
        {
            context_.cameraSystem->SetCarYawDegrees(CurrentCameraYawDeg());
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
            if (focusNpc)
            {
                const auto& npcFrame = autoLapNpcDrivers_[0].Frame();
                context_.cameraSystem->SetRoadAttitude(npcFrame.debugGradeTanX100, 0);
                context_.cameraSystem->ObserveWallContact(
                    npcFrame.debugWallHit != 0u,
                    npcFrame.debugWallPushX, npcFrame.debugWallPushZ,
                    focusPosition);
            }
            else
#endif
            {
                const Game::CarSystem::RuntimeDebugSnapshot carDbg = CarRuntimeDebugSnapshot();
                context_.cameraSystem->SetRoadAttitude(carDbg.gradeTanX100, carDbg.bodyPitchDeg);
                context_.cameraSystem->ObserveWallContact(carDbg.WallHit(),
                                                          carDbg.wallPushX,
                                                          carDbg.wallPushZ,
                                                          focusPosition);
            }
        }
        SRL::Math::Types::Vector3D rawCameraLocation =
            context_.cameraSystem->CameraLocation(focusPosition);
        SRL::Math::Types::Vector3D rawLookTarget =
            context_.cameraSystem->LookTarget(focusPosition, context_.modelOffset);

        const SRL::Math::Types::Vector3D resolvedCameraLocation =
            ResolveCameraSurfaceGuard(rawCameraLocation);
        context_.cameraSystem->CommitSafetyResolvedLocation(resolvedCameraLocation);
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

        if (context_.VerboseFrameLogs())
        {
            SRL::Debug::Print(0, 18, "Cam pos: %d %d %d",
                              frame.location.X.As<int16_t>(),
                              frame.location.Y.As<int16_t>(),
                              frame.location.Z.As<int16_t>());
        }
        return frame;
    }

    // HUD consumes the resolved camera snapshot and current world state.
    void UpdateHud(const CameraFrameState& camera)
    {
        context_.hudSystem->BeginMinimapFrame();
        context_.hudSystem->UpdateMinimap(latestActiveSegmentId_, carYawDeg_);
        UpdateNpcMinimapMarkers();
        context_.hudSystem->EndMinimapFrame();
        context_.hudSystem->Update(context_.cameraSystem->State(),
                                   context_.modelOffset,
                                   camera.location,
                                   context_.carWorldPosition);
        if (Game::CarSystem* car = ActiveCarSystem())
        {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
            if (cameraNpcFocus_ && NpcFocusTargetValid())
            {
                const auto& npcFrame = autoLapNpcDrivers_[0].Frame();
                const int16_t gear = npcFrame.carGear;
                const char gearChar = gear < 0 ? 'R' :
                    (gear == 0 ? 'N' : static_cast<char>('0' + (gear > 9 ? 9 : gear)));
                context_.hudSystem->UpdateDrivetrain(
                    npcFrame.carSpeedKmh, npcFrame.carEngineRpm, gearChar);
                return;
            }
#endif
            const auto drivetrain = car->BuildDrivetrainDebugSnapshot();
            context_.hudSystem->UpdateDrivetrain(drivetrain.speedKmh,
                                                drivetrain.engineRpm,
                                                drivetrain.gearChar);
        }
    }

    struct CarRenderRuntimeInputs
    {
        SRL::Math::Types::Vector3D renderPosition{};
        int32_t gameplayYawDeg = 0;
        int32_t visualYawOffsetDeg = 0;
        Game::CarSystem::RuntimeDebugSnapshot runtimeDebug{};
        int32_t depthBiasUnits = 0;
    };

    Game::CarSystem::RuntimeDebugSnapshot CarRuntimeDebugSnapshot() const
    {
        if (Game::CarSystem* car = ActiveCarSystem())
        {
            return car->RuntimeDebug();
        }
        return {};
    }

    int32_t ResolveCarRenderDepthBiasUnits(
        const Game::CarSystem::RuntimeDebugSnapshot& runtimeDebug) const
    {
        // Do not pull/lift the car — flying look. Seam fix is track-side bias + SORT_MIN.
        (void)runtimeDebug;
        return 0;
    }

    CarRenderRuntimeInputs ResolveCarRenderRuntimeInputs(const Game::CarSystem& car)
    {
        CarRenderRuntimeInputs inputs{};
        inputs.renderPosition = ResolveCarRenderPosition();
        inputs.gameplayYawDeg = carYawDeg_;
        inputs.visualYawOffsetDeg = car.VisualYawOffsetDegrees();
        inputs.runtimeDebug = car.RuntimeDebug();
        inputs.depthBiasUnits = ResolveCarRenderDepthBiasUnits(inputs.runtimeDebug);
        return inputs;
    }

    void StoreShadowDebugState(const SRL::Math::Types::Vector3D& shadowWorldPosition,
                               int32_t shadowYawDeg)
    {
        shadowDebug_.worldPos = shadowWorldPosition;
        shadowDebug_.yawDeg = shadowYawDeg;
    }

    void CaptureShadowDebugState(const Game::CarRenderSystem::ShadowPacket& shadowPacket)
    {
        StoreShadowDebugState(shadowPacket.shadowPosition, shadowPacket.shadowYawDeg);
    }

    SRL::Math::Types::Angle BuildShadowDrawYaw(
        const Game::CarRenderSystem::ShadowPacket& shadowPacket)
    {
        using SRL::Math::Types::Angle;
        using SRL::Math::Types::Fxp;

        return Angle::FromDegrees(
            Fxp::BuildRaw(static_cast<int32_t>(shadowPacket.shadowYawDeg) << 16));
    }

    void DrawCarShadowBlob(const Game::CarRenderSystem::ShadowPacket& shadowPacket)
    {
        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector2D;
        using SRL::Math::Types::Vector3D;

        constexpr Fxp kShadowHalfLength = Fxp::BuildRaw(0x002C0000); // 44.0 (+~30%)
        constexpr Fxp kShadowHalfWidth = Fxp::BuildRaw(0x00150000);  // 21.0 (+~30%)
        // Scene2D sort bias: positive pushes farther back in the VDP1 order used here.
        constexpr Fxp kShadowSortBias = Fxp::BuildRaw(0x00100000);   // force shadow behind car
        constexpr SRL::Types::HighColor kShadowColor = SRL::Types::HighColor::FromRGB555(0, 0, 0);

        CaptureShadowDebugState(shadowPacket);
        Vector3D center = shadowPacket.shadowPosition;
        const auto yaw = BuildShadowDrawYaw(shadowPacket);
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

    void DrawCarShadowModel(const Game::CarRenderSystem::ShadowPacket& shadowPacket)
    {
        if (!context_.carShadowRenderer) return;

        CaptureShadowDebugState(shadowPacket);
        const SRL::Math::Types::Vector3D& shadowPos = shadowPacket.shadowPosition;
        const auto yaw = BuildShadowDrawYaw(shadowPacket);
        context_.carShadowRenderer->Render(shadowPos, yaw, false);
        lastRenderedCarFacesThisFrame_ = GameLoopRuntime::ClampToU16(
            static_cast<uint32_t>(lastRenderedCarFacesThisFrame_) +
            context_.carShadowRenderer->LastRenderFaceCount());
    }

    void RenderCar(const CameraFrameState& camera)
    {
        if (!CanRenderCar()) return;

        AppState::Set(AppState::Stage::LoopCar, frameCounter_);
        Game::CarSystem* car = ActiveCarSystem();
        if (!car) return;

        const auto renderInputs = ResolveCarRenderRuntimeInputs(*car);
        // Car stays on the ground (no visual lift). Seam fighting is handled by
        // track near-pass depth push/sink + SORT_MIN on car faces (see main.cxx /
        // track_system two-pass). Lift 0 = grounded; do not reintroduce large lift.
        constexpr int32_t kCarVisualLiftUnits = 0;
        const auto renderPacket = GameLoopRuntime::BuildCarRenderRuntimePacket(
            renderInputs.renderPosition,
            camera.location,
            camera.lookTarget,
            renderInputs.gameplayYawDeg,
            renderInputs.visualYawOffsetDeg,
            renderInputs.runtimeDebug,
            kCarVisualLiftUnits,
            renderInputs.depthBiasUnits);

#if !AUTO_LAP_NPC_RAILS || AUTO_LAP_NPC_SLIM
        RenderCarShadowIfEnabled(renderPacket);
#endif
        // Car is drawn unlit/flat (DrawAsFlat). Still rebind light so any residual
        // SGL global light state from track does not linger into other systems.
        car->SetLightDirection(context_.lightDirection);
        // Flat lighting and SORT_MIN are immutable runtime attributes configured
        // once during car bootstrap (main.cxx).  Re-walking every face here was
        // pure Master-side work on every frame.
        GameLoopRuntime::ApplyCarRenderRuntimeSync(*car, renderPacket, renderInputs.gameplayYawDeg);
        // Fase 2A mesh1: player SubmitRender+Flush, then reuse the same
        // MeshRenderer at side-rail poses (zero extra Model*/MeshRenderer alloc).
        if (!context_.renderPipeline) return;
        context_.renderPipeline->Reset();
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        PrepareNpcDrawPoses();
        // Crosses before 3D flush (pose4: post-Flush Scene2D pins never showed).
        DrawNpcPoseMarkers();
#endif
        car->SubmitRender(*context_.renderPipeline);
        context_.renderPipeline->Flush();
        lastRenderedCarFacesThisFrame_ = GameLoopRuntime::ClampToU16(
            static_cast<uint32_t>(lastRenderedCarFacesThisFrame_) +
            static_cast<uint32_t>(
                GameLoopRuntime::CaptureCarRenderRuntimeFaceCount(*car)));
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        RenderNpcWithPlayerRenderer(*car);
#endif
    }

    bool EnsureNpcCarRenderers()
    {
        // mesh1 reuses the player MeshRenderer — no dedicated NPC clones.
        return false;
    }

    // World PATH separation at the player's Auto Lap index: side[i] - mid[i].
    bool ResolveNpcLiveLateralRaw(size_t slot, int32_t& outDx, int32_t& outDz) const
    {
        outDx = 0;
        outDz = 0;
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (slot >= AutoLapNpcRailPolicy::kNpcRailCount) return false;
        if (!autoLapNpcRoutes_[slot].Built() ||
            autoLapNpcRoutes_[slot].centers.empty() ||
            autoLapRoute_.centers.empty())
        {
            return false;
        }
        const size_t sideCount = autoLapNpcRoutes_[slot].centers.size();
        const size_t midCount = autoLapRoute_.centers.size();
        const size_t midIndex =
            static_cast<size_t>(autoLapRoute_.index) % midCount;
        const size_t sideIndex = (sideCount == midCount)
            ? midIndex
            : ((midIndex * sideCount) / midCount);
        const auto& sidePt = autoLapNpcRoutes_[slot].centers[sideIndex];
        const auto& midPt = autoLapRoute_.centers[midIndex];
        outDx = sidePt.X.RawValue() - midPt.X.RawValue();
        outDz = sidePt.Z.RawValue() - midPt.Z.RawValue();
        return (outDx != 0 || outDz != 0);
#else
        (void)slot;
        return false;
#endif
    }

    // Project PATH (side-mid) onto the car's right axis (shadow-frame convention).
    // mesh4 applied world dx/dz directly → after ~180° turns the NPC looked like
    // it swapped rails relative to the nose. Local projection keeps one side.
    bool ResolveNpcCarFrameLateralRaw(size_t slot,
                                      int32_t yawDeg,
                                      int32_t& outDx,
                                      int32_t& outDz) const
    {
        outDx = 0;
        outDz = 0;
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        int32_t worldDx = 0;
        int32_t worldDz = 0;
        if (!ResolveNpcLiveLateralRaw(slot, worldDx, worldDz))
        {
            worldDx = 72 << 16;
            worldDz = -(76 << 16);
        }

        using SRL::Math::Types::Angle;
        using SRL::Math::Types::Fxp;
        const Angle yaw = Angle::FromDegrees(Fxp::BuildRaw(yawDeg << 16));
        const Fxp sinYaw = SRL::Math::Trigonometry::Sin(yaw);
        const Fxp cosYaw = SRL::Math::Trigonometry::Cos(yaw);
        // Same basis as car shadow: right = (cos, sin).
        const Fxp rightX = cosYaw;
        const Fxp rightZ = sinYaw;

        const int64_t dot64 =
            (static_cast<int64_t>(worldDx) * static_cast<int64_t>(rightX.RawValue())) +
            (static_cast<int64_t>(worldDz) * static_cast<int64_t>(rightZ.RawValue()));
        const Fxp lat = Fxp::BuildRaw(static_cast<int32_t>(dot64 >> 16));

        outDx = (rightX * lat).RawValue();
        outDz = (rightZ * lat).RawValue();
        return (outDx != 0 || outDz != 0);
#else
        (void)slot;
        (void)yawDeg;
        return false;
#endif
    }

    SRL::Math::Types::Vector3D ResolveNpcCarDrawPosition(size_t slot)
    {
        const SRL::Math::Types::Vector3D base = ResolveCarRenderPosition();
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (slot >= AutoLapNpcRailPolicy::kNpcRailCount ||
            !autoLapNpcFollowers_[slot].Active())
        {
            return base;
        }
        return autoLapNpcFollowers_[slot].pos;
#else
        (void)slot;
        return base;
#endif
    }

    // Live pose in car frame: player + project(side-mid → right); yaw = car 1.
    void PrepareNpcDrawPoses()
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        for (size_t slot = 0; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            npcDrawValid_[slot] = false;
            npcScreenValid_[slot] = false;
        }
        if (!npcRailsArmed_ || autoLapNpcRailReadyCount_ == 0u) return;

        Game::CarSystem* car = ActiveCarSystem();
        const int32_t visualYawOffsetDeg = car ? car->VisualYawOffsetDegrees() : 0;
        const SRL::Math::Types::Vector3D playerBase = ResolveCarRenderPosition();
        npcBaseXRaw_ = playerBase.X.RawValue();
        npcBaseZRaw_ = playerBase.Z.RawValue();

        for (size_t slot = 0; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            if (!autoLapNpcFollowers_[slot].Active()) continue;

            npcDrawPos_[slot] = ResolveNpcCarDrawPosition(slot);
            npcDrawYawDeg_[slot] = static_cast<int16_t>(NormalizeYawDeg360(
                static_cast<int32_t>(autoLapNpcFollowers_[slot].yawDeg) +
                visualYawOffsetDeg));
            npcDrawValid_[slot] = true;
            npcLatDxRaw_[slot] = npcDrawPos_[slot].X.RawValue() - playerBase.X.RawValue();
            npcLatDzRaw_[slot] = npcDrawPos_[slot].Z.RawValue() - playerBase.Z.RawValue();

            SRL::Math::Types::Vector2D screen{};
            const auto depth = SRL::Scene3D::ProjectToScreen(npcDrawPos_[slot], &screen);
            npcScreenDepthRaw_[slot] = depth.RawValue();
            if (depth.RawValue() > 0)
            {
                npcScreenX_[slot] = static_cast<int16_t>(screen.X.RawValue() >> 16);
                npcScreenY_[slot] = static_cast<int16_t>(screen.Y.RawValue() >> 16);
                npcScreenValid_[slot] = true;
            }
            else
            {
                npcScreenX_[slot] = -80;
                npcScreenY_[slot] = -40;
                npcScreenValid_[slot] = true;
            }
        }
#endif
    }

    uint8_t RenderNpcSlotWithPlayerRenderer(Game::CarSystem& car, size_t slot)
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        MeshRenderer* renderer = car.Renderer();
        if (!renderer || slot >= AutoLapNpcRailPolicy::kNpcRailCount) return 0u;
        if (!npcDrawValid_[slot]) return 0u;

        const SRL::Math::Types::Angle yaw =
            SRL::Math::Types::Angle::FromDegrees(
                SRL::Math::Types::Fxp::BuildRaw(
                    static_cast<int32_t>(npcDrawYawDeg_[slot]) << 16));
        renderer->Render(npcDrawPos_[slot], yaw, false);
        const uint32_t faces = renderer->LastRenderFaceCount();
        npcReuseFaces_[slot] = GameLoopRuntime::ClampToU16(faces);
        lastRenderedCarFacesThisFrame_ = GameLoopRuntime::ClampToU16(
            static_cast<uint32_t>(lastRenderedCarFacesThisFrame_) + faces);
        return faces > 0u ? 1u : 0u;
#else
        (void)car;
        (void)slot;
        return 0u;
#endif
    }

    uint8_t RenderNpcWithPlayerRenderer(Game::CarSystem& car)
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        uint8_t drawn = 0u;
        for (size_t slot = 0; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            npcReuseFaces_[slot] = 0u;
            drawn = static_cast<uint8_t>(
                drawn + RenderNpcSlotWithPlayerRenderer(car, slot));
        }
        return drawn;
#else
        (void)car;
        return 0u;
#endif
    }

    uint8_t EnqueueNpcCars(RenderPipeline& pipeline)
    {
        // mesh1 draws via RenderNpcWithPlayerRenderer after Flush (no Enqueue).
        (void)pipeline;
        return 0u;
    }

    // Printed after PresentPeriodicFrameStats so VDP1/DRV/FPS lines do not win.
    void PrintNpcDiagHud()
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        const unsigned act0 = autoLapNpcFollowers_[0].Active() ? 1u : 0u;
        const unsigned v0 = npcDrawValid_[0] ? 1u : 0u;
        SRL::Debug::Print(
            0, 0, "ISO NPC a:%u v:%u f:%u c:%u cam:%u",
            act0, v0,
            static_cast<unsigned>(npcReuseFaces_[0]),
            static_cast<unsigned>(npcContactHits_),
            cameraNpcFocus_ ? 2u : 1u);

        // Car-frame lateral (projected onto right); side should stay stable in turns.
        SRL::Debug::Print(
            0, 14, "d:%d/%d y:%d g:%u v:%d",
            static_cast<int>(npcLatDxRaw_[0] >> 16),
            static_cast<int>(npcLatDzRaw_[0] >> 16),
            static_cast<int>(npcDrawYawDeg_[0]),
            static_cast<unsigned>(autoLapNpcDrivers_[0].Frame().debugGroundSupport),
            static_cast<int>(autoLapNpcDrivers_[0].Frame().carSpeedKmh));
#endif
    }

    void DrawNpcPoseMarkers()
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (!npcRailsArmed_) return;

        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector2D;

        static const SRL::Types::HighColor kColors[AutoLapNpcRailPolicy::kNpcRailCount] = {
            SRL::Types::HighColor::FromRGB555(31, 31, 0),  // yellow
        };
        constexpr int16_t kArm = 36;
        const Fxp armF = Fxp::BuildRaw(kArm << 16);
        const Fxp frontSort = Fxp::BuildRaw(1);

        for (size_t slot = 0; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            if (!npcDrawValid_[slot] || !npcScreenValid_[slot]) continue;

            // Use screen coords captured pre-Flush (view matrix still valid).
            const Vector2D center2D(
                Fxp::BuildRaw(static_cast<int32_t>(npcScreenX_[slot]) << 16),
                Fxp::BuildRaw(static_cast<int32_t>(npcScreenY_[slot]) << 16));

            const Vector2D left(center2D.X - armF, center2D.Y);
            const Vector2D right(center2D.X + armF, center2D.Y);
            const Vector2D up(center2D.X, center2D.Y - armF);
            const Vector2D down(center2D.X, center2D.Y + armF);

            SRL::Scene2D::DrawLine(left, right, kColors[slot], frontSort);
            SRL::Scene2D::DrawLine(up, down, kColors[slot], frontSort);
        }
#endif
    }

    void UpdateNpcMinimapMarkers()
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (!AutoLapPhysicsEnabled() || !npcRailsArmed_) return;
        for (size_t slot = 0u; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            if (!autoLapNpcFollowers_[slot].Active() ||
                !autoLapNpcDrivers_[slot].Active()) continue;
            // Pose-based segment (not StreamingSegmentId): when GS=0 the stream
            // id freezes on lastValid while the mesh stays at npc.pos → desync.
            const int32_t segmentId = autoLapNpcDrivers_[slot].MinimapSegmentId(
                autoLapNpcFollowers_[slot]);
            if (segmentId <= 0) continue;
            context_.hudSystem->UpdateNpcMinimap(
                static_cast<uint8_t>(slot), segmentId,
                autoLapNpcFollowers_[slot].yawDeg);
        }
#endif
    }

    // Re-plant player Y on asphalt (mid-rail XZ untouched). Isolates from NPC
    // shared-face-cache thrash (autolap48).
    bool TryPlayerSoftPlantAsphalt(Game::CarSystem& car)
    {
        if (!context_.trackSystem || !context_.trackSystem->TrackCollisionMapReady())
            return false;
        const int32_t seed = playerLastValidSegmentId_ > 0
            ? playerLastValidSegmentId_
            : static_cast<int32_t>(latestActiveSegmentId_);
        if (seed <= 0) return false;
        static constexpr uint8_t kAsphalt = 1u;
        SRL::Math::Types::Vector3D sample = context_.carWorldPosition;
        SRL::Math::Types::Fxp groundY{};
        int32_t segmentId = -1;
        if (!context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                sample, context_.trackSegOffset, &kAsphalt, 1u,
                groundY, &segmentId, seed, true,
                nullptr, nullptr, nullptr, -1, false) ||
            segmentId <= 0)
        {
            if (!autoLapRoute_.Built() || autoLapRoute_.centers.empty()) return false;
            const size_t count = autoLapRoute_.centers.size();
            const size_t index =
                static_cast<size_t>(autoLapRoute_.index) % count;
            sample = autoLapRoute_.centers[index];
            sample.Y = context_.carWorldPosition.Y;
            if (!context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                    sample, context_.trackSegOffset, &kAsphalt, 1u,
                    groundY, &segmentId, seed, true,
                    nullptr, nullptr, nullptr, -1, false) ||
                segmentId <= 0)
            {
                return false;
            }
            context_.carWorldPosition.X = sample.X;
            context_.carWorldPosition.Z = sample.Z;
        }
        context_.carWorldPosition.Y =
            groundY + Game::CarPhysics::GetRideHeightOffset();
        car.SetWorldPosition(context_.carWorldPosition);
        latestActiveSegmentId_ = static_cast<int16_t>(segmentId);
        playerLastValidSegmentId_ = segmentId;
        playerSafePose_ = context_.carWorldPosition;
        playerSafeYawDeg_ = static_cast<int16_t>(carYawDeg_);
        playerSafePoseValid_ = true;
        return true;
    }

    SRL::Math::Types::Vector3D ResolveCarRenderPosition()
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

        return carRenderPos;
    }

    void RenderCarShadowIfEnabled(const Game::CarRenderSystem::RenderPacket& renderPacket)
    {
        const auto decision = GameLoopRuntime::BuildCarShadowRuntimeDecision(
            context_.RenderCarShadowModel(),
            context_.carShadowRenderer != nullptr);
        Game::CarRenderSystem::ShadowPacket shadowPacket{};

        if (GameLoopRuntime::TryBuildCarShadowPrepPacket(renderPacket,
                                                         decision.drawBlob,
                                                         true,
                                                         false,
                                                         decision.blobGroundBiasUnits,
                                                         shadowPacket))
        {
            DrawCarShadowBlob(shadowPacket);
        }
        if (GameLoopRuntime::TryBuildCarShadowPrepPacket(renderPacket,
                                                         decision.drawModel,
                                                         false,
                                                         true,
                                                         decision.modelGroundBiasUnits,
                                                         shadowPacket))
        {
            DrawCarShadowModel(shadowPacket);
        }
    }

    void ConfigureScene3dView(const CameraFrameState& camera) const
    {
        using SRL::Math::Types::Angle;
        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(camera.location, camera.lookTarget, Angle::FromDegrees(0.0));
    }

    // Re-bind the fixed directional light after track/car draws. SGL smooth
    // lighting (slPutPolygonX) may mutate the light vector argument and leave
    // global slLight state inconsistent across segment batch boundaries.
    void RestoreDirectionalLight() const
    {
        auto lightCopy = context_.lightDirection;
        SRL::Scene3D::SetDirectionalLight(lightCopy);
        SRL::Scene3D::LightSetColor(context_.lightColor);
    }

    void CaptureIdleTrackAndCarTraces()
    {
        CaptureWorkRamStage(hwrStageTrace_.trackDraw, lwrStageTrace_.trackDraw);
        hwrStageTrace_.trackEnd = hwrStageTrace_.trackDraw;
        lwrStageTrace_.trackEnd = lwrStageTrace_.trackDraw;
        CaptureWorkRamStage(hwrStageTrace_.car, lwrStageTrace_.car);
    }

    // Submit prepared 3D content in the fixed order: track then car.
    void RenderFrame(const CameraFrameState& camera)
    {
        lastRenderedCarFacesThisFrame_ = 0u;
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
        RestoreDirectionalLight();
        RenderTrackFrame(camera);

        // Track DrawSmoothMesh may have mutated light state; rebind before car.
        // NPCs share this post-track window with the player (see RenderCar).
        ConfigureScene3dView(camera);
        RestoreDirectionalLight();
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Car);
#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp carTicksStart = CaptureMasterPhaseStamp();
#endif
        RenderCar(camera);
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        DrawAutoLapCameraFocusTransferCover();
#endif
#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp carTicksEnd = CaptureMasterPhaseStamp();
        masterFrameTiming_.carTicks =
            Sh2FrtProfiler::Elapsed(carTicksStart.frt, carTicksEnd.frt);
        RecordMasterPhase(FramePhaseTelemetry::Phase::Car,
                          carTicksStart,
                          carTicksEnd);
#endif
        CaptureWorkRamStage(hwrStageTrace_.car, lwrStageTrace_.car);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Unknown);
    }

    bool IsTrackFrameEnabled() const
    {
        return context_.trackSystem &&
               context_.TrackSystemReady() &&
               context_.RenderTrack();
    }

    void RenderTrackFrame(const CameraFrameState& camera)
    {
        if (!IsTrackFrameEnabled())
        {
            lastSubmittedTrackFacesThisFrame_ = 0u;
            GameLoopRuntime::CaptureTrackReuseRuntimeDisabledFrame(frameCounter_,
                                                                   latestActiveSegmentId_,
                                                                   trackReuseState_);
            CaptureWorkRamStage(hwrStageTrace_.trackDraw, lwrStageTrace_.trackDraw);
            hwrStageTrace_.trackEnd = hwrStageTrace_.trackDraw;
            lwrStageTrace_.trackEnd = lwrStageTrace_.trackDraw;
            return;
        }

#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp trackRenderTicksStart = CaptureMasterPhaseStamp();
#endif
        int32_t renderAnchorSegmentId = latestActiveSegmentId_;
        const SRL::Math::Types::Vector3D* renderAnchorPosition =
            &context_.carWorldPosition;
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (cameraFocusTransferPending_ && cameraFocusTransferNeedsRelocate_)
        {
            // A remote rebuild reuses the live renderer slots. Finish any
            // outstanding Slave simulation read before mutating that window.
            (void)DrainSimulationJobIfInFlight(true);
            const int32_t destinationSegment = cameraFocusTransferNpc_
                ? autoLapNpcDrivers_[0].StreamingSegmentId()
                : latestActiveSegmentId_;
            const bool relocated =
                context_.trackSystem->RelocateFocusWindow(destinationSegment);
            cameraFocusTransferNeedsRelocate_ = false;
            if (!relocated)
            {
                context_.trackSystem->EndFocusTransfer(false);
                cameraFocusTransferPending_ = false;
            }
        }
        const auto focusAnchor = AutoLapCameraFocusPolicy::Select(
            cameraFocusTransferPending_ ? cameraFocusTransferNpc_ : cameraNpcFocus_,
            NpcFocusTargetValid(), latestActiveSegmentId_,
            autoLapNpcDrivers_[0].StreamingSegmentId());
        if (focusAnchor.npc)
        {
            renderAnchorSegmentId = focusAnchor.segmentId;
            renderAnchorPosition =
                &autoLapNpcDrivers_[0].StreamingPosition(autoLapNpcFollowers_[0]);
        }
#endif
        // autolap48: when player GS dies, keep belt on lastValid so the scene
        // does not empty around a stopped car 1 (NPC focus excluded above).
        if (AutoLapPhysicsEnabled() &&
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
            !(cameraNpcFocus_ && NpcFocusTargetValid()) &&
#endif
            playerLastValidSegmentId_ > 0)
        {
            const bool liveBad = renderAnchorSegmentId <= 0 ||
                !SegmentInRenderWindow(renderAnchorSegmentId);
            if (liveBad)
            {
                renderAnchorSegmentId = playerLastValidSegmentId_;
                if (playerSafePoseValid_)
                    renderAnchorPosition = &playerSafePose_;
                if (!SegmentInRenderWindow(playerLastValidSegmentId_))
                    (void)context_.trackSystem->RelocateFocusWindow(
                        playerLastValidSegmentId_);
            }
        }
        // The render belt must follow the same car as the camera. Player
        // simulation, TCOL and lap timing still use the authoritative car 1.
        const auto trackFrameContext = TrackRenderDomain::BuildTrackFrameContext(
            frameCounter_,
            renderAnchorSegmentId,
            true,
            context_.trackSegOffset,
            context_.lightDirection,
            camera.location,
            camera.lookTarget,
            *renderAnchorPosition);
        const auto trackRenderPacket =
            TrackRenderDomain::BuildTrackRenderPacket(trackFrameContext, context_.trackSystem);
        lastSubmittedTrackFacesThisFrame_ = trackRenderPacket.submittedTrackFaces;
        GameLoopRuntime::CaptureTrackReuseRuntimeEnabledRequest(
            frameCounter_,
            renderAnchorSegmentId,
            IsTrackProducerJobInFlightHint(),
            trackReuseState_);

        context_.trackSystem->SetObservedCarSegmentId(trackRenderPacket.observedCarSegmentId);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::TrackCore);
        context_.trackSystem->BeginFrame(trackFrameContext.frameId);
        AppState::Set(AppState::Stage::LoopTrack, trackFrameContext.frameId);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::TrackPrepare);
        context_.trackSystem->RenderFrame(trackRenderPacket.valid,
                                          trackFrameContext.trackOffset,
                                          trackFrameContext.lightDirection,
                                          trackFrameContext.cameraLocation,
                                          trackFrameContext.cameraLookTarget,
                                          trackFrameContext.carWorldPosition);
#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp trackRenderTicksEnd = CaptureMasterPhaseStamp();
        masterFrameTiming_.trackStreamTicks = context_.trackSystem->StreamTicksThisFrame();
        masterFrameTiming_.trackDrawTicks = context_.trackSystem->DrawTicksThisFrame();
        masterFrameTiming_.trackFrameTicks = context_.trackSystem->FrameTicksThisFrame();
        masterFrameTiming_.trackRenderTicks =
            Sh2FrtProfiler::Elapsed(trackRenderTicksStart.frt, trackRenderTicksEnd.frt);
        RecordMasterPhase(FramePhaseTelemetry::Phase::TrackRender,
                          trackRenderTicksStart,
                          trackRenderTicksEnd);
#endif
        // BuildTrackRenderPacket carries the preceding coordinator snapshot. Read
        // again after RenderFrame so the HUD records polygons sent this frame.
        lastSubmittedTrackFacesThisFrame_ = GameLoopRuntime::ClampToU16(
            context_.trackSystem->Telemetry().submittedTrackFaces);
        CaptureWorkRamStage(hwrStageTrace_.trackDraw, lwrStageTrace_.trackDraw);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::TrackCore);
#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp trackEndTicksStart = CaptureMasterPhaseStamp();
#endif
        context_.trackSystem->EndFrame();
#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp trackEndTicksEnd = CaptureMasterPhaseStamp();
        masterFrameTiming_.trackEndTicks =
            Sh2FrtProfiler::Elapsed(trackEndTicksStart.frt, trackEndTicksEnd.frt);
        RecordMasterPhase(FramePhaseTelemetry::Phase::TrackEnd,
                          trackEndTicksStart,
                          trackEndTicksEnd);
#endif
        GameLoopRuntime::CommitTrackReuseRuntimeFrame(frameCounter_,
                                                      latestActiveSegmentId_,
                                                      trackRenderPacket.valid,
                                                      trackReuseState_);
        CaptureWorkRamStage(hwrStageTrace_.trackEnd, lwrStageTrace_.trackEnd);
    }

    void RenderAxes()
    {
        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector2D;
        using SRL::Math::Types::Vector3D;
        if (!context_.RenderAxes()) return;

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

    // ---- Domain: frame finalization and telemetry ------------------------

    void FinishFrame()
    {
#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp finishPrepTicksStart = CaptureMasterPhaseStamp();
#endif
        // Async sim mode: do not block frame completion on Slave sim.
        (void)DrainSimulationJobIfInFlight(false);

        ++frameCounter_;
        GameLoopRuntime::TrackRenderTelemetryViewPacket trackTelemetryView{};
        (void)GameLoopRuntime::TryBuildTrackRenderTelemetryViewPacket(context_.trackSystem,
                                                                      context_.TrackSystemReady(),
                                                                      context_.RenderTrack(),
                                                                      trackTelemetryView);
        const FramePresentationSnapshot framePresentation =
            BuildFramePresentationSnapshot(trackTelemetryView);

#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp finishHudTicksStart = CaptureMasterPhaseStamp();
        masterFrameTiming_.finishPrepTicks =
            Sh2FrtProfiler::Elapsed(finishPrepTicksStart.frt, finishHudTicksStart.frt);
#endif
        PresentFrameHudAndTelemetry(framePresentation, trackTelemetryView);
        if (context_.TimeAttackEnabled() && (frameCounter_ % 6u == 0u))
            context_.hudSystem->PresentTimeAttack(timeAttack_, SRL_AppGetVblankCounter());
#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp finishHudTicksEnd = CaptureMasterPhaseStamp();
        masterFrameTiming_.hudTicks = SaturatingAddTicks(
            masterFrameTiming_.hudTicks,
            Sh2FrtProfiler::Elapsed(finishHudTicksStart.frt, finishHudTicksEnd.frt));
        RecordMasterPhase(FramePhaseTelemetry::Phase::Hud,
                          finishHudTicksStart,
                          finishHudTicksEnd);
#endif
        SynchronizeFrameCore();
#if MASTER_FRAME_PHASE_TELEMETRY
        if (context_.trackSystem)
        {
            masterFrameTiming_.culledSegments =
                context_.trackSystem->TextureRecycleHudSampleThisFrame().culledSegments;
        }
        const FramePhaseTelemetry::Stamp overlayTicksStart = CaptureMasterPhaseStamp();
#endif
        UpdateFrameEndOverlays();
#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp overlayTicksEnd = CaptureMasterPhaseStamp();
        masterFrameTiming_.overlayTicks =
            Sh2FrtProfiler::Elapsed(overlayTicksStart.frt, overlayTicksEnd.frt);
        RecordMasterPhase(FramePhaseTelemetry::Phase::Overlay,
                          overlayTicksStart,
                          overlayTicksEnd);
        FinalizeMasterFramePhaseTelemetry(overlayTicksEnd);
#endif
    }

    FramePresentationSnapshot BuildFramePresentationSnapshot(
        const GameLoopRuntime::TrackRenderTelemetryViewPacket& trackTelemetryView) const
    {
        const Sh2SplitTelemetrySnapshot sh2 = trackTelemetryView.valid
            ? GameLoopRuntime::BuildSh2SplitTelemetrySnapshot(
                simState_,
                trackTelemetryView,
                kEnablePhysicsSafeTelemetry)
            : Sh2SplitTelemetrySnapshot{};
        return GameLoopRuntime::BuildFramePresentationSnapshot(
            IsTrackFrameEnabled() ? lastSubmittedTrackFacesThisFrame_ : 0u,
            CanRenderCar() ? lastRenderedCarFacesThisFrame_ : 0u,
            context_.EnableRuntimeStatsLogs(),
            sh2);
    }

    void PresentFrameHudAndTelemetry(
        const FramePresentationSnapshot& framePresentation,
        const GameLoopRuntime::TrackRenderTelemetryViewPacket& trackTelemetryView)
    {
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Finish);
        // NBG3 diagnostic text would cover the NBG2 bitmap timing panels.
        // Keep diagnostics available if the optional image atlas failed.
        if (context_.TimeAttackEnabled() && context_.hudSystem->HasTimeAttackImage())
            return;
        const bool allowOptionalHudTelemetry =
            !Game::MemoryBudgetRuntimeBridge::ShouldAvoidHudOptionalTelemetry();
        context_.hudSystem->PresentPeriodicFrameStats(frameCounter_,
                                                      SRL_AppGetVblankCounter(),
                                                      context_.EnableRuntimeStatsLogs() &&
                                                          allowOptionalHudTelemetry,
                                                      context_.LogTrack(),
                                                      context_.LogCar(),
                                                      context_.faceCount,
                                                      context_.vertexCount,
                                                      framePresentation.submittedTrackFaces,
                                                      framePresentation.submittedCarFaces);
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        // After periodic VDP1/stats so NPC Y/dP lines are not overwritten.
        PrintNpcDiagHud();
#elif AUTO_LAP_NPC_RAILS && AUTO_LAP_NPC_SLIM
        SRL::Debug::Print(0, 0, "ISO NPC slim");
#else
        // Control build fingerprint (AUTO_LAP_NPC_RAILS=0).
        SRL::Debug::Print(0, 0, "ISO BASE rails0");
#endif
        if (!framePresentation.RuntimeStatsEnabled()) return;
        if (Game::MemoryBudgetRuntimeBridge::ShouldAvoidDebugTransientOptionalTelemetry()) return;

        PrintSegmentOverlapDiagnostics(framePresentation.submittedTrackFaces,
                                       framePresentation.submittedCarFaces,
                                       trackTelemetryView);
        GameLoopObservabilityDomain::TrackRenderSh2PresentationPacket trackSh2Presentation{};
        if (GameLoopRuntime::TryBuildTrackRenderSh2PresentationPacket(
                framePresentation.sh2,
                trackTelemetryView,
                kEnablePhysicsSafeTelemetry,
                static_cast<uint32_t>(simState_.slaveDispatchCount),
                static_cast<uint32_t>(simState_.slaveDispatchSkipsTrackBusy),
                trackSh2Presentation))
        {
            GameLoopObservabilityDomain::PresentTrackRenderSh2PresentationPacket(
                trackSh2Presentation);
        }
        (void)GameLoopObservabilityDomain::TryPresentTrackReuseObservabilityDebugBundle(
            trackReuseState_);
    }

    void SynchronizeFrameCore()
    {
        if (context_.EnableManualGouraudCopy())
        {
            SRL::Scene3D::LightCopyGouraudTable();
        }
        if (context_.VerboseFrameLogs())
        {
            SRL::Debug::Print(1, 15, "SRL::Core::Synchronize frame:%u", frameCounter_);
        }
        hwrStageTrace_.preSync =
            GameLoopRuntime::MaybeCaptureHighWorkRamSnapshot<kEnableDetailedWorkRamTelemetry>();
        lwrStageTrace_.preSync =
            GameLoopRuntime::MaybeCaptureLowWorkRamSnapshot<kEnableDetailedWorkRamTelemetry>();
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Sync);
        AppState::Set(AppState::Stage::LoopSync, frameCounter_);
#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp syncTicksStart = CaptureMasterPhaseStamp();
#endif
        SRL::Core::Synchronize();
#if MASTER_FRAME_PHASE_TELEMETRY
        const FramePhaseTelemetry::Stamp syncTicksEnd = CaptureMasterPhaseStamp();
        masterFrameTiming_.syncTicks =
            Sh2FrtProfiler::Elapsed(syncTicksStart.frt, syncTicksEnd.frt);
        RecordMasterPhase(FramePhaseTelemetry::Phase::Synchronize,
                          syncTicksStart,
                          syncTicksEnd);
#endif
        hwrStageTrace_.postSync =
            GameLoopRuntime::MaybeCaptureHighWorkRamSnapshot<kEnableDetailedWorkRamTelemetry>();
        lwrStageTrace_.postSync =
            GameLoopRuntime::MaybeCaptureLowWorkRamSnapshot<kEnableDetailedWorkRamTelemetry>(true);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Unknown);
    }

    void UpdateFrameEndOverlays()
    {
        // A failed initial belt load otherwise becomes a silent black scene:
        // the sky is deliberately deferred until the track is ready and boot
        // messages are replaced by the per-frame HUD. Keep this one line alive
        // only on failure so the next screenshot identifies catalog vs package
        // failure and whether a Work RAM pool was exhausted.
        if (context_.RenderTrack() && !context_.TrackSystemReady())
        {
            const auto hwr = SRL::Memory::HighWorkRam::GetReport();
            const auto lwr = SRL::Memory::LowWorkRam::GetReport();
            const auto cart = SRL::Memory::CartRam::GetReport();
            SRL::Debug::Print(0, 4, "TRK FAIL cat:%u H:%u L:%u C:%u",
                              context_.trackSystem
                                  ? static_cast<unsigned>(context_.trackSystem->SegmentCount())
                                  : 0u,
                              static_cast<unsigned>(hwr.FreeSize / 1024u),
                              static_cast<unsigned>(lwr.FreeSize / 1024u),
                              static_cast<unsigned>(cart.FreeSize / 1024u));
        }
        const Game::CarSystem::DrivetrainDebugSnapshot drivetrain =
            GameLoopOverlayDomain::BuildExtendedDrivetrainOverlaySnapshot(
                (context_.carSystem && context_.carSystem->get()) ? context_.carSystem->get() : nullptr);
        const auto drivingHud = GameLoopRuntime::BuildDrivingHudTextPacket(drivetrain);
        // Stabilization A/B: one compact DRV line (~4 Hz) replaces KM/H + SHIFT spam.
        // th=throttle, g=gear, m=groundMask, km=speed — diagnoses GEAR:N vs plant miss.
        static uint8_t s_drvCadence = 0u;
        if (++s_drvCadence >= 15u)
        {
            s_drvCadence = 0u;
            const Game::CarSystem* car =
                (context_.carSystem && context_.carSystem->get()) ? context_.carSystem->get() : nullptr;
            const uint8_t groundMask = car ? car->RuntimeDebug().groundMask : 0u;
            SRL::Debug::Print(0, 12, "DRV th:%d g:%c m:%02X km:%d   ",
                              static_cast<int>(drivetrain.throttle),
                              drivetrain.gearChar,
                              static_cast<unsigned>(groundMask),
                              static_cast<int>(drivetrain.speedKmh));
        }
        GameLoopRuntime::PresentDrivingHudShiftTextPacket(drivingHud);

        if (context_.EnableRuntimeStatsLogs() || context_.EnableMinimalFpsOverlay())
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
                GameLoopMemoryPresentationDomain::PresentMemoryDebugPresentationBundle(
                    GameLoopMemoryPresentationDomain::BuildFrameEndMemoryDebugPresentationBundle(
                        hwrStageTrace_,
                        lwrStageTrace_,
                        context_.trackSystem));
            }
        }
        UpdateLowWorkFreeOverlayEnabled<>();
    }

    void PrintSegmentOverlapDiagnostics(
        uint32_t submittedTrackFaces,
        uint32_t submittedCarFaces,
        const GameLoopRuntime::TrackRenderTelemetryViewPacket& trackTelemetryView)
    {
        OverlayDiagnosticsSnapshot overlay{};
        GameLoopOverlayDomain::SegmentSnapshotAssemblyInputs overlayInputs{};
        overlayInputs.ports.track =
            (context_.TrackSystemReady() && context_.trackSystem) ? context_.trackSystem : nullptr;
        overlayInputs.ports.car = (context_.carSystem && context_.carSystem->get())
            ? context_.carSystem->get()
            : nullptr;
        overlayInputs.frame.latestActiveSegmentId = latestActiveSegmentId_;
        overlayInputs.frame.carYawDeg = carYawDeg_;
        overlayInputs.frame.carWorldPosition = context_.carWorldPosition;
        overlayInputs.frame.trackSegOffset = context_.trackSegOffset;
        overlayInputs.frame.cameraLocation = lastValidCameraLocation_;
        overlayInputs.frame.cameraLookTarget = lastValidLookTarget_;
        const Game::CarSystem::RuntimeDebugSnapshot carDebug = CarRuntimeDebugSnapshot();
        const Game::CarSystem::GameplayInputSnapshot input =
            (context_.carSystem && context_.carSystem->get())
                ? context_.carSystem->get()->LastGameplayInput()
                : Game::CarSystem::GameplayInputSnapshot{};
        if (!GameLoopOverlayDomain::BuildOverlayDiagnosticsSnapshot(overlayInputs,
                                                                    carDebug,
                                                                    input,
                                                                    submittedTrackFaces,
                                                                    submittedCarFaces,
                                                                    &trackTelemetryView,
                                                                    overlay))
        {
            return;
        }

        GameLoopObservabilityDomain::OverlayPacketFlow overlayFlow{};
        overlayFlow.valid = true;
        overlayFlow.segment.valid = true;
        overlayFlow.segment.snapshot = overlay.segment;
        overlayFlow.diagnostics.valid = true;
        overlayFlow.diagnostics.submittedTrackFaces = submittedTrackFaces;
        overlayFlow.diagnostics.submittedCarFaces = submittedCarFaces;
        overlayFlow.diagnostics.carDebug = overlay.carDebug;
        overlayFlow.diagnostics.input = overlay.input;
        overlayFlow.diagnostics.snapshot = overlay;

        const OverlayDiagnosticsSnapshot& overlaySnapshot = overlayFlow.diagnostics.snapshot;
        const SegmentOverlaySnapshot& segment = overlayFlow.segment.snapshot;

        GameLoopRuntime::PrintSegmentWindowOverlay(segment);
        GameLoopRuntime::PresentSegmentSpatialOverlay(overlaySnapshot, carYawDeg_);
        GameLoopRuntime::PresentShadowSpatialOverlay(shadowDebug_);
        GameLoopRuntime::PresentFaceAndShadowOverlay(
            overlaySnapshot,
            context_.SbaLoaded(),
            context_.RenderCarShadowModel() && context_.carShadowRenderer,
            context_.sbaMeshCount,
            context_.sbaFaceCount);
        if constexpr (kEnablePhysicsSafeTelemetry)
        {
            GameLoopRuntime::PresentPhysicsQueryOverlay(overlaySnapshot);
        }
        constexpr bool kEnableExtendedDrivetrainOverlay = true;
        if constexpr (kEnableExtendedDrivetrainOverlay)
        {
            GameLoopRuntime::PresentExtendedDrivetrainOverlay(
                GameLoopOverlayDomain::BuildExtendedDrivetrainOverlaySnapshot(
                    (context_.carSystem && context_.carSystem->get()) ? context_.carSystem->get() : nullptr));
        }
        GameLoopRuntime::PrintInputOverlay(overlaySnapshot);
        GameLoopRuntime::PresentSegmentEventOverlay(
            overlayEventState_,
            segment,
            kEnablePhysicsSafeTelemetry);
        overlayEventState_.Update(segment);
        // Query cost, same row as the old TC line, every 15 frames.
        // g = ground faces, wf = wall faces, ws = wall segments,
        // wh = wall hits/calls (all values are from the last frame).
        if (context_.trackSystem)
        {
            static uint8_t s_queryCadence = 0u;
            if (++s_queryCadence >= 15u)
            {
                s_queryCadence = 0u;
                SRL::Debug::Print(0, 11, "Q g:%u wf:%u ws:%u wh:%u/%u",
                                  context_.trackSystem->SurfaceQueryFacesScannedThisFrame(),
                                  context_.trackSystem->WallQueryFacesScannedThisFrame(),
                                  context_.trackSystem->WallQuerySegmentsScannedThisFrame(),
                                  context_.trackSystem->WallQueryHitsThisFrame(),
                                  context_.trackSystem->WallQueryCallsThisFrame());
            }
        }
        // Wheel ground distances last: must not be painted over by input/event rows.
        GameLoopRuntime::PresentGroundProbeOverlay(overlaySnapshot);
        // This row is deliberately written after the generic HUD presenters:
        // it replaces the static wheel-id banner while runtime statistics are
        // enabled, so a wall miss/hit remains visible in captured videos.
        if (context_.trackSystem)
        {
            SRL::Debug::Print(1, 20, "W%u h%u s%d p%d,%d q%u/%u",
                              static_cast<unsigned>(context_.trackSystem->TrackCollisionMapReady() ? 1u : 0u),
                              static_cast<unsigned>(overlaySnapshot.carDebug.WallHit() ? 1u : 0u),
                              static_cast<int>(overlaySnapshot.carDebug.wallSegmentId),
                              static_cast<int>(overlaySnapshot.carDebug.wallPushX),
                              static_cast<int>(overlaySnapshot.carDebug.wallPushZ),
                              static_cast<unsigned>(context_.trackSystem->WallQueryHitsThisFrame()),
                              static_cast<unsigned>(context_.trackSystem->WallQueryCallsThisFrame()));
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
        if (!fpsState_.VblankValid())
        {
            fpsState_.SetVblankValid(true);
            fpsState_.lastVblank = vblankNow;
            return;
        }

        uint32_t vblankDelta = vblankNow - fpsState_.lastVblank;
        fpsState_.lastVblank = vblankNow;
        if (vblankDelta == 0u)
        {
            vblankDelta = 1u;
        }

        ++fpsState_.sampleFrames;
        fpsState_.sampleVblanks += static_cast<uint16_t>(vblankDelta);
        // Texture recycle, same 60-frame window as FPS. Not the per-frame BLT rows.
        if (context_.trackSystem)
        {
            const TrackSystem::TextureRecycleHudSample recycle =
                context_.trackSystem->TextureRecycleHudSampleThisFrame();
            auto satAdd = [](uint16_t& slot, uint16_t add)
            {
                const uint32_t sum = static_cast<uint32_t>(slot) + static_cast<uint32_t>(add);
                slot = static_cast<uint16_t>(sum > 99u ? 99u : sum);
            };
            satAdd(recycleHud_.fresh32, recycle.fresh32);
            satAdd(recycleHud_.fresh64, recycle.fresh64);
            satAdd(recycleHud_.reuse32, recycle.reuse32);
            satAdd(recycleHud_.reuse64, recycle.reuse64);
            satAdd(recycleHud_.noReuse32, recycle.noReuse32);
            satAdd(recycleHud_.noReuse64, recycle.noReuse64);
            recycleHud_.reusableSlots = recycle.reusableSlots;
            recycleHud_.vdp1FreeKiB = recycle.vdp1FreeKiB;
            if (simState_.slaveLastJobTicksThisFrame > recycleHud_.slaveTicks)
            {
                recycleHud_.slaveTicks = simState_.slaveLastJobTicksThisFrame;
            }
            if (recycle.masterDrawTicks > recycleHud_.masterTicks)
            {
                recycleHud_.masterTicks = recycle.masterDrawTicks;
            }
        }
        const uint32_t frameTimeX100 = static_cast<uint32_t>(
            (static_cast<uint64_t>(vblankDelta) * 100000u + (kDisplayRefreshHz / 2u)) /
            static_cast<uint64_t>(kDisplayRefreshHz));
        constexpr uint32_t kTarget30FrameTimeX100 = 100000u / 30u;
        constexpr uint32_t kTarget60FrameTimeX100 = 100000u / 60u;
        if (frameTimeX100 > kTarget30FrameTimeX100)
        {
            ++fpsState_.framesOver30Budget;
        }
        if (frameTimeX100 > kTarget60FrameTimeX100)
        {
            ++fpsState_.framesOver60Budget;
        }
        if (fpsState_.sampleVblanks == 0u)
        {
            return;
        }

        RealtimeFpsMetricsSnapshot metrics{};
        const uint64_t fpsNum = static_cast<uint64_t>(kDisplayRefreshHz) *
                                static_cast<uint64_t>(10u) *
                                static_cast<uint64_t>(fpsState_.sampleFrames);
        metrics.fpsX10 = static_cast<uint16_t>(
            (fpsNum + static_cast<uint64_t>(fpsState_.sampleVblanks / 2u)) /
            static_cast<uint64_t>(fpsState_.sampleVblanks));

        const uint64_t frameMsNum =
            static_cast<uint64_t>(10000u) * static_cast<uint64_t>(fpsState_.sampleVblanks);
        const uint64_t frameMsDen = static_cast<uint64_t>(kDisplayRefreshHz) *
                                    static_cast<uint64_t>(fpsState_.sampleFrames);
        metrics.frameMsX10 = static_cast<uint16_t>(
            (frameMsNum + (frameMsDen / 2u)) / std::max<uint64_t>(1u, frameMsDen));

        const uint64_t vbNum =
            static_cast<uint64_t>(fpsState_.sampleVblanks) * static_cast<uint64_t>(100u);
        metrics.vbX100 = static_cast<uint16_t>(
            (vbNum + static_cast<uint64_t>(fpsState_.sampleFrames / 2u)) /
            static_cast<uint64_t>(fpsState_.sampleFrames));

        metrics.drop30Pct = (fpsState_.sampleFrames > 0u)
            ? static_cast<uint8_t>((static_cast<uint64_t>(fpsState_.framesOver30Budget) * 100u) /
                                   static_cast<uint64_t>(fpsState_.sampleFrames))
            : 0u;
        metrics.drop60Pct = (fpsState_.sampleFrames > 0u)
            ? static_cast<uint8_t>((static_cast<uint64_t>(fpsState_.framesOver60Budget) * 100u) /
                                   static_cast<uint64_t>(fpsState_.sampleFrames))
            : 0u;

        // Fase 1 FPS: print once per sample window (~1 Hz at 60 fps), not every frame.
        constexpr uint32_t kSampleWindowFrames = 60u;
        if (fpsState_.sampleFrames >= kSampleWindowFrames)
        {
            SRL::Debug::Print(0, 16, "FPS:%u.%u ms:%u.%u vb:%u.%02u d30:%u%% d60:%u%%",
                              static_cast<unsigned>(metrics.fpsX10 / 10u),
                              static_cast<unsigned>(metrics.fpsX10 % 10u),
                              static_cast<unsigned>(metrics.frameMsX10 / 10u),
                              static_cast<unsigned>(metrics.frameMsX10 % 10u),
                              static_cast<unsigned>(metrics.vbX100 / 100u),
                              static_cast<unsigned>(metrics.vbX100 % 100u),
                              static_cast<unsigned>(metrics.drop30Pct),
                              static_cast<unsigned>(metrics.drop60Pct));
            // Row 15, once per FPS window. Width stays inside the 45-column print.
            // f/r/n are 32/64 sums capped at 99. q is the reusable pool. k is VDP1
            // free KiB. s/m are peak slave-physics and master-draw milliseconds.
            auto cap99 = [](uint16_t value) -> unsigned
            {
                return static_cast<unsigned>(value > 99u ? 99u : value);
            };
            auto frtTicksToMs = [](uint16_t ticks) -> unsigned
            {
                // TIM_CKS_128 at 28.636 MHz is about 224 ticks per millisecond.
                constexpr uint32_t kTicksPerMs = 224u;
                const uint32_t ms =
                    (static_cast<uint32_t>(ticks) + (kTicksPerMs / 2u)) / kTicksPerMs;
                return static_cast<unsigned>(ms > 99u ? 99u : ms);
            };
            const unsigned reusableSlots = recycleHud_.reusableSlots > 999u
                ? 999u
                : static_cast<unsigned>(recycleHud_.reusableSlots);
            const unsigned vdp1FreeKiB = recycleHud_.vdp1FreeKiB > 999u
                ? 999u
                : static_cast<unsigned>(recycleHud_.vdp1FreeKiB);
            SRL::Debug::Print(0, 15, "V f%2u/%2u r%2u/%2u n%2u/%2u q%3u k%3u s%2u m%2u",
                              cap99(recycleHud_.fresh32),
                              cap99(recycleHud_.fresh64),
                              cap99(recycleHud_.reuse32),
                              cap99(recycleHud_.reuse64),
                              cap99(recycleHud_.noReuse32),
                              cap99(recycleHud_.noReuse64),
                              reusableSlots,
                              vdp1FreeKiB,
                              frtTicksToMs(recycleHud_.slaveTicks),
                              frtTicksToMs(recycleHud_.masterTicks));
#if MASTER_FRAME_PHASE_TELEMETRY
            // Missing-time split over the same 60-frame window:
            // i=input/job consume, b=frame begin, r=full render envelope,
            // f=finish preparation, y=Synchronize, u=residual, a=pose age, z=cull.
            SRL::Debug::Print(0, 14, "M i%2u b%2u r%2u f%2u y%2u u%2u a%u z%u",
                              frtTicksToMs(masterFrameTimingPeaks_.inputTicks),
                              frtTicksToMs(masterFrameTimingPeaks_.beginTicks),
                              frtTicksToMs(masterFrameTimingPeaks_.renderTicks),
                              frtTicksToMs(masterFrameTimingPeaks_.finishPrepTicks),
                              frtTicksToMs(masterFrameTimingPeaks_.syncTicks),
                              frtTicksToMs(masterFrameTimingPeaks_.unaccountedTicks),
                              static_cast<unsigned>(masterFrameTimingPeaks_.poseAgeFrames),
                              static_cast<unsigned>(masterFrameTimingPeaks_.culledSegments));
            // Previously hidden Master-side work. ca=camera, ts=track stream,
            // td=track draw, te=TrackSystem::EndFrame, tr=outer track envelope.
            SRL::Debug::Print(0, 17, "P ca%2u ts%2u td%2u te%2u tr%2u",
                              frtTicksToMs(masterFrameTimingPeaks_.cameraTicks),
                              frtTicksToMs(masterFrameTimingPeaks_.trackStreamTicks),
                              frtTicksToMs(masterFrameTimingPeaks_.trackDrawTicks),
                              frtTicksToMs(masterFrameTimingPeaks_.trackEndTicks),
                              frtTicksToMs(masterFrameTimingPeaks_.trackRenderTicks));
            // Most recent long stall plus its track context. This directly
            // reveals whether a stall coincided with a belt slide/recycle.
            const FramePhaseTelemetry::StallEvent& stall = lastLongFrameContext_.stall;
            const unsigned stallVblanks = stall.vblanks > 999u
                ? 999u
                : static_cast<unsigned>(stall.vblanks);
            const unsigned stallEvents = framePhaseTelemetry_.eventCount > 999u
                ? 999u
                : static_cast<unsigned>(framePhaseTelemetry_.eventCount);
            SRL::Debug::Print(0, 18, "L%s v%u e%u s%d x%u b%u p%u r%u",
                              stall.valid ? FramePhaseTelemetry::PhaseCode(stall.phase) : "--",
                              stallVblanks,
                              stallEvents,
                              lastLongFrameContext_.valid
                                  ? static_cast<int>(lastLongFrameContext_.segmentId)
                                  : -1,
                              static_cast<unsigned>(lastLongFrameContext_.slides),
                              static_cast<unsigned>(lastLongFrameContext_.slidePrepares),
                              static_cast<unsigned>(lastLongFrameContext_.prefetchBuilds),
                              static_cast<unsigned>(lastLongFrameContext_.textureReuses));
            recycleHud_ = {};
            masterFrameTimingPeaks_ = {};
            FramePhaseTelemetry::ResetWindowPeaks(framePhaseTelemetry_);
#else
            recycleHud_ = {};
#endif
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
        if (autoLapRoute_.StartupYawAligned() ||
            AutoLapTestEnabled() ||
            !AutoLapRouteDomain::HasYawAtIndex(autoLapRoute_, nearestIndex))
        {
            return;
        }

        carYawDeg_ = NormalizeYawDeg360(static_cast<int32_t>(autoLapRoute_.yawDeg[nearestIndex]));
        SyncCameraHeadingFromCar();
        AutoLapRouteDomain::MarkAutoLapStartupYawAligned(autoLapRoute_);
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
        if (!cameraPathRuntime_.PrevCarWorldPositionValid())
        {
            cameraPathRuntime_.prevCarWorldPosition = context_.carWorldPosition;
            cameraPathRuntime_.SetPrevCarWorldPositionValid(true);
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

    bool ShouldDisableCameraPathGuidance() const
    {
        return !CameraSystem::kPathGuidedChaseEnabled;
    }

    void ResetCameraPathRuntimeIfNeeded()
    {
        if (!AutoLapTestEnabled() && AutoLapRouteDomain::HasRetainedAutoLapRouteStorage(autoLapRoute_))
        {
            AutoLapRouteDomain::ReleaseAutoLapRouteStorage(autoLapRoute_);
            ClearAutoLapNpcRailRoutes();
        }
        cameraPathRuntime_.SetPrevCarWorldPositionValid(false);
    }

    bool CanBuildCameraPathFrameContext() const
    {
        return context_.trackSystem && context_.TrackSystemReady();
    }

    bool EnsureCameraPathRouteBuilt()
    {
        if (!autoLapRoute_.Built())
        {
            BuildAutoLapRoute(context_, context_.carWorldPosition);
        }
        return autoLapRoute_.centers.size() >= 2u;
    }

    bool TryResolveCameraPathRouteIndices(CameraPathRouteIndices& out) const
    {
        if (!AutoLapRouteDomain::FindNearestRoutePointIndex(
                autoLapRoute_, context_.carWorldPosition, out.nearest))
        {
            return false;
        }

        const size_t routeCount = autoLapRoute_.centers.size();
        out.prev = (out.nearest + routeCount - 1u) % routeCount;
        out.next = (out.nearest + 1u) % routeCount;
        return true;
    }

    void BuildCameraPathFrameContext(CameraSystem::PathFrameContext& out,
                                     const CameraPathRouteIndices& routeIndices,
                                     const SRL::Math::Types::Vector3D& fallbackForward)
    {
        AlignCameraPathStartupYawIfNeeded(routeIndices.nearest);
        PopulateCameraPathGeometryContext(out,
                                          routeIndices.prev,
                                          routeIndices.nearest,
                                          routeIndices.next,
                                          fallbackForward);
        PopulateCameraPathSpeedContext(out);
        out.valid = true;
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

        if (ShouldDisableCameraPathGuidance())
        {
            ResetCameraPathRuntimeIfNeeded();
            PublishEmptyCameraPathFrameContext();
            return;
        }

        if (!CanBuildCameraPathFrameContext())
        {
            PublishEmptyCameraPathFrameContext();
            return;
        }

        if (!EnsureCameraPathRouteBuilt())
        {
            PublishEmptyCameraPathFrameContext();
            return;
        }

        CameraPathRouteIndices routeIndices{};
        if (!TryResolveCameraPathRouteIndices(routeIndices))
        {
            PublishEmptyCameraPathFrameContext();
            return;
        }

        BuildCameraPathFrameContext(pathCtx, routeIndices, fallbackForward);
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

    bool EnsureAutoLapRouteReady(const Context& context,
                                 const SRL::Math::Types::Vector3D& referenceCarWorldPosition)
    {
        if (!autoLapRoute_.Built())
        {
            BuildAutoLapRoute(context, referenceCarWorldPosition);
        }
        if (!AutoLapRouteDomain::HasMinimumRoutePoints(autoLapRoute_))
        {
            return false;
        }
        if (AutoLapRouteDomain::NeedsYawRebuild(autoLapRoute_))
        {
            RebuildAutoLapRouteYawData();
        }
        return true;
    }

    bool InitializeAutoLapRouteIfNeeded(const Context& context,
                                        SRL::Math::Types::Vector3D& ioCarWorldPosition,
                                        int32_t& ioCarYawDeg,
                                        const SRL::Math::Types::Fxp& rideHeight,
                                        int32_t segmentCount)
    {
        if (AutoLapRouteDomain::IsRouteInitialized(autoLapRoute_))
        {
            return true;
        }

        size_t bestIdx = 0u;
        if (!AutoLapRouteDomain::FindNearestRoutePointIndex(
                autoLapRoute_, ioCarWorldPosition, bestIdx))
        {
            return false;
        }

        autoLapRoute_.index = static_cast<uint16_t>(bestIdx);
        ioCarWorldPosition.X = autoLapRoute_.centers[autoLapRoute_.index].X;
        ioCarWorldPosition.Z = autoLapRoute_.centers[autoLapRoute_.index].Z;
        ioCarWorldPosition.Y = AutoLapRouteDomain::ResolveRouteGroundYAt(
                                   autoLapRoute_,
                                   *context.trackSystem,
                                   context.trackSegOffset,
                                   autoLapRoute_.index,
                                   ioCarWorldPosition.Y) +
                               rideHeight;
        AutoLapRouteDomain::MarkAutoLapInitialized(autoLapRoute_);
        if (AutoLapRouteDomain::CanApplyYawAtCurrentIndex(autoLapRoute_))
        {
            ioCarYawDeg = autoLapRoute_.yawDeg[autoLapRoute_.index];
        }
        AdvanceAutoLapObservedSegmentIfAvailable(segmentCount);
        return true;
    }

    void AdvanceAutoLapPlanarPosition(const SRL::Math::Types::Vector3D& nextCenter,
                                      SRL::Math::Types::Vector3D& ioCarWorldPosition) const
    {
        const int32_t ndx = nextCenter.X.RawValue() - ioCarWorldPosition.X.RawValue();
        const int32_t ndz = nextCenter.Z.RawValue() - ioCarWorldPosition.Z.RawValue();
        const int32_t nAdx = (ndx < 0) ? -ndx : ndx;
        const int32_t nAdz = (ndz < 0) ? -ndz : ndz;
        const int32_t maxAxis = (nAdx > nAdz) ? nAdx : nAdz;
        if (maxAxis <= 0)
        {
            return;
        }

        const int32_t stepRaw = (autoLapStepUnits_ << 16);
        const int64_t moveX64 = (static_cast<int64_t>(ndx) * static_cast<int64_t>(stepRaw)) / maxAxis;
        const int64_t moveZ64 = (static_cast<int64_t>(ndz) * static_cast<int64_t>(stepRaw)) / maxAxis;
        ioCarWorldPosition.X += SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(moveX64));
        ioCarWorldPosition.Z += SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(moveZ64));
    }

    void AdvanceAutoLapVerticalPosition(const SRL::Math::Types::Vector3D& currentCenter,
                                        const SRL::Math::Types::Vector3D& nextCenter,
                                        const SRL::Math::Types::Fxp& currentGroundY,
                                        const SRL::Math::Types::Fxp& nextGroundY,
                                        const SRL::Math::Types::Fxp& rideHeight,
                                        SRL::Math::Types::Vector3D& ioCarWorldPosition) const
    {
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
            return;
        }

        ioCarWorldPosition.Y = nextGroundY + rideHeight;
    }

    void AdvanceAutoLapWaypointWindow(const Context& context,
                                      const SRL::Math::Types::Fxp& rideHeight,
                                      size_t routePointCount,
                                      size_t& ioCurrentIndex,
                                      size_t& ioNextIndex,
                                      SRL::Math::Types::Vector3D& ioCarWorldPosition)
    {
        for (size_t guard = 0u; guard < 8u; ++guard)
        {
            if (!AutoLapRouteDomain::HasReachedOrPassedWaypoint(
                    autoLapRoute_, ioCurrentIndex, ioNextIndex, ioCarWorldPosition)) break;
            ioCurrentIndex = ioNextIndex;
            ioNextIndex = (ioCurrentIndex + 1u) % routePointCount;
            ioCarWorldPosition.Y = AutoLapRouteDomain::ResolveRouteGroundYAt(
                                       autoLapRoute_,
                                       *context.trackSystem,
                                       context.trackSegOffset,
                                       ioCurrentIndex,
                                       ioCarWorldPosition.Y) +
                                   rideHeight;
        }
    }

    void AdvanceAutoLapObservedSegmentIfAvailable(int32_t segmentCount)
    {
        if (AutoLapRouteDomain::HasObservedSegmentAtCurrentIndex(autoLapRoute_))
        {
            AutoLapRouteDomain::AdvanceObservedSegmentToward(
                segmentCount,
                autoLapRoute_.ids[autoLapRoute_.index],
                latestActiveSegmentId_);
        }
    }

    // Advance car through the preferred route. When PATH.NYA is available and
    // populated, the middle line drives the player route; otherwise we fall
    // back to the segment-center path.
    void UpdateAutoLapRoute(const Context& context,
                            SRL::Math::Types::Vector3D& ioCarWorldPosition,
                            int32_t& ioCarYawDeg)
    {
        if (!context.trackSystem || !context.TrackSystemReady()) return;
        if (!EnsureAutoLapRouteReady(context, ioCarWorldPosition)) return;
        const auto rideHeight = SRL::Math::Types::Fxp::BuildRaw(-(1 << 13));
        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());
        if (!InitializeAutoLapRouteIfNeeded(context,
                                            ioCarWorldPosition,
                                            ioCarYawDeg,
                                            rideHeight,
                                            segmentCount)) return;

        const size_t routePointCount = autoLapRoute_.centers.size();
        size_t currentIndex = static_cast<size_t>(autoLapRoute_.index);
        size_t nextIndex = (currentIndex + 1u) % routePointCount;
        const SRL::Math::Types::Vector3D& currentCenter = autoLapRoute_.centers[currentIndex];
        const SRL::Math::Types::Vector3D& nextCenter = autoLapRoute_.centers[nextIndex];
        const SRL::Math::Types::Fxp currentGroundY = AutoLapRouteDomain::ResolveRouteGroundYAt(
            autoLapRoute_,
            *context.trackSystem,
            context.trackSegOffset,
            currentIndex,
            ioCarWorldPosition.Y);
        const SRL::Math::Types::Fxp nextGroundY = AutoLapRouteDomain::ResolveRouteGroundYAt(
            autoLapRoute_,
            *context.trackSystem,
            context.trackSegOffset,
            nextIndex,
            ioCarWorldPosition.Y);
        AdvanceAutoLapPlanarPosition(nextCenter, ioCarWorldPosition);
        AdvanceAutoLapVerticalPosition(currentCenter,
                                       nextCenter,
                                       currentGroundY,
                                       nextGroundY,
                                       rideHeight,
                                       ioCarWorldPosition);
        AdvanceAutoLapWaypointWindow(context,
                                     rideHeight,
                                     routePointCount,
                                     currentIndex,
                                     nextIndex,
                                     ioCarWorldPosition);
        autoLapRoute_.index = static_cast<uint16_t>(currentIndex);
        AdvanceAutoLapObservedSegmentIfAvailable(segmentCount);
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

    void LogAutoLapGuideLoadFailure(
        const AutoLapRouteDomain::AutoLapGuideLoadPacket& guideLoad) const
    {
        if constexpr (kEnableAutoPathLogs)
        {
            SRL::Debug::Print(1, 23, guideLoad.attempted ? "AUTO PATH read state" : "AUTO PATH read fail");
        }
    }

    void LogAutoLapGuideParseFailure(
        const AutoLapRouteDomain::AutoLapGuideLoadPacket& guideLoad) const
    {
        if constexpr (kEnableAutoPathLogs)
        {
            SRL::Debug::Print(1, 23, "AUTO PATH parse fail %s",
                              guideLoad.loadedCandidate ? guideLoad.loadedCandidate : "none");
            SRL::Debug::Print(1, 24, "AUTO PATH parse sz:%u",
                              static_cast<unsigned>(guideLoad.byteCount));
        }
    }

    void LogAutoLapGuideLoadSuccess(
        const AutoLapRouteDomain::AutoLapGuideLoadPacket& guideLoad) const
    {
        if constexpr (kEnableAutoPathLogs)
        {
            SRL::Debug::Print(1, 23, "AUTO PATH ok %s",
                              guideLoad.loadedCandidate ? guideLoad.loadedCandidate : "none");
            SRL::Debug::Print(1, 24, "AUTO PATH v:%u l0:%u l1:%u l2:%u",
                              static_cast<unsigned>(guideLoad.parsedVersion),
                              static_cast<unsigned>(guideLoad.parsedLinePointCounts[0]),
                              static_cast<unsigned>(guideLoad.parsedLinePointCounts[1]),
                              static_cast<unsigned>(guideLoad.parsedLinePointCounts[2]));
        }
    }

    bool LoadAutoLapGuideLines()
    {
        AutoLapRouteDomain::ClearAutoLapGuideLines(autoLapRoute_);

        AutoLapRouteDomain::GuideFileBytes bytes{};
        const char* loadedCandidate = nullptr;
        if (!AutoLapRouteDomain::TryLoadGuideBytes(bytes, loadedCandidate))
        {
            LogAutoLapGuideLoadFailure(
                AutoLapRouteDomain::BuildAutoLapGuideReadFailurePacket());
            return false;
        }

        PathNya::ParseResult parsed{};
        if (!AutoLapRouteDomain::TryParseGuideBytes(bytes, parsed))
        {
            LogAutoLapGuideParseFailure(
                AutoLapRouteDomain::BuildAutoLapGuideParseFailurePacket(
                    loadedCandidate,
                    static_cast<uint32_t>(bytes.size())));
            return false;
        }

        AutoLapRouteDomain::CopyParsedGuideLines(autoLapRoute_, parsed);
        LogAutoLapGuideLoadSuccess(
            AutoLapRouteDomain::BuildAutoLapGuideLoadSuccessPacket(
                loadedCandidate,
                static_cast<uint32_t>(bytes.size()),
                parsed));
        return true;
    }

    bool BuildAutoLapRouteFromPathGuide(const Context& context,
                                        const SRL::Math::Types::Vector3D& referenceCarWorldPosition)
    {
        if (!LoadAutoLapGuideLines()) return false;

        const bool autoLapEnabled = AutoLapTestEnabled();
        const int32_t nearestLineIndex = autoLapEnabled ? -1 :
            AutoLapRouteDomain::SelectBestGuideLineIndex(
                autoLapRoute_, context_.trackSegOffset, referenceCarWorldPosition);
        const int32_t selectedLineIndex = AutoLapRouteSelectionPolicy::Select(
            autoLapEnabled,
            AutoLapRouteDomain::HasGuideLineMinimumPoints(
                autoLapRoute_, AutoLapRouteSelectionPolicy::kMiddleGuideLineIndex),
            nearestLineIndex);
        if (selectedLineIndex < 0)
        {
            LogAutoLapGuideLineEmpty();
            return false;
        }
        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());
        const auto& routeLine = AutoLapRouteDomain::ResolveSelectedRouteLine(
            autoLapRoute_,
            selectedLineIndex,
            segmentCount);
        if (routeLine == nullptr) return false;
        if (!AutoLapRouteDomain::PopulateRouteFromGuideLine(
                autoLapRoute_,
                *context.trackSystem,
                context.trackSegOffset,
                *routeLine)) return false;
        const bool reversed = AutoLapRouteDomain::NormalizeRouteDirection(
            autoLapRoute_,
            segmentCount);
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        autoLapGuideReversed_ = reversed;
#endif
        // Guide validity includes the yaw/offset arrays. Build them before
        // testing validity, otherwise every parsed PATH falls back to catalog
        // centers and the physical pilot never sees the selected guide line.
        RebuildAutoLapRouteYawData();
        const auto build = AutoLapRouteDomain::BuildAutoLapRouteBuildPacket(
            autoLapRoute_,
            AutoLapRouteDomain::HasValidGuideBuildOutput(autoLapRoute_),
            true,
            reversed);
        const auto routeTrace = AutoLapRouteDomain::BuildAutoLapGuideRouteTrace(
            build.usedGuidePath,
            build.normalizedDirection,
            build.selectedGuideLine,
            static_cast<uint16_t>(routeLine->size()),
            build.routePointCount);
        LogAutoLapGuideRouteSelection(routeTrace);
        LogAutoLapGuideBuildResult(build);
        // Capture PATH lateral samples while all 3 raw guides still live in LWR.
        // Freeing mid before arm caused Resolve to compare side rails against
        // simplified player centers and yield dP:0/0 (autolap23).
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (build.valid)
        {
            CaptureNpcLateralSamplesFromGuides();
        }
#endif
        // No NPC side-effects that allocate here. Arming happens after the
        // player has stable ground + motion (safe F1 redesign).
        return build.valid;
    }

    void CaptureNpcLateralSamplesFromGuides()
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        for (size_t slot = 0; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            npcLatDxRaw_[slot] = 0;
            npcLatDzRaw_[slot] = 0;
            const int32_t lineIndex = AutoLapNpcRailPolicy::GuideLineIndexForNpcSlot(slot);
            if (lineIndex < 0) continue;
            if (!AutoLapRouteDomain::HasGuideLineMinimumPoints(
                    autoLapRoute_, static_cast<size_t>(lineIndex), 2u) ||
                !AutoLapRouteDomain::HasGuideLineMinimumPoints(autoLapRoute_, 1u, 2u))
            {
                continue;
            }
            const auto& side = autoLapRoute_.guideLines[static_cast<size_t>(lineIndex)];
            const auto& mid = autoLapRoute_.guideLines[1];
            // Sample near the player's current parametric index when possible.
            size_t idx = 0u;
            if (!autoLapRoute_.centers.empty() && mid.size() == side.size())
            {
                idx = static_cast<size_t>(autoLapRoute_.index) % side.size();
            }
            npcLatDxRaw_[slot] = side[idx].X.RawValue() - mid[idx].X.RawValue();
            npcLatDzRaw_[slot] = side[idx].Z.RawValue() - mid[idx].Z.RawValue();
        }
#endif
    }

    void ClearAutoLapNpcRailRoutes()
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (cameraFocusTransferPending_ && context_.trackSystem)
            context_.trackSystem->EndFocusTransfer(false);
        cameraFocusTransferPending_ = false;
        cameraFocusTransferNpc_ = false;
        cameraFocusTransferNeedsRelocate_ = false;
        SetCameraNpcFocus(false);
        cameraXHeldPrev_ = false;
        npcPublishedSimFrameValid_ = false;
        npcConsumedSimFrameValid_ = false;
        npcContactSimFrameValid_ = false;
        npcContactPrevValid_ = false;
        npcContactCooldown_ = 0u;
        pendingCarContactKickXRaw_ = 0;
        pendingCarContactKickZRaw_ = 0;
        playerTrafficState_ = {};
        playerPassState_ = {};
        playerStallState_ = {};
        playerRecoveryRetryCountdown_ = 0u;
        playerRecoveryAwaitingPublish_ = false;
        playerContactRecoveryFrames_ = 0u;
        playerContactSpeedLimitKmh_ = 30;
        for (size_t slot = 0; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            autoLapNpcDrivers_[slot].Clear();
            ReleaseNpcRailRoute(autoLapNpcRoutes_[slot]);
            NpcRailFollower::Reset(autoLapNpcFollowers_[slot]);
            autoLapNpcRailCounts_[slot] = 0u;
            npcDrawValid_[slot] = false;
            npcDrawYawDeg_[slot] = 0;
            npcScreenValid_[slot] = false;
            npcScreenX_[slot] = 0;
            npcScreenY_[slot] = 0;
            npcScreenDepthRaw_[slot] = 0;
            npcLatDxRaw_[slot] = 0;
            npcLatDzRaw_[slot] = 0;
        }
        autoLapNpcRailReadyCount_ = 0u;
        npcRailsArmed_ = false;
        npcStableFrames_ = 0u;
#endif
    }

    void LogAutoLapNpcRailsTelemetry() const
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        // nSide = NPC guide 0 count; nMid = player Auto Lap centers.
        const unsigned nSide = static_cast<unsigned>(autoLapNpcRailCounts_[0]);
        const unsigned nMid = static_cast<unsigned>(autoLapRoute_.centers.size());
        SRL::Debug::Print(1, 26, "AUTO RAILS s:%u m:%u", nSide, nMid);
        SRL::Debug::Print(1, 27, "AUTO NPC ready:%u",
                          static_cast<unsigned>(autoLapNpcRailReadyCount_));
#else
        (void)0;
#endif
    }

    bool ArmNpcRailsOnce()
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        // Mid ref for relative draw is autoLapRoute_.centers (already in LWR).
        if (autoLapRoute_.centers.size() < 2u)
        {
            SRL::Debug::Print(1, 25, "AUTO NPC mid fail");
            return false;
        }

        // Prefer resident LWR guideLines from the player PATH build — avoids a
        // second CD reload + PathLine std::vector peak on the tight HWR heap.
        bool usedResidentGuides = true;
        for (size_t slot = 0; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            const int32_t lineIndex = AutoLapNpcRailPolicy::GuideLineIndexForNpcSlot(slot);
            if (lineIndex < 0 ||
                !AutoLapRouteDomain::HasGuideLineMinimumPoints(
                    autoLapRoute_, static_cast<size_t>(lineIndex), 2u))
            {
                usedResidentGuides = false;
                break;
            }
        }

        PathNya::ParseResult parsed{};
        AutoLapRouteDomain::GuideFileBytes bytes{};
        if (!usedResidentGuides)
        {
            const char* loadedCandidate = nullptr;
            if (!AutoLapRouteDomain::TryLoadGuideBytes(bytes, loadedCandidate))
            {
                SRL::Debug::Print(1, 25, "AUTO NPC reload fail");
                return false;
            }
            if (!PathNya::Parse(bytes.data(), bytes.size(), parsed))
            {
                SRL::Debug::Print(1, 25, "AUTO NPC parse fail");
                return false;
            }
            // Drop file bytes before allocating HWR routes (parse owns copies).
            bytes.clear();
        }

        uint8_t ready = 0u;
        for (size_t slot = 0; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            autoLapNpcDrivers_[slot].Clear();
            NpcRailFollower::Reset(autoLapNpcFollowers_[slot]);
            ReleaseNpcRailRoute(autoLapNpcRoutes_[slot]);
            autoLapNpcRailCounts_[slot] = 0u;

            const int32_t lineIndex = AutoLapNpcRailPolicy::GuideLineIndexForNpcSlot(slot);
            if (lineIndex < 0) continue;

            bool built = false;
            if (usedResidentGuides)
            {
                // Swap-consume guide line (no second LWR allocate).
                built = PromoteGuideLineToNpcRail(
                    autoLapNpcRoutes_[slot],
                    autoLapRoute_.guideLines[static_cast<size_t>(lineIndex)],
                    lineIndex,
                    context_.trackSegOffset,
                    autoLapGuideReversed_);
            }
            else if (static_cast<size_t>(lineIndex) < parsed.lines.size() &&
                     parsed.lines[static_cast<size_t>(lineIndex)].size() >= 2u)
            {
                built = BuildNpcRailFromPathLine(
                    autoLapNpcRoutes_[slot],
                    parsed.lines[static_cast<size_t>(lineIndex)],
                    lineIndex,
                    context_.trackSegOffset,
                    autoLapGuideReversed_);
            }
            if (!built) continue;

            RebuildNpcRailYaw(autoLapNpcRoutes_[slot],
                               [this](int32_t dx, int32_t dz, int32_t fallback) {
                                   return YawFromDeltaRaw(dx, dz, fallback);
                               });
            // yawDeg allocate can fail under LWR pressure — drop the rail if empty.
            if (autoLapNpcRoutes_[slot].yawDeg.size() !=
                autoLapNpcRoutes_[slot].centers.size())
            {
                ReleaseNpcRailRoute(autoLapNpcRoutes_[slot]);
                NpcRailFollower::Reset(autoLapNpcFollowers_[slot]);
                continue;
            }

            // Same station as the player on this rail (mesh1 +8 ahead + faster
            // step made NPCs race ahead and corner out of sync with the master).
            size_t nearest = 0u;
            if (!FindNearestNpcRailPointIndex(
                    autoLapNpcRoutes_[slot], context_.carWorldPosition, nearest))
            {
                ReleaseNpcRailRoute(autoLapNpcRoutes_[slot]);
                continue;
            }
            const uint16_t startIndex = static_cast<uint16_t>(nearest);
            if (autoLapNpcDrivers_[slot].Bind(autoLapNpcFollowers_[slot],
                                              autoLapNpcRoutes_[slot],
                                              startIndex,
                                              context_.trackSystem,
                                              context_.trackSegOffset,
                                              latestActiveSegmentId_))
            {
                autoLapNpcRailCounts_[slot] = static_cast<uint16_t>(
                    autoLapNpcRoutes_[slot].centers.size());
                ++ready;
            }
            else
            {
                ReleaseNpcRailRoute(autoLapNpcRoutes_[slot]);
            }
        }

        // Fallback parse scratch no longer needed.
        if (!usedResidentGuides)
        {
            PathNya::Clear(parsed);
        }

        autoLapNpcRailReadyCount_ = ready;
        if (ready == 0u)
        {
            SRL::Debug::Print(1, 25, "AUTO NPC build fail");
            return false;
        }

        // If build-time lateral capture missed (guides already gone), sample now
        // against player centers before side guides are consumed.
        for (size_t slot = 0; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            if ((npcLatDxRaw_[slot] != 0 || npcLatDzRaw_[slot] != 0)) continue;
            if (!autoLapNpcRoutes_[slot].Built() ||
                autoLapNpcRoutes_[slot].centers.empty() ||
                autoLapRoute_.centers.empty()) continue;
            const size_t sideCount = autoLapNpcRoutes_[slot].centers.size();
            const size_t midCount = autoLapRoute_.centers.size();
            const size_t sideIndex = 0u;
            const size_t midIndex = (sideCount == midCount)
                ? sideIndex
                : ((sideIndex * midCount) / sideCount);
            npcLatDxRaw_[slot] =
                autoLapNpcRoutes_[slot].centers[sideIndex].X.RawValue() -
                autoLapRoute_.centers[midIndex].X.RawValue();
            npcLatDzRaw_[slot] =
                autoLapNpcRoutes_[slot].centers[sideIndex].Z.RawValue() -
                autoLapRoute_.centers[midIndex].Z.RawValue();
        }

        npcRailsArmed_ = true;
        npcContactPrevValid_ = false;
        LogAutoLapNpcRailsTelemetry();
        SRL::Debug::Print(
            1, 25, "AUTO NPC pose F1 dx:%d/%d",
            static_cast<int>(npcLatDxRaw_[0] >> 16),
            static_cast<int>(npcLatDzRaw_[0] >> 16));
        return true;
#else
        return false;
#endif
    }

    void TickAutoLapNpcRails()
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (!AutoLapPhysicsEnabled())
        {
            if (npcRailsArmed_ || autoLapNpcRailReadyCount_ != 0u)
            {
                ClearAutoLapNpcRailRoutes();
            }
            npcPublishedSimFrameValid_ = false;
            npcConsumedSimFrameValid_ = false;
            return;
        }

        if (!AutoLapNpcRailPolicy::ShouldAdvanceForSimulationFrame(
                npcPublishedSimFrameValid_, npcPublishedSimFrameId_,
                npcConsumedSimFrameValid_, npcConsumedSimFrameId_))
        {
            return;
        }
        npcConsumedSimFrameId_ = npcPublishedSimFrameId_;
        npcConsumedSimFrameValid_ = true;

        if (!npcRailsArmed_)
        {
            static constexpr uint8_t kNpcStableFramesRequired = 30u;
            const bool moving = lastAlAccel_ || lastAlSpeedKmh_ > 0;
            if (lastAlGround_ && moving)
            {
                if (npcStableFrames_ < 255u) ++npcStableFrames_;
            }
            else
            {
                npcStableFrames_ = 0u;
            }

            if (npcStableFrames_ >= kNpcStableFramesRequired)
            {
                if (!ArmNpcRailsOnce())
                {
                    // Retry later after another stable window.
                    npcStableFrames_ = 0u;
                }
            }
            return;
        }

        for (size_t slot = 0; slot < AutoLapNpcRailPolicy::kNpcRailCount; ++slot)
        {
            if (!autoLapNpcRoutes_[slot].Built()) continue;
            // Re-bind if a prior Clear/Step dropped Active but the route remains.
            if (!autoLapNpcFollowers_[slot].Active())
            {
                size_t nearest = 0u;
                if (FindNearestNpcRailPointIndex(
                        autoLapNpcRoutes_[slot], context_.carWorldPosition, nearest))
                {
                    autoLapNpcDrivers_[slot].Bind(
                        autoLapNpcFollowers_[slot], autoLapNpcRoutes_[slot],
                        static_cast<uint16_t>(nearest),
                        context_.trackSystem, context_.trackSegOffset,
                        latestActiveSegmentId_);
                }
            }
            if (!autoLapNpcFollowers_[slot].Active()) continue;

            // Independent car physics: PATH is only the steering target.
            // Never overwrite the resulting pose with a waypoint coordinate.
            const auto playerYaw = SRL::Math::Types::Angle::FromDegrees(
                SRL::Math::Types::Fxp::BuildRaw(
                    NormalizeYawDeg360(carYawDeg_) << 16));
            const auto* activeCar = ActiveCarSystem();
            const int16_t playerSpeedKmh = activeCar
                ? static_cast<int16_t>(activeCar->RuntimeDebug().speedKmh)
                : 0;
            const AutoLapTrafficPolicy::Vehicle opponent{
                context_.carWorldPosition.X.RawValue(),
                context_.carWorldPosition.Z.RawValue(),
                SRL::Math::Trigonometry::Sin(playerYaw).RawValue(),
                -SRL::Math::Trigonometry::Cos(playerYaw).RawValue(),
                playerSpeedKmh};
            const uint8_t previousRecoveries = autoLapNpcDrivers_[slot].RecoveryCount();
            autoLapNpcDrivers_[slot].Step(autoLapNpcFollowers_[slot],
                                          autoLapNpcRoutes_[slot], &opponent);
            if (slot == 0u && cameraNpcFocus_ && !cameraFocusTransferPending_ &&
                autoLapNpcDrivers_[slot].RecoveryCount() != previousRecoveries &&
                NpcFocusTargetValid() &&
                !SegmentInRenderWindow(autoLapNpcDrivers_[slot].StreamingSegmentId()))
            {
                // A validated respawn can jump beyond the normal belt guard.
                // Reuse the existing one-shot remote focus handoff only then.
                cameraFocusTransferPending_ = true;
                cameraFocusTransferNpc_ = true;
                cameraFocusTransferNeedsRelocate_ = true;
                context_.trackSystem->BeginFocusTransfer();
            }
        }

        if ((frameCounter_ & 7u) == 0u)
        {
            SRL::Debug::Print(
                1, 21, "N2 v:%d d:%d/%d g:%u s:%u m:%u r:%u",
                static_cast<int>(autoLapNpcDrivers_[0].Frame().carSpeedKmh),
                static_cast<int>(autoLapNpcDrivers_[0].Frame().debugNetDx),
                static_cast<int>(autoLapNpcDrivers_[0].Frame().debugNetDz),
                static_cast<unsigned>(autoLapNpcDrivers_[0].Frame().debugGroundSupport),
                static_cast<unsigned>(autoLapNpcDrivers_[0].StalledFrames()),
                static_cast<unsigned>(autoLapNpcDrivers_[0].MissingGroundFrames()),
                static_cast<unsigned>(autoLapNpcDrivers_[0].RecoveryCount()));
        }
#endif
    }

    void ResolveAutoLapCarContact()
    {
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
        if (!AutoLapPhysicsEnabled() || !npcRailsArmed_ ||
            !autoLapNpcFollowers_[0].Active() ||
            !context_.trackSystem ||
            !context_.trackSystem->TrackCollisionMapReady())
        {
            npcContactPrevValid_ = false;
            npcContactSimFrameValid_ = false;
            return;
        }
        // Never modify the published player pose while an older Slave result
        // is still in flight: that result would overwrite the correction.
        if (simState_.JobInFlight()) return;

        if (!AutoLapNpcRailPolicy::ShouldAdvanceForSimulationFrame(
                npcPublishedSimFrameValid_, npcPublishedSimFrameId_,
                npcContactSimFrameValid_, npcContactSimFrameId_)) return;
        npcContactSimFrameId_ = npcPublishedSimFrameId_;
        npcContactSimFrameValid_ = true;

        auto& npc = autoLapNpcFollowers_[0];
        const auto playerNow = context_.carWorldPosition;
        const auto npcNow = npc.pos;
        if (!npcContactPrevValid_)
        {
            prevContactPlayerPos_ = playerNow;
            prevContactNpcPos_ = npcNow;
            npcContactPrevValid_ = true;
            return;
        }
        if (npcContactCooldown_ > 0u) --npcContactCooldown_;

        using SRL::Math::Types::Angle;
        using SRL::Math::Types::Fxp;
        const Angle playerYaw = Angle::FromDegrees(Fxp::BuildRaw(carYawDeg_ * (1 << 16)));
        const Angle npcYaw = Angle::FromDegrees(Fxp::BuildRaw(
            static_cast<int32_t>(npc.yawDeg) * (1 << 16)));
        AutoLapCarContactPolicy::Pose playerPose{};
        playerPose.xRaw = playerNow.X.RawValue();
        playerPose.zRaw = playerNow.Z.RawValue();
        playerPose.forwardXRaw = SRL::Math::Trigonometry::Sin(playerYaw).RawValue();
        playerPose.forwardZRaw = -SRL::Math::Trigonometry::Cos(playerYaw).RawValue();
        AutoLapCarContactPolicy::Pose playerPrevPose = playerPose;
        playerPrevPose.xRaw = prevContactPlayerPos_.X.RawValue();
        playerPrevPose.zRaw = prevContactPlayerPos_.Z.RawValue();
        AutoLapCarContactPolicy::Pose npcPose{};
        npcPose.xRaw = npcNow.X.RawValue();
        npcPose.zRaw = npcNow.Z.RawValue();
        npcPose.forwardXRaw = SRL::Math::Trigonometry::Sin(npcYaw).RawValue();
        npcPose.forwardZRaw = -SRL::Math::Trigonometry::Cos(npcYaw).RawValue();
        AutoLapCarContactPolicy::Pose npcPrevPose = npcPose;
        npcPrevPose.xRaw = prevContactNpcPos_.X.RawValue();
        npcPrevPose.zRaw = prevContactNpcPos_.Z.RawValue();

        const int64_t yDifference = static_cast<int64_t>(playerNow.Y.RawValue()) -
            static_cast<int64_t>(npcNow.Y.RawValue());
        if (yDifference >= -(16 << 16) && yDifference <= (16 << 16))
        {
            const auto response = AutoLapCarContactPolicy::ResolveSwept(
                playerPrevPose, playerPose, npcPrevPose, npcPose,
                npcContactCooldown_ == 0u);
            if (response.contact)
            {
                static constexpr uint8_t kAsphalt = 1u;
                Fxp playerGroundY{};
                Fxp npcGroundY{};
                SRL::Math::Types::Vector3D playerCandidate = playerNow;
                SRL::Math::Types::Vector3D npcCandidate = npcNow;
                if (response.swept)
                {
                    playerCandidate.X = Fxp::BuildRaw(response.playerImpactXRaw);
                    playerCandidate.Z = Fxp::BuildRaw(response.playerImpactZRaw);
                    npcCandidate.X = Fxp::BuildRaw(response.npcImpactXRaw);
                    npcCandidate.Z = Fxp::BuildRaw(response.npcImpactZRaw);
                }
                const auto playerImpactBase = playerCandidate;
                const auto npcImpactBase = npcCandidate;
                playerCandidate.X += Fxp::BuildRaw(response.playerPushXRaw);
                playerCandidate.Z += Fxp::BuildRaw(response.playerPushZRaw);
                npcCandidate.X += Fxp::BuildRaw(response.npcPushXRaw);
                npcCandidate.Z += Fxp::BuildRaw(response.npcPushZRaw);
                bool playerAllowed = context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                    playerCandidate, context_.trackSegOffset, &kAsphalt, 1u,
                    playerGroundY, nullptr, latestActiveSegmentId_, false);

                const int32_t npcSeed = autoLapNpcDrivers_[0].Frame().activeSegmentId;
                bool npcAllowed = context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                    npcCandidate, context_.trackSegOffset, &kAsphalt, 1u,
                    npcGroundY, nullptr, npcSeed > 0 ? npcSeed : latestActiveSegmentId_, false);
                if (response.swept && (!playerAllowed || !npcAllowed))
                {
                    // A curb may reject the lateral push. Keep both cars at
                    // first contact instead of allowing them to cross.
                    playerCandidate = playerImpactBase;
                    npcCandidate = npcImpactBase;
                    playerAllowed = context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                        playerCandidate, context_.trackSegOffset, &kAsphalt, 1u,
                        playerGroundY, nullptr, latestActiveSegmentId_, false);
                    npcAllowed = context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                        npcCandidate, context_.trackSegOffset, &kAsphalt, 1u,
                        npcGroundY, nullptr, npcSeed > 0 ? npcSeed : latestActiveSegmentId_, false);
                }
                if (response.swept && (!playerAllowed || !npcAllowed))
                {
                    // The previous pair is the last known non-overlapping
                    // state. Both cars roll back together or neither does.
                    playerCandidate = prevContactPlayerPos_;
                    npcCandidate = prevContactNpcPos_;
                    playerAllowed = context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                        playerCandidate, context_.trackSegOffset, &kAsphalt, 1u,
                        playerGroundY, nullptr, latestActiveSegmentId_, false);
                    npcAllowed = context_.trackSystem->FindSurfaceYBySurfaceTypeSet(
                        npcCandidate, context_.trackSegOffset, &kAsphalt, 1u,
                        npcGroundY, nullptr, npcSeed > 0 ? npcSeed : latestActiveSegmentId_, false);
                }
                if (response.swept && (!playerAllowed || !npcAllowed))
                {
                    playerAllowed = false;
                    npcAllowed = false;
                }
                if (playerAllowed) context_.carWorldPosition = playerCandidate;
                if (npcAllowed)
                {
                    npc.pos = npcCandidate;
                    npc.pos.Y = npcGroundY + Game::CarPhysics::GetRideHeightOffset();
                }
                if (playerAllowed && npcAllowed)
                {
                    pendingCarContactKickXRaw_ = response.playerKickXRaw;
                    pendingCarContactKickZRaw_ = response.playerKickZRaw;
                    const int16_t playerSpeed = ActiveCarSystem()
                        ? ActiveCarSystem()->RuntimeDebug().speedKmh : 0;
                    const int16_t npcSpeed = autoLapNpcDrivers_[0].Frame().carSpeedKmh;
                    const auto contactLimits =
                        AutoLapCarContactPolicy::ContactSpeedLimits(
                            response, playerSpeed, npcSpeed);
                    autoLapNpcDrivers_[0].AddContactKick(
                        response.npcKickXRaw, response.npcKickZRaw,
                        contactLimits.npcKmh);
                    if (contactLimits.playerKmh >= 0)
                    {
                        playerContactRecoveryFrames_ = 8u;
                        playerContactSpeedLimitKmh_ = contactLimits.playerKmh;
                    }
                    else
                    {
                        playerContactRecoveryFrames_ = 0u;
                        playerTrafficState_ = {};
                    }
                    if (response.playerKickXRaw != 0 || response.playerKickZRaw != 0)
                    {
                        npcContactCooldown_ = 6u;
                    }
                    if (npcContactHits_ < 65535u) ++npcContactHits_;
                }
            }
        }
        prevContactPlayerPos_ = context_.carWorldPosition;
        prevContactNpcPos_ = npc.pos;
#endif
    }

    void LogAutoLapGuideLineEmpty() const
    {
        if constexpr (kEnableAutoPathLogs)
        {
            SRL::Debug::Print(1, 24, "AUTO PATH line empty");
        }
    }

    void LogAutoLapGuideRouteSelection(
        const AutoLapRouteDomain::AutoLapGuideRouteTrace& routeTrace) const
    {
        if constexpr (kEnableAutoPathLogs)
        {
            SRL::Debug::Print(1, 25, "AUTO PATH l:%d raw:%u out:%u",
                              static_cast<int>(routeTrace.selectedGuideLine),
                              static_cast<unsigned>(routeTrace.rawPointCount),
                              static_cast<unsigned>(routeTrace.outputPointCount));
        }
    }

    void LogAutoLapGuideBuildResult(
        const AutoLapRouteDomain::AutoLapRouteBuildPacket& build) const
    {
        if constexpr (kEnableAutoPathLogs)
        {
            SRL::Debug::Print(1, 26, build.normalizedDirection ? "AUTO PATH dir:REV fix" : "AUTO PATH dir:FWD");
        }
    }

    void LogAutoLapFallbackBuild() const
    {
        if constexpr (kEnableAutoPathLogs)
        {
            SRL::Debug::Print(1, 24, "AUTO PATH fallback seg centers");
        }
    }

    void BuildFallbackAutoLapRoute(const Context& context)
    {
        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());
        LogAutoLapFallbackBuild();
        // A failed PATH build must never leave a stale guide-line marker that
        // would allow the physics pilot to use unverified segment centers.
        autoLapRoute_.selectedGuideLine = -1;
        ClearAutoLapNpcRailRoutes();
        AutoLapRouteDomain::PopulateFallbackCenters(
            autoLapRoute_,
            *context.trackSystem,
            context.trackSegOffset,
            segmentCount);
        RebuildAutoLapRouteYawData();
        AutoLapRouteDomain::FinalizeAutoLapFallbackBuildState(autoLapRoute_);
    }

    void UpdateAutoLapHeading(size_t currentIndex,
                              size_t nextIndex,
                              size_t routePointCount,
                              int32_t& ioCarYawDeg)
    {
        const auto headingVector = AutoLapRouteDomain::BuildHeadingVector(
            autoLapRoute_,
            currentIndex,
            nextIndex,
            routePointCount);
        const int32_t targetYawDeg = YawFromDeltaRaw(
            headingVector.deltaXRaw,
            headingVector.deltaZRaw,
            ioCarYawDeg);
        ioCarYawDeg = NormalizeYawDeg360(targetYawDeg);
        if (AutoLapRouteDomain::CanUpdateCurrentYawOffset(autoLapRoute_))
        {
            autoLapRoute_.currentOffDeg = static_cast<int16_t>(
                ShortestDeltaDeg(static_cast<int32_t>(autoLapRoute_.baseYawDeg), ioCarYawDeg));
        }
        else
        {
            autoLapRoute_.currentOffDeg = 0;
        }
    }

    void BuildAutoLapRoute(const Context& context,
                           const SRL::Math::Types::Vector3D& referenceCarWorldPosition)
    {
        AutoLapRouteDomain::ResetAutoLapRouteBuildData(autoLapRoute_);
        if (!context.trackSystem) return;

        if (BuildAutoLapRouteFromPathGuide(context, referenceCarWorldPosition))
        {
            AutoLapRouteDomain::FinalizeAutoLapGuideBuild(autoLapRoute_);
            return;
        }

        AutoLapRouteDomain::ClearAutoLapGuideLines(autoLapRoute_);
        BuildFallbackAutoLapRoute(context);
    }

    using SimulationTask = Game::SimulationTask;
    using CarRenderPrepareTask = Game::CarRenderPrepareTask;

    using SimulationRuntimeState = Game::SimulationRuntimeState;

    Context context_{};
    TimeAttack::Session timeAttack_{};
    SRL::Input::Digital pad_{0};
    CameraRig::OrbitState orbitState_{};    
    int32_t carYawDeg_ = 0;
    uint32_t frameCounter_ = 0;
    RealtimeFpsState fpsState_{};
#if MASTER_FRAME_PHASE_TELEMETRY
    MasterFrameTiming masterFrameTiming_{};
    MasterFrameTimingPeaks masterFrameTimingPeaks_{};
    FramePhaseTelemetry::State framePhaseTelemetry_{};
    FramePhaseTelemetry::Stamp masterFrameStartStamp_{};
    LongFrameContext lastLongFrameContext_{};
    uint32_t lastCommittedSimulationFrameId_ = 0u;
    bool lastCommittedSimulationFrameValid_ = false;
#endif
    struct TextureRecycleHudAccum
    {
        uint16_t fresh32 = 0;
        uint16_t fresh64 = 0;
        uint16_t reuse32 = 0;
        uint16_t reuse64 = 0;
        uint16_t noReuse32 = 0;
        uint16_t noReuse64 = 0;
        uint16_t reusableSlots = 0;
        uint16_t vdp1FreeKiB = 0;
        uint16_t slaveTicks = 0;
        uint16_t masterTicks = 0;
    };
    TextureRecycleHudAccum recycleHud_{};
    SimulationTask simulationTask_{};
    SimulationRuntimeState simState_{};
    CarRenderPrepareTask carPrepareTask_{};
    CarPrepareRuntimeState carPrepareState_{};
    GameLoopRuntime::SimulationReuseRuntimeState simulationReuseState_{};
    GameLoopRuntime::TrackReuseRuntimeState trackReuseState_{};
    int16_t latestActiveSegmentId_ = -1;
    int32_t playerLastValidSegmentId_ = -1;
    SRL::Math::Types::Vector3D playerSafePose_{};
    int16_t playerSafeYawDeg_ = 0;
    bool playerSafePoseValid_ = false;
    ShadowDebugState shadowDebug_{};

    OverlayEventState overlayEventState_{};
    bool trackProducerHintCached_ = false;
    bool trackProducerJobInFlightHint_ = false;
    uint16_t lastSubmittedTrackFacesThisFrame_ = 0u;
    uint16_t lastRenderedCarFacesThisFrame_ = 0u;
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
    CameraSurfaceGuardState cameraSurfaceGuardState_{};
    uint8_t stateFlags_ = kAutoLapInputToggleEnabledBit;
    int16_t autoLapTargetSegmentId_ = 1;
    // PATH auto-lap speed multiplier test: 4x over baseline (6 -> 24).
    int16_t autoLapStepUnits_ = 12; // 2x do passo base (6)
    AutoLapRouteState autoLapRoute_{};
#if AUTO_LAP_NPC_RAILS && !AUTO_LAP_NPC_SLIM
    std::array<NpcRailRoute, AutoLapNpcRailPolicy::kNpcRailCount> autoLapNpcRoutes_{};
    std::array<NpcRailFollower::State, AutoLapNpcRailPolicy::kNpcRailCount> autoLapNpcFollowers_{};
    std::array<NpcAutoLapPhysicalDriver, AutoLapNpcRailPolicy::kNpcRailCount> autoLapNpcDrivers_{};
    std::array<uint16_t, AutoLapNpcRailPolicy::kNpcRailCount> autoLapNpcRailCounts_{};
    std::array<SRL::Math::Types::Vector3D, AutoLapNpcRailPolicy::kNpcRailCount> npcDrawPos_{};
    std::array<int16_t, AutoLapNpcRailPolicy::kNpcRailCount> npcDrawYawDeg_{};
    std::array<bool, AutoLapNpcRailPolicy::kNpcRailCount> npcDrawValid_{};
    std::array<int16_t, AutoLapNpcRailPolicy::kNpcRailCount> npcScreenX_{};
    std::array<int16_t, AutoLapNpcRailPolicy::kNpcRailCount> npcScreenY_{};
    std::array<int32_t, AutoLapNpcRailPolicy::kNpcRailCount> npcScreenDepthRaw_{};
    std::array<bool, AutoLapNpcRailPolicy::kNpcRailCount> npcScreenValid_{};
    std::array<int32_t, AutoLapNpcRailPolicy::kNpcRailCount> npcLatDxRaw_{};
    std::array<int32_t, AutoLapNpcRailPolicy::kNpcRailCount> npcLatDzRaw_{};
    std::array<uint16_t, AutoLapNpcRailPolicy::kNpcRailCount> npcReuseFaces_{};
    int32_t npcBaseXRaw_ = 0;
    int32_t npcBaseZRaw_ = 0;
    uint8_t autoLapNpcRailReadyCount_ = 0u;
    uint8_t npcStableFrames_ = 0u;
    bool npcRailsArmed_ = false;
    uint32_t npcPublishedSimFrameId_ = 0u;
    uint32_t npcConsumedSimFrameId_ = 0u;
    uint32_t npcContactSimFrameId_ = 0u;
    bool npcPublishedSimFrameValid_ = false;
    bool npcConsumedSimFrameValid_ = false;
    bool npcContactSimFrameValid_ = false;
    bool autoLapGuideReversed_ = false;
    bool lastAlGround_ = false;
    bool lastAlAccel_ = false;
    int16_t lastAlSpeedKmh_ = 0;
    SRL::Math::Types::Vector3D prevContactPlayerPos_{};
    SRL::Math::Types::Vector3D prevContactNpcPos_{};
    int32_t pendingCarContactKickXRaw_ = 0;
    int32_t pendingCarContactKickZRaw_ = 0;
    uint16_t npcContactHits_ = 0u;
    uint8_t npcContactCooldown_ = 0u;
    bool npcContactPrevValid_ = false;
    bool cameraNpcFocus_ = false;
    bool cameraFocusTransferPending_ = false;
    bool cameraFocusTransferNpc_ = false;
    bool cameraFocusTransferNeedsRelocate_ = false;
    bool cameraXHeldPrev_ = false;
    AutoLapTrafficPolicy::State playerTrafficState_{};
    AutoLapTrafficPolicy::PassState playerPassState_{};
    AutoLapTrafficPolicy::StallState playerStallState_{};
    uint8_t playerRecoveryRetryCountdown_ = 0u;
    uint32_t playerRecoveryFrameId_ = 0u;
    bool playerRecoveryAwaitingPublish_ = false;
    uint8_t playerContactRecoveryFrames_ = 0u;
    int16_t playerContactSpeedLimitKmh_ = 30;
#endif
    AutoLapDrivePolicy::State autoLapDriveState_{};
    CameraPathRuntimeState cameraPathRuntime_{};
    HwrStageTrace hwrStageTrace_{};
    WorkRamTraceCooldownType hwrTraceCooldownFrames_ = 0;
    LwrStageTrace lwrStageTrace_{};
    WorkRamTraceCooldownType lwrTraceCooldownFrames_ = 0;
    LowWorkOverlayCooldownType lowWorkFreeOverlayCooldownFrames_ = 0;
    LowWorkOverlayStorage lowWorkOverlay_{};
    uint8_t carForwardOffsetRepeatFrames_ = 0u;
};
