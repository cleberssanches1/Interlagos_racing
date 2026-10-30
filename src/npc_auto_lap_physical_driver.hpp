#pragma once

#include "auto_lap_drive_policy.hpp"
#include "auto_lap_speed_policy.hpp"
#include "auto_lap_traffic_policy.hpp"
#include "npc_rail_follower.hpp"
#include "simple_car_physics.hpp"
#include "track_collision_query.hpp"

// Each NPC owns the same physics and TCOL query types as the player's car.
// PATH supplies steering targets, not a pose to copy into the rendered car.
class NpcAutoLapPhysicalDriver
{
public:
    void Clear()
    {
        active_ = false;
        pendingKickXRaw_ = 0;
        pendingKickZRaw_ = 0;
        driveState_ = {};
        trafficState_ = {};
        stallState_ = {};
        contactRecoveryFrames_ = 0u;
        contactSpeedLimitKmh_ = 30;
        recoveryRetryCountdown_ = 0u;
        recoveryCount_ = 0u;
        assistCooldown_ = 0u;
        lastValidSegmentId_ = -1;
        safePoseValid_ = false;
        trackSystem_ = nullptr;
        trackOffset_ = nullptr;
    }

    bool Bind(NpcRailFollower::State& npc,
              const NpcRailRoute& route,
              uint16_t startIndex,
              TrackSystem* trackSystem,
              const SRL::Math::Types::Vector3D& trackOffset,
              int32_t seedSegmentId)
    {
        if (!trackSystem || !trackSystem->TrackCollisionMapReady() ||
            seedSegmentId <= 0) return false;
        NpcRailFollower::State candidate{};
        if (!NpcRailFollower::Bind(candidate, route, startIndex, 1)) return false;

        static constexpr uint8_t kAsphalt = 1u;
        SRL::Math::Types::Fxp groundY{};
        int32_t segmentId = -1;
        // The independent TCOL window only searches the seed segment and its
        // driveable neighbors. A seed of -1 skips TCOL entirely, so the NPC
        // would fail to spawn even when this PATH point lies on asphalt.
        if (!trackSystem->FindSurfaceYBySurfaceTypeSet(
                candidate.pos, trackOffset, &kAsphalt, 1u,
                groundY, &segmentId, seedSegmentId, false))
        {
            return false;
        }

        Clear();
        npc = candidate;
        npc.pos.Y = groundY + Game::CarPhysics::GetRideHeightOffset();
        query_.Configure(trackSystem, &trackOffset);
        // NPC must never write the shared TCOL face cache (autolap48).
        query_.SetIsolateFaceCache(true);
        trackSystem_ = trackSystem;
        trackOffset_ = &trackOffset;
        lastValidSegmentId_ = segmentId;
        frame_ = {};
        frame_.carWorldPosition = npc.pos;
        frame_.carYawDeg = npc.yawDeg;
        frame_.activeSegmentId = segmentId;
        frame_.phase = Game::GameplayFrameState::RacePhase::Running;
        frame_.resetRequested = true;
        int32_t yaw = npc.yawDeg;
        physics_.Step(frame_, &query_, npc.pos, yaw);
        frame_.resetRequested = false;
        frame_.carWorldPosition = npc.pos;
        frame_.carYawDeg = yaw;
        safePosition_ = npc.pos;
        safeYawDeg_ = npc.yawDeg;
        safePoseValid_ = true;
        active_ = true;
        return true;
    }

