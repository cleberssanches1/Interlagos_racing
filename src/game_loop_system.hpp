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
        using SRL::Math::Types::Angle;
        using SRL::Math::Types::Fxp;
        using SRL::Math::Types::Vector2D;
        using SRL::Math::Types::Vector3D;

        while (1)
        {
            AppState::Set(AppState::Stage::LoopFrameBegin, frameCounter_);
            AppState::PresentOverlay(2);
            if (!context_.cartOkFlag || !(*context_.cartOkFlag))
            {
                AppState::Set(AppState::Stage::Fault, frameCounter_);
                SRL::Debug::Print(1, 3, "ERRO: Cartucho 4MB ausente");
                SRL::Debug::Print(1, 4, "Insira cart DRAM e reinicie");
                continue;
            }

            if (!context_.cameraSystem || !context_.trackSystem || !context_.hudSystem || !context_.renderPipeline)
            {
                AppState::Set(AppState::Stage::Fault, frameCounter_);
                SRL::Debug::Print(1, 3, "ERRO: subsistemas nao inicializados");
                continue;
            }

            const bool bHeld = pad_.IsHeld(SRL::Input::Digital::Button::B);
            const bool cHeld = pad_.IsHeld(SRL::Input::Digital::Button::C);
            const bool leftHeld = pad_.IsHeld(SRL::Input::Digital::Button::Left);
            const bool rightHeld = pad_.IsHeld(SRL::Input::Digital::Button::Right);
            context_.cameraSystem->UpdateFromPad(pad_, carYawDeg_, orbitState_);

            // Consume last completed slave simulation (double-buffered, one-frame latency).
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
            }
            if (carPrepareJobInFlight_ && carPrepareTask_.IsDone())
            {
                carPrepareJobInFlight_ = false;
                carPrepareHasCompleted_ = true;
                carPrepareCompletedIdx_ = carPrepareInFlightIdx_;
            }

            Game::GameplayFrameState frameState{};
            frameState.frameId = frameCounter_;
            frameState.carWorldPosition = context_.carWorldPosition;
            frameState.carYawDeg = carYawDeg_;

            if (context_.carSystem && context_.carSystem->get())
            {
                // Feed command interface for future physics integration.
                if (cHeld) context_.carSystem->get()->Command()->Accelerate();
                if (bHeld) context_.carSystem->get()->Command()->Brake();
                if (leftHeld) context_.carSystem->get()->Command()->SteerLeft();
                if (rightHeld) context_.carSystem->get()->Command()->SteerRight();
                context_.carSystem->get()->UpdateWheels(cHeld, bHeld);
                const auto& commands = context_.carSystem->get()->Commands();
                frameState.throttle = commands.throttle;
                frameState.steering = commands.steering;
                frameState.braking = commands.braking;
                frameState.wheelsSpinning = commands.wheelsSpinning;
            }

            const bool useSlaveSim =
                context_.enableSlaveForSimulation &&
                (context_.gameplayTick || context_.carPhysics || context_.audioEvents);
            if (useSlaveSim)
            {
                SimulationTask::Payload simPayload{};
                simPayload.gameplayTick = context_.gameplayTick;
                simPayload.carPhysics = context_.carPhysics;
                simPayload.audioEvents = context_.audioEvents;
                simPayload.trackCollision = context_.trackCollision;
                simPayload.frameState = frameState;
                simPayload.outWorldPosition = frameState.carWorldPosition;
                simPayload.outYawDeg = frameState.carYawDeg;
                if (!simJobInFlight_)
                {
                    const uint8_t slot = simWriteIdx_;
                    simInput_[slot] = simPayload;
                    simulationTask_.Configure(&simInput_[slot], &simOutput_[slot]);
                    SRL::Slave::ExecuteOnSlave(simulationTask_);
                    simJobInFlight_ = true;
                    simInFlightIdx_ = slot;
                    simWriteIdx_ ^= 1u;
                }
            }
            else
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
                context_.carWorldPosition = frameState.carWorldPosition;
                carYawDeg_ = frameState.carYawDeg;
            }

            if (context_.renderCar && context_.carSystem && context_.carSystem->get() &&
                context_.carSystem->get()->Valid() && context_.enableSlaveForCarPrepare)
            {
                if (!carPrepareJobInFlight_)
                {
                    const uint8_t slot = carPrepareWriteIdx_;
                    carPrepareInputYaw_[slot] = carYawDeg_;
                    carPrepareTask_.Configure(&carPrepareInputYaw_[slot], &carPrepareOutputYaw_[slot]);
                    SRL::Slave::ExecuteOnSlave(carPrepareTask_);
                    carPrepareJobInFlight_ = true;
                    carPrepareInFlightIdx_ = slot;
                    carPrepareWriteIdx_ ^= 1u;
                }
            }

            if (context_.enableBg && context_.bgManager)
            {
                AppState::Set(AppState::Stage::LoopBackground, frameCounter_);
                context_.bgManager->Update(context_.cameraSystem->State());
            }

            const Vector3D cameraLocation = context_.cameraSystem->CameraLocation(context_.carWorldPosition);
            const Vector3D viewDirection = context_.cameraSystem->ViewDirection();
            const Vector3D lookTarget = context_.cameraSystem->LookTarget(context_.carWorldPosition, context_.modelOffset);
            (void)viewDirection;

            if (context_.verboseFrameLogs)
            {
                SRL::Debug::Print(0, 18, "Cam pos: %d %d %d",
                                  cameraLocation.X.As<int16_t>(),
                                  cameraLocation.Y.As<int16_t>(),
                                  cameraLocation.Z.As<int16_t>());
            }

            context_.hudSystem->Update(context_.cameraSystem->State(),
                                       context_.modelOffset,
                                       cameraLocation,
                                       context_.carWorldPosition);

            SRL::Scene3D::LoadIdentity();
            SRL::Scene3D::LookAt(cameraLocation, lookTarget, Angle::FromDegrees(0.0));

            context_.trackSystem->BeginFrame(frameCounter_);
            if (context_.trackSystemReady && context_.renderTrack)
            {
                AppState::Set(AppState::Stage::LoopTrack, frameCounter_);
                context_.trackSystem->RenderFrame(true, context_.trackSegOffset, context_.lightDirection, cameraLocation);
                context_.trackSystem->EndFrame();
            }

            SRL::Scene3D::LoadIdentity();
            SRL::Scene3D::LookAt(cameraLocation, lookTarget, Angle::FromDegrees(0.0));

            if (context_.renderCar && context_.carSystem && context_.carSystem->get() && context_.carSystem->get()->Valid())
            {
                AppState::Set(AppState::Stage::LoopCar, frameCounter_);
                if (context_.enableSlaveForCarPrepare && carPrepareHasCompleted_)
                {
                    context_.carSystem->get()->SetYawDegrees(carPrepareOutputYaw_[carPrepareCompletedIdx_]);
                    context_.carSystem->get()->TickCommandState();
                }
                else
                {
                    context_.carSystem->get()->Render(carYawDeg_);
                }
                // Visual bias: keep car slightly above track draw order/transitions.
                const Vector3D renderLift(0.0, SRL::Math::Types::Fxp::BuildRaw(-2 << 16), 0.0);
                context_.carSystem->get()->SetWorldPosition(context_.carWorldPosition + renderLift);
                context_.renderPipeline->Reset();
                context_.carSystem->get()->SubmitRender(*context_.renderPipeline);
                context_.renderPipeline->Flush();
            }
            if (context_.renderAxes)
            {
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

            uint32_t submittedTrackFaces = 0;
            if (context_.trackSystemReady && context_.renderTrack && context_.trackSystem)
            {
                submittedTrackFaces = context_.trackSystem->Telemetry().submittedTrackFaces;
            }
            const uint32_t submittedCarFaces =
                (context_.renderCar && context_.carSystem && context_.carSystem->get() && context_.carSystem->get()->Valid())
                    ? context_.faceCount
                    : 0;

            ++frameCounter_;
            context_.hudSystem->PresentPeriodicFrameStats(frameCounter_,
                                                          context_.logTrack,
                                                          context_.logCar,
                                                          context_.faceCount,
                                                          context_.vertexCount,
                                                          submittedTrackFaces,
                                                          submittedCarFaces);
            // Keep gouraud table upload alive even when VBlank Event dispatch is disabled.
            SRL::Scene3D::LightCopyGouraudTable();
            if (context_.verboseFrameLogs)
            {
                SRL::Debug::Print(1, 15, "SRL::Core::Synchronize frame:%u", frameCounter_);
            }
            AppState::Set(AppState::Stage::LoopSync, frameCounter_);
            SRL::Core::Synchronize();
        }
    }

private:
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
};
