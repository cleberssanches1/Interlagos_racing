#pragma once

#include "interfaces.hpp"
#include "track_system.hpp"

// Collision query adapter backed by loaded track segments.
class TrackCollisionQueryFromSystem final : public Game::ITrackCollisionQuery
{
public:
    TrackCollisionQueryFromSystem(const TrackSystem* trackSystem,
                                  const SRL::Math::Types::Vector3D* trackOffset)
        : trackSystem_(trackSystem)
        , trackOffset_(trackOffset)
    {}

    bool Sample(const SRL::Math::Types::Vector3D& worldPosition,
                SRL::Math::Types::Vector3D& outSurfaceNormal,
                int32_t& outSegmentId) override
    {
        // Saturn world axis uses negative Y as up in current camera setup.
        outSurfaceNormal = SRL::Math::Types::Vector3D(0.0, -1.0, 0.0);

        if (!trackSystem_ || !trackSystem_->Ready())
        {
            outSegmentId = -1;
            return false;
        }

        const SRL::Math::Types::Vector3D offset =
            trackOffset_ ? *trackOffset_ : SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        SRL::Math::Types::Vector3D center{};
        const bool found = trackSystem_->FindNearestSegment(worldPosition, offset, outSegmentId, center);
        return found;
    }

private:
    const TrackSystem* trackSystem_ = nullptr;
    const SRL::Math::Types::Vector3D* trackOffset_ = nullptr;
};
