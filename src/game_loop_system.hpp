#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <memory>
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

    // Run the main frame loop with fixed subsystem ordering.
    int RunForever()
    {
        while (1)
        {
            AppState::Set(AppState::Stage::LoopFrameBegin, frameCounter_);
            AppState::PresentOverlay(2);
            if (!ValidateFramePreconditions()) continue;
            hwrStageTrace_ = {};
            SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::Unknown);
            hwrStageTrace_.begin = CaptureHighWorkRamSnapshot();

            const FrameInputState input = PollFrameInput();
            ConsumeCompletedJobs();

            SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::Gameplay);
            Game::GameplayFrameState frameState = BuildGameplayFrameState(input);
            ExecuteGameplayFrame(frameState);
            hwrStageTrace_.gameplay = CaptureHighWorkRamSnapshot();

            if (autoLapTestEnabled_)
            {
                SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::AutoLap);
                UpdateAutoLapRoute(context_, context_.carWorldPosition, carYawDeg_);
            }
            hwrStageTrace_.autoLap = CaptureHighWorkRamSnapshot();

            SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::Background);
            ScheduleCarPrepareIfEnabled();
            UpdateBackground();
            hwrStageTrace_.background = CaptureHighWorkRamSnapshot();

            const CameraFrameState camera = ResolveCameraFrameState();
            SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::Hud);
            UpdateHud(camera);
            hwrStageTrace_.hud = CaptureHighWorkRamSnapshot();
            RenderFrame(camera);
            RenderAxes();

            FinishFrame();
        }
    }

