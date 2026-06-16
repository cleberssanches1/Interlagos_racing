#pragma once

#include <cstdint>

#include "car_system.hpp"

class MeshRenderer;
class RenderPipeline;

namespace CarRenderDomain
{

enum class Stage : uint8_t
{
    StateAssembly = 0,
    ShadowAssembly,
    Submit,
    Telemetry
};

struct Ports
{
    Game::CarSystem* car = nullptr;
    MeshRenderer* shadowRenderer = nullptr;
    RenderPipeline* renderPipeline = nullptr;
};

struct FrameContext
{
    SRL::Math::Types::Vector3D worldPosition{};
    SRL::Math::Types::Vector3D cameraLocation{};
    SRL::Math::Types::Vector3D cameraLookTarget{};
    int32_t gameplayYawDeg = 0;
    int32_t visualYawOffsetDeg = 0;
    Game::CarSystem::RuntimeDebugSnapshot runtimeDebug{};
};

struct CarRenderPacket
{
    bool valid = false;
    SRL::Math::Types::Vector3D renderPosition{};
    int32_t renderYawDeg = 0;
    Game::CarSystem::RuntimeDebugSnapshot runtimeDebug{};
};

struct CarShadowPacket
{
    bool drawBlob = false;
    bool drawModel = false;
    SRL::Math::Types::Vector3D shadowPosition{};
    int32_t shadowYawDeg = 0;
};

struct CarSubmitPacket
{
    bool valid = false;
    bool drawShadowBlob = false;
    bool drawShadowModel = false;
    SRL::Math::Types::Vector3D renderPosition{};
    int32_t renderYawDeg = 0;
    SRL::Math::Types::Vector3D shadowPosition{};
    int32_t shadowYawDeg = 0;
};

struct CarRenderTelemetry
{
    uint16_t renderedFaceCount = 0u;
    bool submitted = false;
};

} // namespace CarRenderDomain
