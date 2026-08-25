#pragma once

#include <cstdint>

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
                int32_t& outSegmentId) const override
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

        // Keep gameplay segment tracking strictly progressive. A global nearest
        // search on a closed track can jump to a spatially close but logically
        // distant sector, which desynchronizes the streamed window. After the
        // first lock, stay on a local forward-biased search only.
        bool found = false;
        if (lastSegmentId_ > 0)
        {
            SRL::Math::Types::Fxp localScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
            if (TrySampleProgressive(worldPosition, offset, lastSegmentId_, outSegmentId, center, localScore))
            {
                found = true;
            }
        }

        if (!found && lastSegmentId_ <= 0)
        {
            found = trackSystem_->FindNearestSegment(worldPosition, offset, outSegmentId, center);
        }
        if (found && outSegmentId > 0)
        {
            lastSegmentId_ = outSegmentId;
        }
        else
        {
            lastSegmentId_ = -1;
        }
        return found;
    }

    bool SampleSurfaceYByFamilyId(const SRL::Math::Types::Vector3D& worldPosition,
                                  uint16_t familyId,
                                  SRL::Math::Types::Fxp& outSurfaceY,
                                  int32_t* outSegmentId = nullptr,
                                  int32_t seedSegmentId = -1) const override
    {
        if (!trackSystem_ || !trackSystem_->Ready())
        {
            if (outSegmentId) *outSegmentId = -1;
            outSurfaceY = worldPosition.Y;
            return false;
        }

        const SRL::Math::Types::Vector3D offset =
            trackOffset_ ? *trackOffset_ : SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        const int32_t resolvedSeedSegmentId =
            (seedSegmentId > 0) ? seedSegmentId : static_cast<int32_t>(lastSegmentId_);
        int32_t sampledSegmentId = -1;
        int32_t* sampledSegmentPtr = outSegmentId ? outSegmentId : &sampledSegmentId;
        const bool found = trackSystem_->FindSurfaceYByFamilyId(
            worldPosition,
            offset,
            familyId,
            outSurfaceY,
            sampledSegmentPtr,
            resolvedSeedSegmentId);
        if (found && sampledSegmentPtr && *sampledSegmentPtr > 0)
        {
            lastSegmentId_ = static_cast<int16_t>(*sampledSegmentPtr);
        }
        return found;
    }

    bool SampleSurfaceYByFamilySet(const SRL::Math::Types::Vector3D& worldPosition,
                                   const uint16_t* familyIds,
                                   size_t familyCount,
                                   SRL::Math::Types::Fxp& outSurfaceY,
                                   int32_t* outSegmentId = nullptr,
                                   int32_t seedSegmentId = -1) const override
    {
        if (!familyIds || familyCount == 0u)
        {
            if (outSegmentId) *outSegmentId = -1;
            outSurfaceY = worldPosition.Y;
            return false;
        }

        if (!trackSystem_ || !trackSystem_->Ready())
        {
            if (outSegmentId) *outSegmentId = -1;
            outSurfaceY = worldPosition.Y;
            return false;
        }

        const SRL::Math::Types::Vector3D offset =
            trackOffset_ ? *trackOffset_ : SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        const int32_t resolvedSeedSegmentId =
            (seedSegmentId > 0) ? seedSegmentId : static_cast<int32_t>(lastSegmentId_);
        int32_t sampledSegmentId = -1;
        int32_t* sampledSegmentPtr = outSegmentId ? outSegmentId : &sampledSegmentId;
        const bool found = trackSystem_->FindSurfaceYByFamilySet(
            worldPosition,
            offset,
            familyIds,
            familyCount,
            outSurfaceY,
            sampledSegmentPtr,
            resolvedSeedSegmentId);
        if (found && sampledSegmentPtr && *sampledSegmentPtr > 0)
        {
            lastSegmentId_ = static_cast<int16_t>(*sampledSegmentPtr);
        }
        return found;
    }

    bool SampleSurfaceYByFamilySetStrict(const SRL::Math::Types::Vector3D& worldPosition,
                                         const uint16_t* familyIds,
                                         size_t familyCount,
                                         SRL::Math::Types::Fxp& outSurfaceY,
                                         int32_t* outSegmentId = nullptr,
                                         int32_t seedSegmentId = -1) const override
    {
        if (!familyIds || familyCount == 0u)
        {
            if (outSegmentId) *outSegmentId = -1;
            outSurfaceY = worldPosition.Y;
            return false;
        }

        if (!trackSystem_ || !trackSystem_->Ready())
        {
            if (outSegmentId) *outSegmentId = -1;
            outSurfaceY = worldPosition.Y;
            return false;
        }

        const SRL::Math::Types::Vector3D offset =
            trackOffset_ ? *trackOffset_ : SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        const int32_t resolvedSeedSegmentId =
            (seedSegmentId > 0) ? seedSegmentId : static_cast<int32_t>(lastSegmentId_);
        int32_t sampledSegmentId = -1;
        int32_t* sampledSegmentPtr = outSegmentId ? outSegmentId : &sampledSegmentId;
        const bool found = trackSystem_->FindSurfaceYByFamilySet(
            worldPosition,
            offset,
            familyIds,
            familyCount,
            outSurfaceY,
            sampledSegmentPtr,
            resolvedSeedSegmentId,
            false);
        if (found && sampledSegmentPtr && *sampledSegmentPtr > 0)
        {
            lastSegmentId_ = static_cast<int16_t>(*sampledSegmentPtr);
        }
        return found;
    }
    bool SampleSurfaceYBySurfaceTypeSet(const SRL::Math::Types::Vector3D& worldPosition,
                                        const uint8_t* surfaceTypes,
                                        size_t surfaceTypeCount,
                                        SRL::Math::Types::Fxp& outSurfaceY,
                                        int32_t* outSegmentId = nullptr,
                                        int32_t seedSegmentId = -1) const override
    {
        if (!surfaceTypes || surfaceTypeCount == 0u ||
            !trackSystem_ || !trackSystem_->Ready())
        {
            if (outSegmentId) *outSegmentId = -1;
            outSurfaceY = worldPosition.Y;
            return false;
        }
        const SRL::Math::Types::Vector3D offset =
            trackOffset_ ? *trackOffset_ : SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        const int32_t resolvedSeedSegmentId =
            (seedSegmentId > 0) ? seedSegmentId : static_cast<int32_t>(lastSegmentId_);
        int32_t sampledSegmentId = -1;
        int32_t* sampledSegmentPtr = outSegmentId ? outSegmentId : &sampledSegmentId;
        const bool found = trackSystem_->FindSurfaceYBySurfaceTypeSet(
            worldPosition,
            offset,
            surfaceTypes,
            surfaceTypeCount,
            outSurfaceY,
            sampledSegmentPtr,
            resolvedSeedSegmentId);
        if (found && *sampledSegmentPtr > 0)
        {
            lastSegmentId_ = static_cast<int16_t>(*sampledSegmentPtr);
        }
        return found;
    }
    bool SampleSurfaceYBySurfaceTypeSetStrict(const SRL::Math::Types::Vector3D& worldPosition,
                                              const uint8_t* surfaceTypes,
                                              size_t surfaceTypeCount,
                                              SRL::Math::Types::Fxp& outSurfaceY,
                                              int32_t* outSegmentId = nullptr,
                                              int32_t seedSegmentId = -1) const override
    {
        if (!surfaceTypes || surfaceTypeCount == 0u ||
            !trackSystem_ || !trackSystem_->Ready())
        {
            if (outSegmentId) *outSegmentId = -1;
            outSurfaceY = worldPosition.Y;
            return false;
        }
        const SRL::Math::Types::Vector3D offset =
            trackOffset_ ? *trackOffset_ : SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        const int32_t resolvedSeedSegmentId =
            (seedSegmentId > 0) ? seedSegmentId : static_cast<int32_t>(lastSegmentId_);
        int32_t sampledSegmentId = -1;
        int32_t* sampledSegmentPtr = outSegmentId ? outSegmentId : &sampledSegmentId;
        const bool found = trackSystem_->FindSurfaceYBySurfaceTypeSet(
            worldPosition,
            offset,
            surfaceTypes,
            surfaceTypeCount,
            outSurfaceY,
            sampledSegmentPtr,
            resolvedSeedSegmentId,
            false);
        if (found && *sampledSegmentPtr > 0)
        {
            lastSegmentId_ = static_cast<int16_t>(*sampledSegmentPtr);
        }
        return found;
    }

    bool SampleWheelSurfaceBySurfaceTypeSetStrict(
        const SRL::Math::Types::Vector3D& worldPosition,
        const uint8_t* surfaceTypes,
        size_t surfaceTypeCount,
        Game::SurfaceContact& outContact,
        int32_t seedSegmentId = -1,
        int16_t hintFaceIndex = -1) const override
    {
        outContact = Game::SurfaceContact{};
        if (!surfaceTypes || surfaceTypeCount == 0u ||
            !trackSystem_ || !trackSystem_->Ready())
        {
            return false;
        }

        const SRL::Math::Types::Vector3D offset =
            trackOffset_ ? *trackOffset_ : SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        const int32_t resolvedSeedSegmentId =
            (seedSegmentId > 0) ? seedSegmentId : static_cast<int32_t>(lastSegmentId_);
        const bool found = trackSystem_->FindSurfaceYBySurfaceTypeSet(
            worldPosition,
            offset,
            surfaceTypes,
            surfaceTypeCount,
            outContact.surfaceY,
            &outContact.segmentId,
            resolvedSeedSegmentId,
            false,
            &outContact.familyId,
            &outContact.surfaceType,
            &outContact.faceIndex,
            hintFaceIndex);
        if (!found) return false;
        outContact.valid = true;
        if (outContact.segmentId > 0)
        {
            lastSegmentId_ = static_cast<int16_t>(outContact.segmentId);
        }
        return true;
    }

    bool ResolvePlanarWallPush(const SRL::Math::Types::Vector3D& worldPosition,
                               const SRL::Math::Types::Vector3D& forwardDirection,
                               SRL::Math::Types::Fxp collisionRadius,
                               SRL::Math::Types::Vector3D& outPush,
                               int32_t* outSegmentId = nullptr,
                               int32_t seedSegmentId = -1) const override
    {
        outPush = SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        if (!trackSystem_ || !trackSystem_->Ready())
        {
            if (outSegmentId) *outSegmentId = -1;
            return false;
        }
        const SRL::Math::Types::Vector3D offset =
            trackOffset_ ? *trackOffset_ : SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        const int32_t resolvedSeedSegmentId =
            (seedSegmentId > 0) ? seedSegmentId : static_cast<int32_t>(lastSegmentId_);
        constexpr uint8_t kWallGlobalFallbackCadenceFrames = 8u;
        constexpr uint8_t kWallMissStreakForceFallback = 2u;
        bool allowGlobalFallback = false;
        if (resolvedSeedSegmentId <= 0)
        {
            allowGlobalFallback = true;
        }
        else if (wallMissStreak_ >= kWallMissStreakForceFallback)
        {
            allowGlobalFallback = true;
        }
        else if (wallGlobalFallbackCountdown_ == 0u)
        {
            allowGlobalFallback = true;
        }
        if (wallGlobalFallbackCountdown_ > 0u)
        {
            --wallGlobalFallbackCountdown_;
        }

        const bool found = trackSystem_->FindPlanarWallPush(worldPosition,
                                                            offset,
                                                            forwardDirection,
                                                            collisionRadius,
                                                            outPush,
                                                            outSegmentId,
                                                            resolvedSeedSegmentId,
                                                            allowGlobalFallback);
        if (found)
        {
            if (outSegmentId && *outSegmentId > 0)
            {
                lastSegmentId_ = static_cast<int16_t>(*outSegmentId);
            }
            wallMissStreak_ = 0u;
            wallGlobalFallbackCountdown_ = kWallGlobalFallbackCadenceFrames;
        }
        else
        {
            if (wallMissStreak_ < 0xFFu) ++wallMissStreak_;
        }
        return found;
    }

    bool SampleSurfaceContact(const SRL::Math::Types::Vector3D& worldPosition,
                              Game::SurfaceContact& outContact,
                              int32_t seedSegmentId = -1) const override
    {
        outContact = Game::SurfaceContact{};
        if (!trackSystem_ || !trackSystem_->Ready())
        {
            return false;
        }
        const SRL::Math::Types::Vector3D offset =
            trackOffset_ ? *trackOffset_ : SRL::Math::Types::Vector3D(0.0, 0.0, 0.0);
        const int32_t resolvedSeedSegmentId =
            (seedSegmentId > 0) ? seedSegmentId : static_cast<int32_t>(lastSegmentId_);
        constexpr uint8_t kGlobalFallbackCadenceFrames = 8u;
        constexpr uint8_t kMissStreakForceFallback = 3u;
        bool allowGlobalFallback = false;
        if (resolvedSeedSegmentId <= 0)
        {
            allowGlobalFallback = true;
        }
        else if (contactMissStreak_ >= kMissStreakForceFallback)
        {
            allowGlobalFallback = true;
        }
        else if (contactGlobalFallbackCountdown_ == 0u)
        {
            allowGlobalFallback = true;
        }
        if (contactGlobalFallbackCountdown_ > 0u)
        {
            --contactGlobalFallbackCountdown_;
        }
        const bool found = trackSystem_->FindSurfaceContact(worldPosition,
                                                            offset,
                                                            outContact,
                                                            resolvedSeedSegmentId,
                                                            allowGlobalFallback);
        if (found && outContact.segmentId > 0)
        {
            lastSegmentId_ = static_cast<int16_t>(outContact.segmentId);
            contactMissStreak_ = 0u;
            contactGlobalFallbackCountdown_ = kGlobalFallbackCadenceFrames;
        }
        else
        {
            if (contactMissStreak_ < 0xFFu) ++contactMissStreak_;
        }
        return found;
    }