private:
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
        Snapshot track{};
        Snapshot car{};
        Snapshot preFinish{};
        Snapshot preSync{};
        Snapshot postSync{};
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

    static int32_t SnapshotLiveDelta(const HwrStageTrace::Snapshot& from,
                                     const HwrStageTrace::Snapshot& to)
    {
        return static_cast<int32_t>(to.liveBytes) - static_cast<int32_t>(from.liveBytes);
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
        const uint32_t initLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Init));
        const uint32_t unknownLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Unknown));
        const uint32_t carLiveBytesDirect = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Car));
        const uint32_t payloadBytesDirect = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedPayloadBytes());
        const uint32_t visibleTagBytesDirect =
            initLiveBytesDirect +
            unknownLiveBytesDirect +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Gameplay)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::AutoLap)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Background)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Hud)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackCore)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackPrepare)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackLod)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackTexture)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackBackend)) +
            carLiveBytesDirect +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Finish)) +
            static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::Sync));
        const uint32_t initSmall16Count = static_cast<uint32_t>(
            SRL::Memory::LowWorkRam::GetUsedBlockCountByTagAtMost(
                SRL::Memory::DebugTag::Init, 16u));
        const uint32_t unknownSmall16Count = static_cast<uint32_t>(
            SRL::Memory::LowWorkRam::GetUsedBlockCountByTagAtMost(
                SRL::Memory::DebugTag::Unknown, 16u));
        const uint32_t trackCoreBlockCount = static_cast<uint32_t>(
            SRL::Memory::LowWorkRam::GetUsedBlockCountByTag(
                SRL::Memory::DebugTag::TrackCore));
        const uint32_t trackPrepareBlockCount = static_cast<uint32_t>(
            SRL::Memory::LowWorkRam::GetUsedBlockCountByTag(
                SRL::Memory::DebugTag::TrackPrepare));
        const uint32_t trackLodBlockCount = static_cast<uint32_t>(
            SRL::Memory::LowWorkRam::GetUsedBlockCountByTag(
                SRL::Memory::DebugTag::TrackLod));
        const uint32_t trackTextureBlockCount = static_cast<uint32_t>(
            SRL::Memory::LowWorkRam::GetUsedBlockCountByTag(
                SRL::Memory::DebugTag::TrackTexture));
        const uint32_t trackCoreSmall16Count = static_cast<uint32_t>(
            SRL::Memory::LowWorkRam::GetUsedBlockCountByTagAtMost(
                SRL::Memory::DebugTag::TrackCore, 16u));
        const uint32_t trackPrepareSmall16Count = static_cast<uint32_t>(
            SRL::Memory::LowWorkRam::GetUsedBlockCountByTagAtMost(
                SRL::Memory::DebugTag::TrackPrepare, 16u));
        const uint32_t trackLodSmall16Count = static_cast<uint32_t>(
            SRL::Memory::LowWorkRam::GetUsedBlockCountByTagAtMost(
                SRL::Memory::DebugTag::TrackLod, 16u));
        const uint32_t trackTextureSmall16Count = static_cast<uint32_t>(
            SRL::Memory::LowWorkRam::GetUsedBlockCountByTagAtMost(
                SRL::Memory::DebugTag::TrackTexture, 16u));
        const uint32_t invalidTagCount = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBlockCountWithInvalidTag());
        const uint32_t invalidTagBytes = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesWithInvalidTag());
        const uint32_t trackCoreLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackCore));
        const uint32_t trackPrepareLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackPrepare));
        const uint32_t trackLodLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackLod));
        const uint32_t trackTextureLiveBytesLwr = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackTexture));
        SRL::Debug::Print(2, 20, "LW7 tc:%u tw:%u tl:%u tx:%u",
                          static_cast<unsigned>(trackCoreLiveBytesLwr),
                          static_cast<unsigned>(trackPrepareLiveBytesLwr),
                          static_cast<unsigned>(trackLodLiveBytesLwr),
                          static_cast<unsigned>(trackTextureLiveBytesLwr));
        SRL::Debug::Print(2, 21, "LW8 c:%u w:%u l:%u x:%u",
                          static_cast<unsigned>(trackCoreBlockCount),
                          static_cast<unsigned>(trackPrepareBlockCount),
                          static_cast<unsigned>(trackLodBlockCount),
                          static_cast<unsigned>(trackTextureBlockCount));
        if (validation.valid)
        {
            SRL::Debug::Print(2, 22, "LW10 c:%u w:%u l:%u x:%u",
                              static_cast<unsigned>(trackCoreSmall16Count),
                              static_cast<unsigned>(trackPrepareSmall16Count),
                              static_cast<unsigned>(trackLodSmall16Count),
                              static_cast<unsigned>(trackTextureSmall16Count));
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
        input.leftHeld = pad_.IsHeld(SRL::Input::Digital::Button::Left);
        input.rightHeld = pad_.IsHeld(SRL::Input::Digital::Button::Right);

        context_.cameraSystem->UpdateFromPad(pad_, carYawDeg_, orbitState_);
        if (input.yHeld && !yHeldPrev_)
        {
            autoLapTestEnabled_ = !autoLapTestEnabled_;
            autoLapRouteInitialized_ = false;
            autoLapRouteBuilt_ = false;
            SRL::Debug::Print(1, 23, "AUTO LAP:%u", autoLapTestEnabled_ ? 1u : 0u);
        }
        if (input.yHeld && input.leftHeld && !leftHeldPrev_)
        {
            if (autoLapStepUnits_ > 1) --autoLapStepUnits_;
            SRL::Debug::Print(1, 24, "AUTO SPD:%d", static_cast<int>(autoLapStepUnits_));
        }
        if (input.yHeld && input.rightHeld && !rightHeldPrev_)
        {
            if (autoLapStepUnits_ < 36) ++autoLapStepUnits_;
            SRL::Debug::Print(1, 24, "AUTO SPD:%d", static_cast<int>(autoLapStepUnits_));
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
            // Feed command interface for future physics integration.
            if (input.cHeld) car->Command()->Accelerate();
            if (input.bHeld) car->Command()->Brake();
            if (input.leftHeld) car->Command()->SteerLeft();
            if (input.rightHeld) car->Command()->SteerRight();
            car->UpdateWheels(input.cHeld, input.bHeld);
            const auto& commands = car->Commands();
            frameState.throttle = commands.throttle;
            frameState.steering = commands.steering;
            frameState.braking = commands.braking;
            frameState.wheelsSpinning = commands.wheelsSpinning;
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
            hwrStageTrace_.track = CaptureHighWorkRamSnapshot();
            hwrStageTrace_.car = hwrStageTrace_.track;
            return;
        }

        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(camera.location, camera.lookTarget, Angle::FromDegrees(0.0));

        if (context_.trackSystemReady && context_.renderTrack)
        {
            context_.trackSystem->SetObservedCarSegmentId(latestActiveSegmentId_);
        }
        SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::TrackCore);
        context_.trackSystem->BeginFrame(frameCounter_);
        if (context_.trackSystemReady && context_.renderTrack)
        {
            AppState::Set(AppState::Stage::LoopTrack, frameCounter_);
            SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::TrackPrepare);
            context_.trackSystem->RenderFrame(true,
                                             context_.trackSegOffset,
                                             context_.lightDirection,
                                             camera.location,
                                             context_.carWorldPosition);
            SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::TrackCore);
            context_.trackSystem->EndFrame();
        }
        hwrStageTrace_.track = CaptureHighWorkRamSnapshot();

        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(camera.location, camera.lookTarget, Angle::FromDegrees(0.0));
        SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::Car);
        RenderCar(camera);
        hwrStageTrace_.car = CaptureHighWorkRamSnapshot();
        SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::Unknown);
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
        hwrStageTrace_.preFinish = CaptureHighWorkRamSnapshot();
        SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::Finish);
        context_.hudSystem->PresentPeriodicFrameStats(frameCounter_,
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
        hwrStageTrace_.preSync = CaptureHighWorkRamSnapshot();
        SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::Sync);
        AppState::Set(AppState::Stage::LoopSync, frameCounter_);
        SRL::Core::Synchronize();
        hwrStageTrace_.postSync = CaptureHighWorkRamSnapshot();
        SRL::Memory::HighWorkRam::SetDebugTag(SRL::Memory::DebugTag::Unknown);
        PrintWorkRamUsageRealtime();
        MaybeLogHighWorkRamTrace();
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
            BuildAutoLapRoute(context);
        }
        if (autoLapRouteCenters_.size() < 2) return;

        const auto rideHeight = SRL::Math::Types::Fxp::BuildRaw(-3 << 16);

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
            if (!autoLapRouteIds_.empty() && autoLapRouteIndex_ < autoLapRouteIds_.size())
            {
                latestActiveSegmentId_ = autoLapRouteIds_[autoLapRouteIndex_];
            }
        }

        const SRL::Math::Types::Vector3D& currentCenter = autoLapRouteCenters_[autoLapRouteIndex_];
        const uint16_t nextIndex = static_cast<uint16_t>((static_cast<size_t>(autoLapRouteIndex_) + 1u) % autoLapRouteCenters_.size());
        const SRL::Math::Types::Vector3D& nextCenter = autoLapRouteCenters_[nextIndex];

        // Move from current car position to next segment center.
        const int32_t ndx = nextCenter.X.RawValue() - ioCarWorldPosition.X.RawValue();
        const int32_t ndz = nextCenter.Z.RawValue() - ioCarWorldPosition.Z.RawValue();
        const int32_t nAdx = (ndx < 0) ? -ndx : ndx;
        const int32_t nAdz = (ndz < 0) ? -ndz : ndz;
        const int32_t maxAxis = (nAdx > nAdz) ? nAdx : nAdz;
        if (maxAxis <= 0) return;

        const int32_t stepRaw = (autoLapStepUnits_ << 16);
        const int64_t moveX64 = (static_cast<int64_t>(ndx) * static_cast<int64_t>(stepRaw)) / maxAxis;
        const int64_t moveZ64 = (static_cast<int64_t>(ndz) * static_cast<int64_t>(stepRaw)) / maxAxis;
        ioCarWorldPosition.X += SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(moveX64));
        ioCarWorldPosition.Z += SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(moveZ64));

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
        if (atx <= (8 << 16) && atz <= (8 << 16))
        {
            autoLapRouteIndex_ = nextIndex;
            ioCarWorldPosition.Y = nextCenter.Y + rideHeight;
        }

        if (!autoLapRouteIds_.empty() && autoLapRouteIndex_ < autoLapRouteIds_.size())
        {
            latestActiveSegmentId_ = autoLapRouteIds_[autoLapRouteIndex_];
        }

        if (nAdx >= nAdz)
        {
            ioCarYawDeg = (ndx >= 0) ? 90 : 270;
        }
        else
        {
            ioCarYawDeg = (ndz >= 0) ? 180 : 0;
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
            "CD/DATA/PATH.NYA",
            "CD/DATA/PATH.NYA;1",
            "cd/data/PATH.NYA",
            "cd/data/PATH.NYA;1",
            "PATH.NYA",
            "PATH.NYA;1",
        };

        std::vector<uint8_t> bytes{};
        bool loaded = false;
        for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i)
        {
            if (!ReadCdBinaryFile(candidates[i], bytes)) continue;
            loaded = true;
            break;
        }
        if (!loaded) return false;

        PathNya::ParseResult parsed{};
        if (!PathNya::Parse(bytes.data(), bytes.size(), parsed))
        {
            SRL::Debug::Print(1, 23, "AUTO PATH parse fail sz:%u",
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

        SRL::Debug::Print(1, 23, "AUTO PATH v:%u l0:%u l1:%u l2:%u",
                          static_cast<unsigned>(parsed.version),
                          static_cast<unsigned>(autoLapGuideLines_[0].size()),
                          static_cast<unsigned>(autoLapGuideLines_[1].size()),
                          static_cast<unsigned>(autoLapGuideLines_[2].size()));
        return true;
    }

    bool BuildAutoLapRouteFromPathGuide(const Context& context)
    {
        if (!LoadAutoLapGuideLines()) return false;
        const auto& middleLine = autoLapGuideLines_[1];
        if (middleLine.size() < 2u)
        {
            SRL::Debug::Print(1, 24, "AUTO PATH mid empty");
            return false;
        }

        autoLapRouteCenters_.reserve(middleLine.size());
        autoLapRouteIds_.reserve(middleLine.size());

        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());
        if (segmentCount <= 0) return false;

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
        for (size_t i = 0; i < middleLine.size(); ++i)
        {
            const SRL::Math::Types::Vector3D routePoint = middleLine[i] + context.trackSegOffset;
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
                static constexpr int32_t kBackSearch = 2;
                static constexpr int32_t kForwardSearch = 8;
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
        return !autoLapRouteCenters_.empty() &&
               autoLapRouteCenters_.size() == autoLapRouteIds_.size();
    }

    // Build preferred route for the player car.
    void BuildAutoLapRoute(const Context& context)
    {
        autoLapRouteIds_.clear();
        autoLapRouteCenters_.clear();
        if (!context.trackSystem) return;

        if (BuildAutoLapRouteFromPathGuide(context))
        {
            autoLapRouteBuilt_ = true;
            autoLapRouteInitialized_ = false;
            return;
        }

        SRL::Debug::Print(1, 24, "AUTO PATH fallback seg centers");

        SRL::Math::Types::Vector3D c{};
        const int32_t segmentCount = static_cast<int32_t>(context.trackSystem->SegmentCount());
        for (int32_t id = 1; id <= segmentCount; ++id)
        {
            if (!context.trackSystem->FindSegmentCenterById(id, context.trackSegOffset, c)) continue;
            autoLapRouteIds_.push_back(id);
            autoLapRouteCenters_.push_back(c);
        }
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
    bool autoLapTestEnabled_ = true;
    int16_t autoLapTargetSegmentId_ = 1;
    int16_t autoLapStepUnits_ = 6;
    bool autoLapRouteInitialized_ = false;
    bool autoLapRouteBuilt_ = false;
    uint16_t autoLapRouteIndex_ = 0;
    TrackLowWorkVector<int16_t> autoLapRouteIds_{};
    TrackLowWorkVector<SRL::Math::Types::Vector3D> autoLapRouteCenters_{};
    std::array<TrackLowWorkVector<SRL::Math::Types::Vector3D>, 3> autoLapGuideLines_{};
    HwrStageTrace hwrStageTrace_{};
    uint16_t hwrTraceCooldownFrames_ = 0;
    bool yHeldPrev_ = false;
    bool leftHeldPrev_ = false;
    bool rightHeldPrev_ = false;
};
