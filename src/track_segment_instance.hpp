#pragma once

#include "renderable.hpp"
#include "track_renderer.hpp"

#include <cstdio>

using SRL::Math::Types::Angle;
using SRL::Math::Types::Fxp;
using SRL::Math::Types::Vector3D;

namespace Game
{
class TrackSegmentInstance : public IRenderInstance
{
public:
    TrackSegmentInstance(TrackRenderer* renderer, size_t index)
        : renderer_(renderer), index_(index)
    {
        std::snprintf(name_, sizeof(name_), "Track seg %zu", index_);
    }

    MeshRenderer* Renderer() override
    {
        return renderer_ ? renderer_->SegmentRenderer(index_) : nullptr;
    }

    Vector3D Position() const override
    {
        return renderer_ ? renderer_->SegmentCenter(index_) : Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0));
    }

    Angle Yaw() const override
    {
        return Angle::FromDegrees(Fxp::Convert(0));
    }

    const char* Name() const override
    {
        return name_;
    }

    bool Valid() const
    {
        return renderer_ && index_ < renderer_->MeshCenters().size() && renderer_->SegmentRenderer(index_);
    }

    size_t Index() const { return index_; }

private:
    TrackRenderer* renderer_;
    size_t index_;
    char name_[32]{};
};
} // namespace Game
