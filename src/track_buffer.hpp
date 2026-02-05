#pragma once

#include <srl.hpp>
#include <algorithm>

#include "track_renderer.hpp"

using SRL::Math::Types::Fxp;
using SRL::Math::Types::Vector3D;

namespace Game
{
class TrackBuffer
{
public:
    struct Stats
    {
        size_t meshCount{0};
        size_t drawLimit{0};
        bool ready{false};
    };

    TrackBuffer(TrackRenderer& renderer)
        : renderer_(renderer)
    {}

    bool Prepare(const TrackSerializedCopy& serialized, size_t maxMeshes, size_t drawLimit)
    {
        if (!serialized.Valid()) return false;
        renderer_.ResetState();
        if (!renderer_.LoadFromSerialized(serialized, maxMeshes)) return false;
        renderer_.SetDrawLimit(drawLimit);
        renderer_.SetStartMesh(0);
        const size_t showCount = std::min(renderer_.MeshCount(), drawLimit);
        renderer_.LogSegmentCenters(showCount);
        stats_.meshCount = renderer_.MeshCount();
        stats_.drawLimit = renderer_.DrawLimit();
        stats_.ready = true;
        return true;
    }

    const Stats& GetStats() const { return stats_; }

    Vector3D SegmentCenter(size_t index) const
    {
        if (index >= renderer_.MeshCenters().size()) return Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0));
        return renderer_.MeshCenters()[index] + renderer_.Offset();
    }

    size_t SegmentCount() const { return renderer_.MeshCenters().size(); }

    TrackRenderer& Renderer() { return renderer_; }

private:
    TrackRenderer& renderer_;
    Stats stats_;
};
} // namespace Game
