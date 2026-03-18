#pragma once

#include <cstdint>
#include <memory>

#include <srl.hpp>
#include <srl_slave.hpp>

#include "background_manager.hpp"
#include "application_state.hpp"
#include "camera_system.hpp"
#include "car_system.hpp"
#include "hud_system.hpp"
#include "interfaces.hpp"
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

            const FrameInputState input = PollFrameInput();
            ConsumeCompletedJobs();

            Game::GameplayFrameState frameState = BuildGameplayFrameState(input);
            ExecuteGameplayFrame(frameState);

            if (autoLapTestEnabled_)
            {
                UpdateAutoLapRoute(context_, context_.carWorldPosition, carYawDeg_);
            }

            ScheduleCarPrepareIfEnabled();
            UpdateBackground();

            const CameraFrameState camera = ResolveCameraFrameState();
            UpdateHud(camera);
            RenderFrame(camera);
            RenderAxes();

            FinishFrame();
        }
    }

private:
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
            return;
        }

        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(camera.location, camera.lookTarget, Angle::FromDegrees(0.0));

        context_.trackSystem->BeginFrame(frameCounter_);
        if (context_.trackSystemReady && context_.renderTrack)
        {
            AppState::Set(AppState::Stage::LoopTrack, frameCounter_);
            context_.trackSystem->SetObservedCarSegmentId(latestActiveSegmentId_);
            context_.trackSystem->RenderFrame(true,
                                             context_.trackSegOffset,
                                             context_.lightDirection,
                                             camera.location,
                                             context_.carWorldPosition);
            context_.trackSystem->EndFrame();
        }

        SRL::Scene3D::LoadIdentity();
        SRL::Scene3D::LookAt(camera.location, camera.lookTarget, Angle::FromDegrees(0.0));
        RenderCar(camera);
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
        AppState::Set(AppState::Stage::LoopSync, frameCounter_);
        SRL::Core::Synchronize();
    }

    // Advance car through segment centers for full lap streaming test.
    void UpdateAutoLapRoute(const Context& context,
                            SRL::Math::Types::Vector3D& ioCarWorldPosition,
                            int32_t& ioCarYawDeg)
    {
        if (!context.trackSystem || !context.trackSystemReady) return;
        if (!autoLapRouteBuilt_)
        {
            BuildAutoLapRoute(context);
        }
        if (autoLapRouteIds_.size() < 2 || autoLapRouteCenters_.size() < 2) return;

        const auto rideHeight = SRL::Math::Types::Fxp::BuildRaw(-3 << 16);

        if (!autoLapRouteInitialized_)
        {
            int32_t nearestId = -1;
            SRL::Math::Types::Vector3D nearestCenter{};
            if (!context.trackSystem->FindNearestSegment(ioCarWorldPosition, context.trackSegOffset, nearestId, nearestCenter) ||
                nearestId <= 0)
            {
                return;
            }
            size_t bestIdx = 0;
            for (size_t i = 0; i < autoLapRouteIds_.size(); ++i)
            {
                if (autoLapRouteIds_[i] == nearestId)
                {
                    bestIdx = i;
                    break;
                }
            }
            autoLapRouteIndex_ = static_cast<uint16_t>(bestIdx);
            ioCarWorldPosition.X = autoLapRouteCenters_[autoLapRouteIndex_].X;
            ioCarWorldPosition.Z = autoLapRouteCenters_[autoLapRouteIndex_].Z;
            ioCarWorldPosition.Y = autoLapRouteCenters_[autoLapRouteIndex_].Y + rideHeight;
            autoLapRouteInitialized_ = true;
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

        if (nAdx >= nAdz)
        {
            ioCarYawDeg = (ndx >= 0) ? 90 : 270;
        }
        else
        {
            ioCarYawDeg = (ndz >= 0) ? 180 : 0;
        }
    }

    // Build deterministic segment route ordered by segment id.
    void BuildAutoLapRoute(const Context& context)
    {
        autoLapRouteIds_.clear();
        autoLapRouteCenters_.clear();
        if (!context.trackSystem) return;

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
    std::vector<int16_t> autoLapRouteIds_{};
    std::vector<SRL::Math::Types::Vector3D> autoLapRouteCenters_{};
    bool yHeldPrev_ = false;
    bool leftHeldPrev_ = false;
    bool rightHeldPrev_ = false;
};
