#pragma once

#include <cstdint>
#include <memory>

#include <srl.hpp>

#include "background_manager.hpp"
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
            if (!context_.cartOkFlag || !(*context_.cartOkFlag))
            {
                SRL::Debug::Print(1, 3, "ERRO: Cartucho 4MB ausente");
                SRL::Debug::Print(1, 4, "Insira cart DRAM e reinicie");
                continue;
            }

            if (!context_.cameraSystem || !context_.trackSystem || !context_.hudSystem || !context_.renderPipeline)
            {
                SRL::Debug::Print(1, 3, "ERRO: subsistemas nao inicializados");
                continue;
            }

            const bool bHeld = pad_.IsHeld(SRL::Input::Digital::Button::B);
            const bool cHeld = pad_.IsHeld(SRL::Input::Digital::Button::C);
            const bool leftHeld = pad_.IsHeld(SRL::Input::Digital::Button::Left);
            const bool rightHeld = pad_.IsHeld(SRL::Input::Digital::Button::Right);
            context_.cameraSystem->UpdateFromPad(pad_, carYawDeg_, orbitState_);

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
            context_.carWorldPosition = frameState.carWorldPosition;
            carYawDeg_ = frameState.carYawDeg;
            SRL::Debug::Print(2, 16, "GP ph:%d seg:%ld cp:%lu spd:%d",
                              static_cast<int>(frameState.phase),
                              static_cast<long>(frameState.activeSegmentId),
                              static_cast<unsigned long>(frameState.checkpointsPassed),
                              static_cast<int>(frameState.speedProxy));
            SRL::Debug::Print(2, 17, "CMD th:%d st:%d br:%d wh:%d",
                              static_cast<int>(frameState.throttle),
                              static_cast<int>(frameState.steering),
                              frameState.braking ? 1 : 0,
                              frameState.wheelsSpinning ? 1 : 0);

            if (context_.enableBg && context_.bgManager)
            {
                context_.bgManager->Update(context_.cameraSystem->State());
            }

            const Vector3D cameraLocation = context_.cameraSystem->CameraLocation(context_.carWorldPosition);
            const Vector3D viewDirection = context_.cameraSystem->ViewDirection();
            const Vector3D lookTarget = context_.cameraSystem->LookTarget(context_.carWorldPosition, context_.modelOffset);
            if (context_.cameraSystem->IsZHeld() && ((frameCounter_ & 31) == 0))
            {
                SRL::Debug::Print(1, 14, "Orbit offset: %d %d %d",
                                  viewDirection.X.As<int16_t>(),
                                  viewDirection.Y.As<int16_t>(),
                                  viewDirection.Z.As<int16_t>());
                SRL::Debug::Print(1, 15, "View angles yaw:%d pitch:%d",
                                  context_.cameraSystem->ViewYawDeg(),
                                  context_.cameraSystem->ViewPitchDeg());
            }
            if (frameCounter_ == 0 || (frameCounter_ & 63) == 0)
            {
                SRL::Debug::Print(1, 9, "Car mesh center %u: %d %d %d",
                                  (context_.carSystem && context_.carSystem->get() && context_.carSystem->get()->Renderer())
                                      ? static_cast<unsigned>(context_.carSystem->get()->Renderer()->LastMeshDrawn())
                                      : 0,
                                  context_.carWorldPosition.X.As<int16_t>(),
                                  context_.carWorldPosition.Y.As<int16_t>(),
                                  context_.carWorldPosition.Z.As<int16_t>());
            }
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
                context_.trackSystem->RenderFrame(true, context_.trackSegOffset, context_.lightDirection, cameraLocation);
                context_.trackSystem->EndFrame();
            }

            SRL::Scene3D::LoadIdentity();
            SRL::Scene3D::LookAt(cameraLocation, lookTarget, Angle::FromDegrees(0.0));

            if (context_.renderCar && context_.carSystem && context_.carSystem->get() && context_.carSystem->get()->Valid())
            {
                context_.carSystem->get()->SetWorldPosition(context_.carWorldPosition);
                context_.carSystem->get()->Render(carYawDeg_);
                context_.renderPipeline->Reset();
                context_.carSystem->get()->SubmitRender(*context_.renderPipeline);
                context_.renderPipeline->Flush();
            }
            if (context_.audioEvents)
            {
                frameState.carWorldPosition = context_.carWorldPosition;
                frameState.carYawDeg = carYawDeg_;
                context_.audioEvents->OnFrame(frameState);
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

            ++frameCounter_;
            context_.hudSystem->PresentPeriodicFrameStats(frameCounter_,
                                                          context_.logTrack,
                                                          context_.logCar,
                                                          context_.faceCount,
                                                          context_.vertexCount);
            if (context_.verboseFrameLogs)
            {
                SRL::Debug::Print(1, 15, "SRL::Core::Synchronize frame:%u", frameCounter_);
            }
            SRL::Core::Synchronize();
        }
    }

private:
    Context context_{};
    SRL::Input::Digital pad_{0};
    CameraRig::OrbitState orbitState_{};
    int32_t carYawDeg_ = 0;
    uint32_t frameCounter_ = 0;
};