private:
    static SRL::Math::Types::Fxp ScoreToCenter(const SRL::Math::Types::Vector3D& worldPosition,
                                               const SRL::Math::Types::Vector3D& center)
    {
        return (center.X - worldPosition.X).Abs() + (center.Z - worldPosition.Z).Abs();
    }

    static int32_t WrapSegmentId(int32_t id, int32_t total)
    {
        if (total <= 0) return -1;
        while (id <= 0) id += total;
        while (id > total) id -= total;
        return id;
    }

    bool TrySampleProgressive(const SRL::Math::Types::Vector3D& worldPosition,
                              const SRL::Math::Types::Vector3D& offset,
                              int32_t seedSegmentId,
                              int32_t& outSegmentId,
                              SRL::Math::Types::Vector3D& outSegmentCenter,
                              SRL::Math::Types::Fxp& outScore) const
    {
        const int32_t total = static_cast<int32_t>(trackSystem_->SegmentCount());
        if (total <= 0 || seedSegmentId <= 0) return false;

        bool found = false;
        SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        SRL::Math::Types::Vector3D bestCenter{};
        int32_t bestId = -1;

        constexpr int32_t kBackSearch = 2;
        constexpr int32_t kForwardSearch = 12;
        for (int32_t delta = -kBackSearch; delta <= kForwardSearch; ++delta)
        {
            const int32_t candidateId = WrapSegmentId(seedSegmentId + delta, total);
            if (candidateId <= 0) continue;

            SRL::Math::Types::Vector3D candidateCenter{};
            if (!trackSystem_->FindSegmentCenterById(candidateId, offset, candidateCenter)) continue;

            const SRL::Math::Types::Fxp score = ScoreToCenter(worldPosition, candidateCenter);
            if (!found || score < bestScore)
            {
                found = true;
                bestScore = score;
                bestCenter = candidateCenter;
                bestId = candidateId;
            }
        }

        if (!found) return false;
        outSegmentId = bestId;
        outSegmentCenter = bestCenter;
        outScore = bestScore;
        return true;
    }

    const TrackSystem* trackSystem_ = nullptr;
    const SRL::Math::Types::Vector3D* trackOffset_ = nullptr;
    mutable int16_t lastSegmentId_ = -1;
    mutable uint8_t contactGlobalFallbackCountdown_ = 0u;
    mutable uint8_t contactMissStreak_ = 0u;
    mutable uint8_t wallGlobalFallbackCountdown_ = 0u;
    mutable uint8_t wallMissStreak_ = 0u;
};
