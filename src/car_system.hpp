#pragma once

#include <array>
#include <cstdio>
#include <memory>

#include <srl.hpp>

#include "mesh_renderer.hpp"
#include "renderable.hpp"
#include "interfaces.hpp"
#include "modelObject.hpp"

using SRL::Math::Types::Angle;
using SRL::Math::Types::Fxp;
using SRL::Math::Types::Vector3D;

class RenderPipeline;

namespace Game
{
class CarSystem : public IRenderInstance
{
public:
    struct Config
    {
        Vector3D modelCenter;
        Vector3D lightDirection;
        std::array<size_t, 5> drawOrder;
        size_t orderCount{0};
        bool wireframeOnly{false};
    };

    explicit CarSystem(ModelObject* carObj, bool smooth, const Config& config);

    bool Valid() const { return renderer_ != nullptr; }

    void UpdateWheels(bool start, bool stop);

    void Render(int32_t yawDeg);
    void SubmitRender(class RenderPipeline& pipeline, bool logStats = false);

    void SetWorldPosition(const Vector3D& pos) { worldPosition_ = pos; }

    Vector3D WorldPosition() const { return worldPosition_; }

    Game::ICarCommand* Command() { return &command_; }

    // IRenderInstance
    MeshRenderer* Renderer() override { return renderer_.get(); }
    Vector3D Position() const override { return worldPosition_; }
    Angle Yaw() const override { return Angle::FromDegrees(SRL::Math::Types::Fxp::Convert(yawDeg_)); }
    const char* Name() const override { return name_; }

    void SetYawDegrees(int32_t yawDeg) { yawDeg_ = yawDeg; }

private:
    struct CarCommandAdapter : Game::ICarCommand
    {
        Vector3D* position;
        explicit CarCommandAdapter(Vector3D* pos) : position(pos) {}
        void Accelerate() override {}
        void Brake() override {}
        void SteerLeft() override {}
        void SteerRight() override {}
        Vector3D WorldPosition() const override
        {
            return position ? *position : Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0));
        }
    };

    CarCommandAdapter command_{&worldPosition_};
    std::unique_ptr<MeshRenderer> renderer_;
    Vector3D worldPosition_{Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0))};
    Config config_;
    static constexpr size_t kCrashSkipMesh = SIZE_MAX;
    int32_t yawDeg_{0};
    char name_[32]{};
};
} // namespace Game