    void AddContactKick(int32_t xRaw, int32_t zRaw,
                        int16_t speedLimitKmh)
    {
        // A rear-hit victim gets the world-space push but no collision brake.
        if (speedLimitKmh >= 0 && (xRaw != 0 || zRaw != 0))
        {
            contactRecoveryFrames_ = 8u;
            contactSpeedLimitKmh_ = speedLimitKmh;
        }
        else if (speedLimitKmh < 0)
        {
            contactRecoveryFrames_ = 0u;
            trafficState_ = {};
        }
        constexpr int32_t kMaxKickRaw = 6 << 16;
        const int64_t x = static_cast<int64_t>(pendingKickXRaw_) + xRaw;
        const int64_t z = static_cast<int64_t>(pendingKickZRaw_) + zRaw;
        pendingKickXRaw_ = static_cast<int32_t>(
            x < -kMaxKickRaw ? -kMaxKickRaw : (x > kMaxKickRaw ? kMaxKickRaw : x));
        pendingKickZRaw_ = static_cast<int32_t>(
            z < -kMaxKickRaw ? -kMaxKickRaw : (z > kMaxKickRaw ? kMaxKickRaw : z));
    }

    void Step(NpcRailFollower::State& npc, const NpcRailRoute& route,
              const AutoLapTrafficPolicy::Vehicle* opponent)
    {
        if (!active_ || !npc.Active() || route.centers.size() < 3u) return;
        const size_t count = route.centers.size();
        size_t current = static_cast<size_t>(npc.pointIndex) % count;
        // Exactly the player Auto Lap's bounded, position-driven progress rule.
        for (size_t guard = 0u; guard < 3u; ++guard)
        {
            const size_t next = (current + 1u) % count;
            if (!HasReachedOrPassedNpcWaypoint(route, current, next, npc.pos)) break;
            current = next;
        }
        npc.pointIndex = static_cast<uint16_t>(current);
        size_t next = (current + 1u) % count;
        size_t afterNext = (next + 1u) % count;

        // autolap47/48: GS drops → accel cut. Assist before policy, but never
        // every frame (shared TCOL face-cache thrash took down the player).
        bool railAssist = false;
        if (assistCooldown_ > 0u) --assistCooldown_;
        if (frame_.debugGroundSupport == 0u && lastValidSegmentId_ > 0 &&
            assistCooldown_ == 0u)
        {
            railAssist = EnsureNpcOnAsphalt(npc, route, current, next);
            assistCooldown_ = 2u; // at most every other frame
            current = static_cast<size_t>(npc.pointIndex) % count;
            next = (current + 1u) % count;
            afterNext = (next + 1u) % count;
        }

        const auto yawAngle = SRL::Math::Types::Angle::FromDegrees(
            SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(npc.yawDeg) << 16));
        AutoLapDrivePolicy::Input drive{};
        drive.car = {npc.pos.X.RawValue(), npc.pos.Z.RawValue()};
        drive.current = {route.centers[current].X.RawValue(),
                         route.centers[current].Z.RawValue()};
        drive.next = {route.centers[next].X.RawValue(),
                      route.centers[next].Z.RawValue()};
        drive.afterNext = {route.centers[afterNext].X.RawValue(),
                           route.centers[afterNext].Z.RawValue()};
        drive.forwardXRaw = SRL::Math::Trigonometry::Sin(yawAngle).RawValue();
        drive.forwardZRaw = -SRL::Math::Trigonometry::Cos(yawAngle).RawValue();
        drive.speedKmh = frame_.carSpeedKmh;
        drive.plannedSpeedKmh = AutoLapSpeedPolicy::RouteSpeed(
            route, current, drive.car.xRaw, drive.car.zRaw);
        AutoLapSpeedPolicy::RouteLookahead(
            route, current, drive.car.xRaw, drive.car.zRaw,
            drive.speedKmh, drive.routeTarget.xRaw, drive.routeTarget.zRaw);
        drive.hasRouteTarget = true;
        if (route.yawDeg.size() == count)
        {
            const int32_t a = route.yawDeg[current];
            const int32_t b = route.yawDeg[next];
            const int32_t c = route.yawDeg[afterNext];
            const int32_t ab = ((b - a + 540) % 360) - 180;
            const int32_t bc = ((c - b + 540) % 360) - 180;
            const int32_t absAb = ab < 0 ? -ab : ab;
            const int32_t absBc = bc < 0 ? -bc : bc;
            drive.upcomingTurnDeg = static_cast<int16_t>(absAb > absBc ? absAb : absBc);
        }
        drive.surfaceType = frame_.groundSurfaceType;
        // G2: near side-rail XZ — brief GS flicker should not cut accel.
        bool nearSideRail = false;
        {
            const auto& pt = route.centers[current];
            const int32_t dx = npc.pos.X.RawValue() - pt.X.RawValue();
            const int32_t dz = npc.pos.Z.RawValue() - pt.Z.RawValue();
            const int32_t adx = dx < 0 ? -dx : dx;
            const int32_t adz = dz < 0 ? -dz : dz;
            const int32_t lim = 96 << 16;
            nearSideRail = adx < lim && adz < lim;
        }
        const bool flickerGrace = nearSideRail &&
            driveState_.missingGroundFrames < 16u &&
            frame_.debugGroundSupport == 0u;
        // Treat successful rail assist / near-rail flicker as supported.
        drive.groundSupported =
            frame_.debugGroundSupport != 0u || railAssist || flickerGrace;
        drive.wallHit = frame_.debugWallHit != 0u;
        const AutoLapTrafficPolicy::Vehicle ego{
            npc.pos.X.RawValue(), npc.pos.Z.RawValue(),
            drive.forwardXRaw, drive.forwardZRaw, frame_.carSpeedKmh};
        auto command = AutoLapDrivePolicy::Step(drive, driveState_);
        bool blockedByTraffic = false;
        if (opponent)
        {
            const int16_t limit = AutoLapTrafficPolicy::LimitSpeed(
                ego, *opponent, command.targetSpeedKmh, trafficState_);
            blockedByTraffic = limit <= 4;
            AutoLapTrafficPolicy::ApplySpeedLimit(command, frame_.carSpeedKmh, limit);
        }
        if (contactRecoveryFrames_ > 0u)
        {
            --contactRecoveryFrames_;
            AutoLapTrafficPolicy::ApplySpeedLimit(
                command, frame_.carSpeedKmh, contactSpeedLimitKmh_);
        }
        // Never freeze NPC accel while rail assist or near-rail flicker grace.
        if (driveState_.missingGroundFrames > 8u && !railAssist && !flickerGrace)
            command.accelerate = false;
        if ((railAssist || flickerGrace) && !command.accelerate &&
            frame_.carSpeedKmh < 40)
            command.accelerate = true;
        if (drive.wallHit && drive.speedKmh < 8)
        {
            command.accelerate = false;
            command.brake = false;
        }

