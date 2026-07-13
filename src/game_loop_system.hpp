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
#include "auto_lap_route_build_ops.hpp"
#include "auto_lap_route_transition_ops.hpp"
#include "auto_lap_route_runtime_state.hpp"
#include "auto_lap_route_lifecycle_ops.hpp"
#include "camera_path_runtime_state.hpp"
#include "camera_system.hpp"
#include "car_render_state_assembler.hpp"
#include "car_prepare_runtime_state.hpp"
#include "car_system.hpp"
#include "cd_asset_transition_ops.hpp"
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
            kSbaLoadedBit = 1u << 17
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
        SRL::Math::Types::Vector3D lightDirection{};
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
        while (1)
        {
            AppState::Set(AppState::Stage::LoopFrameBegin, frameCounter_);
            AppState::PresentOverlay(2);
            if (!ValidateFramePreconditions()) continue;
            ResetPerFrameStageTraces();
            ResetPerFrameSimulationTelemetry();
            ResetPerFrameTrackRenderHintCache();
            SetWorkRamDebugTag(SRL::Memory::DebugTag::Unknown);
            CaptureWorkRamStage(hwrStageTrace_.begin, lwrStageTrace_.begin, true);

            const FrameInputState input = PollFrameInput();
            ConsumeCompletedJobs();

            SetWorkRamDebugTag(SRL::Memory::DebugTag::Gameplay);
            Game::GameplayFrameState frameState = BuildGameplayFrameState(input);
            ExecuteGameplayFrame(frameState);
            // Keep chase heading synced with gameplay/physics yaw every frame.
            SyncCameraHeadingFromCar();
            CaptureWorkRamStage(hwrStageTrace_.gameplay, lwrStageTrace_.gameplay);

            if (AutoLapTestEnabled())
            {
                SetWorkRamDebugTag(SRL::Memory::DebugTag::AutoLap);
                UpdateAutoLapRoute(context_, context_.carWorldPosition, carYawDeg_);
                SyncCameraHeadingFromCar();
            }
            CaptureWorkRamStage(hwrStageTrace_.autoLap, lwrStageTrace_.autoLap);

            SetWorkRamDebugTag(SRL::Memory::DebugTag::Background);
            ScheduleCarPrepareIfEnabled();
            UpdateBackground();
            CaptureWorkRamStage(hwrStageTrace_.background, lwrStageTrace_.background);

            const CameraFrameState camera = ResolveCameraFrameState();
            SetWorkRamDebugTag(SRL::Memory::DebugTag::Hud);
            UpdateHud(camera);
            CaptureWorkRamStage(hwrStageTrace_.hud, lwrStageTrace_.hud);
            RenderFrame(camera);
            RenderAxes();

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

    template <bool tEnabled = kEnableLowWorkFreeOverlay,
              typename TOverlay = LowWorkOverlayStorage>
    void UpdateLowWorkFreeOverlayEnabled()
    {
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

#ifdef TRACK_LWR_STAGE_TRACE
            TrackSystem::PrintLwrStageProbes();
#endif

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

        int32_t yawForCamera = carYawDeg_;
        const bool allowManualYawInput =
            !AutoLapTestEnabled() && (context_.carPhysics == nullptr);
        context_.cameraSystem->UpdateFromPad(pad_, yawForCamera, orbitState_, allowManualYawInput);
        if (allowManualYawInput)
        {
            carYawDeg_ = yawForCamera;
        }

        if (Game::CarSystem* car = ActiveCarSystem())
        {
            if (xHeld)
            {
                if (AutoLapTestEnabled())
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
        if (input.yHeld && !YHeldPrev())
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
                if (AutoLapInputToggleEnabled())
                {
                    SetAutoLapTestEnabled(!AutoLapTestEnabled());
                    AutoLapRouteDomain::ClearAutoLapStartupYawAlignment(autoLapRoute_);
                    if (AutoLapTestEnabled())
                    {
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
        context_.carWorldPosition = worldPosition;
        carYawDeg_ = yawDeg;
        SyncCameraHeadingFromCar();
        latestActiveSegmentId_ = frameState.activeSegmentId;
        if (Game::CarSystem* car = ActiveCarSystem())
        {
            car->ApplySimulationFrameState(frameState);
        }
        if (context_.audioEvents)
        {
            // Keep PCM/SGL sound driver calls on the Master SH2.
            context_.audioEvents->OnFrame(frameState);
        }
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

    Game::GameplayFrameState BuildGameplayFrameState(const FrameInputState& input)
    {
        Game::GameplayFrameState frameState{};

        Game::CarSystem* car = ActiveCarSystem();
        if (car)
        {
            Game::CarSystem::GameplayInputSnapshot gameplayInput{};
            if (!AutoLapTestEnabled())
            {
                gameplayInput.SetAccelerateHeld(input.bHeld);
                gameplayInput.SetBrakeHeld(input.cHeld);
                gameplayInput.SetSteerLeftHeld(input.leftHeld);
                gameplayInput.SetSteerRightHeld(input.rightHeld);
                gameplayInput.SetShiftDownHeld(input.lHeld);
                gameplayInput.SetShiftUpHeld(input.rHeld);
                gameplayInput.SetShiftLockHeld(input.xHeld);
            }
            car->PrepareGameplayFrameState(AutoLapTestEnabled() ? nullptr : &gameplayInput,
                                           frameCounter_,
                                           context_.carWorldPosition,
                                           carYawDeg_,
                                           AutoLapTestEnabled(),
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
        return true;
    }

    void ExecuteGameplayFrame(Game::GameplayFrameState& frameState)
    {
        const bool useSlaveSim =
            context_.EnableSlaveForSimulation() &&
            (context_.gameplayTick || context_.carPhysics);

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
        context_.bgManager->Update(context_.cameraSystem->State(), carYawDeg_);
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

    // Resolve camera snapshot for the current authoritative car state.
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

            if (context_.VerboseFrameLogs())
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
        context_.hudSystem->Update(context_.cameraSystem->State(),
                                   context_.modelOffset,
                                   camera.location,
                                   context_.carWorldPosition);
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
        constexpr int32_t kCarDepthBiasUnits = 3;
        return (runtimeDebug.speedProxy <= 20) ? 0 : kCarDepthBiasUnits;
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
    }

    void RenderCar(const CameraFrameState& camera)
    {
        if (!CanRenderCar()) return;

        AppState::Set(AppState::Stage::LoopCar, frameCounter_);
        Game::CarSystem* car = ActiveCarSystem();
        if (!car) return;

        const auto renderInputs = ResolveCarRenderRuntimeInputs(*car);
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

        RenderCarShadowIfEnabled(renderPacket);
        GameLoopRuntime::ApplyCarRenderRuntimeSync(*car, renderPacket, renderInputs.gameplayYawDeg);
        const auto telemetry =
            GameLoopRuntime::SubmitCarRenderRuntime(*context_.renderPipeline, *car);
        lastRenderedCarFacesThisFrame_ = telemetry.renderedFaceCount;
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
        RenderTrackFrame(camera);

        ConfigureScene3dView(camera);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Car);
        RenderCar(camera);
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

        const auto trackFrameContext = TrackRenderDomain::BuildTrackFrameContext(
            frameCounter_,
            latestActiveSegmentId_,
            true,
            context_.trackSegOffset,
            context_.lightDirection,
            camera.location,
            camera.lookTarget,
            context_.carWorldPosition);
        const auto trackRenderPacket =
            TrackRenderDomain::BuildTrackRenderPacket(trackFrameContext, context_.trackSystem);
        lastSubmittedTrackFacesThisFrame_ = trackRenderPacket.submittedTrackFaces;
        GameLoopRuntime::CaptureTrackReuseRuntimeEnabledRequest(
            frameCounter_,
            latestActiveSegmentId_,
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
        CaptureWorkRamStage(hwrStageTrace_.trackDraw, lwrStageTrace_.trackDraw);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::TrackCore);
        context_.trackSystem->EndFrame();
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

        PresentFrameHudAndTelemetry(framePresentation, trackTelemetryView);
        SynchronizeFrameCore();
        UpdateFrameEndOverlays();
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
        const bool allowOptionalHudTelemetry =
            !Game::MemoryBudgetRuntimeBridge::ShouldAvoidHudOptionalTelemetry();
        context_.hudSystem->PresentPeriodicFrameStats(frameCounter_,
                                                      context_.EnableRuntimeStatsLogs() &&
                                                          allowOptionalHudTelemetry,
                                                      context_.LogTrack(),
                                                      context_.LogCar(),
                                                      context_.faceCount,
                                                      context_.vertexCount,
                                                      framePresentation.submittedTrackFaces,
                                                      framePresentation.submittedCarFaces);
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
        SRL::Core::Synchronize();
        hwrStageTrace_.postSync =
            GameLoopRuntime::MaybeCaptureHighWorkRamSnapshot<kEnableDetailedWorkRamTelemetry>();
        lwrStageTrace_.postSync =
            GameLoopRuntime::MaybeCaptureLowWorkRamSnapshot<kEnableDetailedWorkRamTelemetry>(true);
        SetWorkRamDebugTag(SRL::Memory::DebugTag::Unknown);
    }

    void UpdateFrameEndOverlays()
    {
        const Game::CarSystem::DrivetrainDebugSnapshot drivetrain =
            GameLoopOverlayDomain::BuildExtendedDrivetrainOverlaySnapshot(
                (context_.carSystem && context_.carSystem->get()) ? context_.carSystem->get() : nullptr);
        const auto drivingHud = GameLoopRuntime::BuildDrivingHudTextPacket(drivetrain);
        GameLoopRuntime::PresentPresenterBoundaryHudStatusTextPacket(
            GameLoopRuntime::BuildPresenterBoundaryStatusTextPacket(drivingHud));
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
        GameLoopRuntime::PresentGroundProbeOverlay(overlaySnapshot);
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

        SRL::Debug::Print(0, 16, "FPS:%u.%u ms:%u.%u vb:%u.%02u d30:%u%% d60:%u%%",
                          static_cast<unsigned>(metrics.fpsX10 / 10u),
                          static_cast<unsigned>(metrics.fpsX10 % 10u),
                          static_cast<unsigned>(metrics.frameMsX10 / 10u),
                          static_cast<unsigned>(metrics.frameMsX10 % 10u),
                          static_cast<unsigned>(metrics.vbX100 / 100u),
                          static_cast<unsigned>(metrics.vbX100 % 100u),
                          static_cast<unsigned>(metrics.drop30Pct),
                          static_cast<unsigned>(metrics.drop60Pct));

        constexpr uint32_t kSampleWindowFrames = 60u;
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

        std::vector<uint8_t> bytes{};
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

        const int32_t selectedLineIndex = AutoLapRouteDomain::SelectBestGuideLineIndex(
            autoLapRoute_,
            context_.trackSegOffset,
            referenceCarWorldPosition);
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
        RebuildAutoLapRouteYawData();
        return build.valid;
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
    SRL::Input::Digital pad_{0};
    CameraRig::OrbitState orbitState_{};    
    int32_t carYawDeg_ = 0;
    uint32_t frameCounter_ = 0;
    RealtimeFpsState fpsState_{};
    SimulationTask simulationTask_{};
    SimulationRuntimeState simState_{};
    CarRenderPrepareTask carPrepareTask_{};
    CarPrepareRuntimeState carPrepareState_{};
    GameLoopRuntime::SimulationReuseRuntimeState simulationReuseState_{};
    GameLoopRuntime::TrackReuseRuntimeState trackReuseState_{};
    int16_t latestActiveSegmentId_ = -1;
    ShadowDebugState shadowDebug_{};
    int32_t cameraSlopeLiftRaw_ = 0;
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
    uint8_t stateFlags_ = kAutoLapInputToggleEnabledBit;
    int16_t autoLapTargetSegmentId_ = 1;
    // PATH auto-lap speed multiplier test: 4x over baseline (6 -> 24).
    int16_t autoLapStepUnits_ = 12; // 2x do passo base (6)
    AutoLapRouteState autoLapRoute_{};
    CameraPathRuntimeState cameraPathRuntime_{};
    HwrStageTrace hwrStageTrace_{};
    WorkRamTraceCooldownType hwrTraceCooldownFrames_ = 0;
    LwrStageTrace lwrStageTrace_{};
    WorkRamTraceCooldownType lwrTraceCooldownFrames_ = 0;
    LowWorkOverlayCooldownType lowWorkFreeOverlayCooldownFrames_ = 0;
    LowWorkOverlayStorage lowWorkOverlay_{};
    uint8_t carForwardOffsetRepeatFrames_ = 0u;
};