        frame_.throttle = command.accelerate ? 100 : 0;
        frame_.steering = command.steering;
        frame_.braking = command.brake;
        frame_.autoLapContactKickXRaw = pendingKickXRaw_;
        frame_.autoLapContactKickZRaw = pendingKickZRaw_;
        pendingKickXRaw_ = 0;
        pendingKickZRaw_ = 0;
        // G1.2: prefer lastValid / nearest TCOL segment as probe seed (NPC
        // routes store centers only — no PATH ids in LWR).
        if (lastValidSegmentId_ > 0)
        {
            frame_.activeSegmentId = lastValidSegmentId_;
        }
        else if (trackSystem_ && trackOffset_)
        {
            int32_t nearest = -1;
            SRL::Math::Types::Vector3D center{};
            if (trackSystem_->FindNearestSegment(
                    npc.pos, *trackOffset_, nearest, center) &&
                nearest > 0)
            {
                frame_.activeSegmentId = nearest;
            }
        }
        int32_t yaw = npc.yawDeg;
        physics_.Step(frame_, &query_, npc.pos, yaw);
        npc.yawDeg = static_cast<int16_t>((yaw % 360 + 360) % 360);
        frame_.carWorldPosition = npc.pos;
        frame_.carYawDeg = npc.yawDeg;
        frame_.autoLapContactKickXRaw = 0;
        frame_.autoLapContactKickZRaw = 0;
        if (frame_.debugGroundSupport != 0u && frame_.activeSegmentId > 0)
        {
            lastValidSegmentId_ = frame_.activeSegmentId;
            safePosition_ = npc.pos;
            safeYawDeg_ = npc.yawDeg;
            safePoseValid_ = true;
        }
        else if (lastValidSegmentId_ > 0 && assistCooldown_ == 0u)
        {
            // Post-step plant only on the same throttled cadence as pre-step.
            (void)EnsureNpcOnAsphalt(npc, route,
                static_cast<size_t>(npc.pointIndex) % count,
                (static_cast<size_t>(npc.pointIndex) + 1u) % count);
            assistCooldown_ = 2u;
        }
        const bool stalled = AutoLapTrafficPolicy::TickNpcRecovery(
            stallState_, npc.pos.X.RawValue(), npc.pos.Z.RawValue(),
            frame_.debugGroundSupport != 0u || railAssist, blockedByTraffic);
        if (recoveryRetryCountdown_ > 0u) --recoveryRetryCountdown_;
        if (stalled && recoveryRetryCountdown_ == 0u)
        {
            if (TryRecoverAtRailPoint(npc, route, opponent))
            {
                stallState_ = {};
                recoveryRetryCountdown_ = 60u;
            }
            else
            {
                // Do not let an unsupported NPC keep drifting while the
                // camera/track belt still reference its last supported road.
                // Never RelocateFocusWindow here — belt belongs to car 1.
                RestoreSafePose(npc);
                stallState_ = {};
                recoveryRetryCountdown_ = 60u;
            }
        }
    }

    bool Active() const { return active_; }
    const Game::GameplayFrameState& Frame() const { return frame_; }
    int32_t StreamingSegmentId() const
    {
        return frame_.debugGroundSupport != 0u && frame_.activeSegmentId > 0
            ? frame_.activeSegmentId : lastValidSegmentId_;
    }
    // Minimap must track the mesh pose. StreamingSegmentId freezes on
    // lastValid while GS=0, which desyncs the arrow from the drawn car.
    int32_t MinimapSegmentId(const NpcRailFollower::State& npc) const
    {
        if (trackSystem_ && trackOffset_)
        {
            int32_t nearest = -1;
            SRL::Math::Types::Vector3D center{};
            if (trackSystem_->FindNearestSegment(
                    npc.pos, *trackOffset_, nearest, center) &&
                nearest > 0)
            {
                return nearest;
            }
        }
        if (frame_.activeSegmentId > 0) return frame_.activeSegmentId;
        return lastValidSegmentId_;
    }
    bool HasSafeAnchor() const { return safePoseValid_ && lastValidSegmentId_ > 0; }
    const SRL::Math::Types::Vector3D& StreamingPosition(
        const NpcRailFollower::State& npc) const
    {
        return frame_.debugGroundSupport != 0u ? npc.pos : safePosition_;
    }
    int16_t StreamingYawDeg(const NpcRailFollower::State& npc) const
    {
        return frame_.debugGroundSupport != 0u ? npc.yawDeg : safeYawDeg_;
    }
    uint8_t StalledFrames() const { return stallState_.stoppedFrames; }
    uint8_t MissingGroundFrames() const { return stallState_.missingGroundFrames; }
    uint8_t RecoveryCount() const { return recoveryCount_; }

private:
    // Keep NPC on asphalt + PATH when TCOL GS flickers (autolap46/47).
    // 1) Plant Y under current XZ (seed = nearest loaded segment).
    // 2) Else snap XZ to rail center, plant, sync yaw.
    // 3) Always nudge one rail step so the car cannot sit at s:0.
    bool EnsureNpcOnAsphalt(NpcRailFollower::State& npc,
                             const NpcRailRoute& route,
                             size_t current,
                             size_t next)
    {
        if (!trackSystem_ || !trackOffset_ || lastValidSegmentId_ <= 0 ||
            route.centers.size() < 3u) return false;
        static constexpr uint8_t kAsphalt = 1u;
        const size_t count = route.centers.size();
        current %= count;
        next %= count;

        int32_t seed = lastValidSegmentId_;
        {
            int32_t nearest = -1;
            SRL::Math::Types::Vector3D center{};
            if (trackSystem_->FindNearestSegment(
                    npc.pos, *trackOffset_, nearest, center) &&
                nearest > 0)
            {
                seed = nearest;
            }
        }

        SRL::Math::Types::Fxp groundY{};
        int32_t segmentId = -1;
        SRL::Math::Types::Vector3D sample = npc.pos;
        // useSharedFaceCache=false — autolap48: NPC assist poisoned player GS.
        bool planted = trackSystem_->FindSurfaceYBySurfaceTypeSet(
            sample, *trackOffset_, &kAsphalt, 1u,
            groundY, &segmentId, seed, true,
            nullptr, nullptr, nullptr, -1, false) && segmentId > 0;
        if (!planted)
        {
            sample = route.centers[current];
            sample.Y = npc.pos.Y;
            planted = trackSystem_->FindSurfaceYBySurfaceTypeSet(
                sample, *trackOffset_, &kAsphalt, 1u,
                groundY, &segmentId, seed, true,
                nullptr, nullptr, nullptr, -1, false) && segmentId > 0;
            if (planted)
            {
                npc.pos.X = sample.X;
                npc.pos.Z = sample.Z;
            }
        }
        if (!planted)
        {
            // Last resort: sit on rail point even if Y query failed this frame.
            npc.pos.X = route.centers[current].X;
            npc.pos.Z = route.centers[current].Z;
            if (route.yawDeg.size() == count)
                npc.yawDeg = static_cast<int16_t>(route.yawDeg[current]);
            segmentId = seed;
        }
        else
        {
            npc.pos.Y = groundY + Game::CarPhysics::GetRideHeightOffset();
            if (route.yawDeg.size() == count)
                npc.yawDeg = static_cast<int16_t>(route.yawDeg[current]);
        }

        // autolap50: do NOT AdvanceHorizontal here — continuous PATH nudges
        // while GS=0 looked like a search loop. Plant + yaw; physics drives.
        (void)next;
        if (route.yawDeg.size() == count)
            npc.yawDeg = static_cast<int16_t>(route.yawDeg[current]);

        frame_.carWorldPosition = npc.pos;
        frame_.carYawDeg = npc.yawDeg;
        if (segmentId > 0)
        {
            frame_.activeSegmentId = segmentId;
            lastValidSegmentId_ = segmentId;
        }
        safePosition_ = npc.pos;
        safeYawDeg_ = npc.yawDeg;
        safePoseValid_ = true;
        if (driveState_.missingGroundFrames > 0u)
            driveState_.missingGroundFrames = 0u;
        if (stallState_.missingGroundFrames > 0u)
            stallState_.missingGroundFrames = 0u;
        return true;
    }

    void RestoreSafePose(NpcRailFollower::State& npc)
    {
        if (!HasSafeAnchor()) return;
        npc.pos = safePosition_;
        npc.yawDeg = safeYawDeg_;
        frame_.resetRequested = true;
        frame_.activeSegmentId = lastValidSegmentId_;
        int32_t yaw = safeYawDeg_;
        physics_.Step(frame_, &query_, npc.pos, yaw);
        frame_.resetRequested = false;
        frame_.carWorldPosition = npc.pos;
        frame_.carYawDeg = yaw;
        driveState_ = {};
        trafficState_ = {};
        pendingKickXRaw_ = 0;
        pendingKickZRaw_ = 0;
        if (recoveryCount_ < 255u) ++recoveryCount_;
    }

    bool TryRecoverAtRailPoint(NpcRailFollower::State& npc,
                               const NpcRailRoute& route,
                               const AutoLapTrafficPolicy::Vehicle* opponent)
    {
        if (!trackSystem_ || !trackOffset_ || lastValidSegmentId_ <= 0 ||
            route.centers.empty()) return false;
        // Recovery is a last resort after 90 stopped frames. Even when the
        // camera car is near, a validated free PATH point must remain usable.
        static constexpr uint8_t kAsphalt = 1u;
        const size_t count = route.centers.size();
        for (size_t advance = 1u; advance <= 8u; ++advance)
        {
            const size_t index = (static_cast<size_t>(npc.pointIndex) + advance) % count;
            const auto& point = route.centers[index];
            if (opponent)
            {
                const int64_t dx = static_cast<int64_t>(point.X.RawValue()) - opponent->xRaw;
                const int64_t dz = static_cast<int64_t>(point.Z.RawValue()) - opponent->zRaw;
                if (dx > -(140LL << 16) && dx < (140LL << 16) &&
                    dz > -(140LL << 16) && dz < (140LL << 16)) continue;
            }
            SRL::Math::Types::Fxp groundY{};
            int32_t candidateSegmentId = -1;
            if (!trackSystem_->FindSurfaceYBySurfaceTypeSet(
                    point, *trackOffset_, &kAsphalt, 1u,
                    groundY, &candidateSegmentId, lastValidSegmentId_, false) ||
                candidateSegmentId <= 0) continue;
            SRL::Math::Types::Vector3D sample = point;
            sample.Y = groundY + Game::CarPhysics::GetRideHeightOffset();
            SRL::Math::Types::Vector3D wallPush{};
            const SRL::Math::Types::Vector3D forward(
                SRL::Math::Types::Fxp::BuildRaw(0),
                SRL::Math::Types::Fxp::BuildRaw(0),
                SRL::Math::Types::Fxp::BuildRaw(-(1 << 16)));
            if (trackSystem_->FindPlanarWallPush(
                    sample, *trackOffset_, forward,
                    SRL::Math::Types::Fxp::BuildRaw(30 << 16),
                    wallPush, nullptr, candidateSegmentId, false, false,
                    &sample) &&
                (wallPush.X.RawValue() != 0 || wallPush.Z.RawValue() != 0)) continue;
            const uint8_t oldRecoveries = recoveryCount_;
            if (Bind(npc, route, static_cast<uint16_t>(index),
                     trackSystem_, *trackOffset_, candidateSegmentId))
            {
                recoveryCount_ = oldRecoveries == 255u
                    ? 255u : static_cast<uint8_t>(oldRecoveries + 1u);
                return true;
            }
        }
        return false;
    }

    Game::SimpleCarPhysics physics_{};
    TrackCollisionQueryFromSystem query_{};
    Game::GameplayFrameState frame_{};
    AutoLapDrivePolicy::State driveState_{};
    AutoLapTrafficPolicy::State trafficState_{};
    AutoLapTrafficPolicy::NpcProgressState stallState_{};
    int32_t pendingKickXRaw_ = 0;
    int32_t pendingKickZRaw_ = 0;
    TrackSystem* trackSystem_ = nullptr;
    const SRL::Math::Types::Vector3D* trackOffset_ = nullptr;
    int32_t lastValidSegmentId_ = -1;
    SRL::Math::Types::Vector3D safePosition_{};
    int16_t safeYawDeg_ = 0;
    bool safePoseValid_ = false;
    uint8_t contactRecoveryFrames_ = 0u;
    int16_t contactSpeedLimitKmh_ = 30;
    uint8_t recoveryRetryCountdown_ = 0u;
    uint8_t recoveryCount_ = 0u;
    uint8_t assistCooldown_ = 0u;
    bool active_ = false;
};
