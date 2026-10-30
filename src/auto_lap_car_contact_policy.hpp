#pragma once

#include <cstdint>

// One player/NPC pair, in world XZ Q16.16. This policy has no SGL or heap work.
namespace AutoLapCarContactPolicy
{
struct Pose
{
    int32_t xRaw = 0;
    int32_t zRaw = 0;
    int32_t forwardXRaw = 0;
    int32_t forwardZRaw = -(1 << 16);
};

struct Response
{
    int32_t playerPushXRaw = 0;
    int32_t playerPushZRaw = 0;
    int32_t npcPushXRaw = 0;
    int32_t npcPushZRaw = 0;
    int32_t playerKickXRaw = 0;
    int32_t playerKickZRaw = 0;
    int32_t npcKickXRaw = 0;
    int32_t npcKickZRaw = 0;
    int32_t playerImpactXRaw = 0;
    int32_t playerImpactZRaw = 0;
    int32_t npcImpactXRaw = 0;
    int32_t npcImpactZRaw = 0;
    bool swept = false;
    bool contact = false;
    // 0 = side/ambiguous, 1 = player struck NPC rear, 2 = NPC struck player rear.
    uint8_t rearStriker = 0u;
    uint8_t strikerLossKmh = 0u;
};

struct SpeedLimits
{
    int16_t playerKmh = 30;
    int16_t npcKmh = 30;
};

inline SpeedLimits ContactSpeedLimits(const Response& response,
                                      int16_t playerSpeedKmh,
                                      int16_t npcSpeedKmh)
{
    SpeedLimits limits{};
    if (response.rearStriker == 1u)
    {
        const int32_t reduced = playerSpeedKmh - response.strikerLossKmh;
        limits.playerKmh = static_cast<int16_t>(reduced > 0 ? reduced : 0);
        limits.npcKmh = -1; // no collision brake for the rear-hit car
    }
    else if (response.rearStriker == 2u)
    {
        const int32_t reduced = npcSpeedKmh - response.strikerLossKmh;
        limits.npcKmh = static_cast<int16_t>(reduced > 0 ? reduced : 0);
        limits.playerKmh = -1;
    }
    return limits;
}

inline int32_t Abs(int32_t value) { return value < 0 ? -value : value; }
inline int32_t Min(int32_t a, int32_t b) { return a < b ? a : b; }

inline int32_t ApproxLength(int32_t xRaw, int32_t zRaw)
{
    const int32_t ax = Abs(xRaw);
    const int32_t az = Abs(zRaw);
    return (ax >= az) ? ax + (az >> 1) : az + (ax >> 1);
}

// Two circles along each car's centerline: near-rectangular longitudinal reach
// without making adjacent PATH lanes collide merely because they are parallel.
inline Response Resolve(const Pose& player,
                        const Pose& npc,
                        int32_t playerMoveXRaw,
                        int32_t playerMoveZRaw,
                        int32_t npcMoveXRaw,
                        int32_t npcMoveZRaw,
                        bool allowKick)
{
    Response out{};
    constexpr int32_t kAxleOffsetRaw = 15 << 16;
    constexpr int32_t kPairDiameterRaw = 44 << 16;
    constexpr int32_t kBroadphaseRaw = 100 << 16;
    const int64_t baseDx = static_cast<int64_t>(player.xRaw) - npc.xRaw;
    const int64_t baseDz = static_cast<int64_t>(player.zRaw) - npc.zRaw;
    if (baseDx < -kBroadphaseRaw || baseDx > kBroadphaseRaw ||
        baseDz < -kBroadphaseRaw || baseDz > kBroadphaseRaw)
        return out;

    int32_t bestDx = 0;
    int32_t bestDz = 0;
    int32_t bestDepth = 0;
    for (int32_t p = -1; p <= 1; p += 2)
    {
        for (int32_t n = -1; n <= 1; n += 2)
        {
            const int32_t px = player.xRaw + static_cast<int32_t>(
                (static_cast<int64_t>(player.forwardXRaw) * kAxleOffsetRaw * p) >> 16);
            const int32_t pz = player.zRaw + static_cast<int32_t>(
                (static_cast<int64_t>(player.forwardZRaw) * kAxleOffsetRaw * p) >> 16);
            const int32_t nx = npc.xRaw + static_cast<int32_t>(
                (static_cast<int64_t>(npc.forwardXRaw) * kAxleOffsetRaw * n) >> 16);
            const int32_t nz = npc.zRaw + static_cast<int32_t>(
                (static_cast<int64_t>(npc.forwardZRaw) * kAxleOffsetRaw * n) >> 16);
            const int32_t dx = px - nx;
            const int32_t dz = pz - nz;
            const int32_t depth = kPairDiameterRaw - ApproxLength(dx, dz);
            if (depth > bestDepth)
            {
                bestDepth = depth;
                bestDx = dx;
                bestDz = dz;
            }
        }
    }
    if (bestDepth <= 0) return out;
    out.contact = true;

    int32_t length = ApproxLength(bestDx, bestDz);
    if (length < (1 << 14))
    {
        // Perfectly coincident pair: separate to the player's right.
        bestDx = -player.forwardZRaw;
        bestDz = player.forwardXRaw;
        length = ApproxLength(bestDx, bestDz);
    }
    const int32_t normalXRaw = static_cast<int32_t>(
        (static_cast<int64_t>(bestDx) << 16) / length);
    const int32_t normalZRaw = static_cast<int32_t>(
        (static_cast<int64_t>(bestDz) << 16) / length);
    // A light car-car correction: the controlled car moves less than the NPC.
    const int32_t playerPush = Min(bestDepth >> 2, 4 << 16);
    const int32_t npcPush = Min(bestDepth - (bestDepth >> 2), 12 << 16);
    out.playerPushXRaw = static_cast<int32_t>(
        (static_cast<int64_t>(normalXRaw) * playerPush) >> 16);
    out.playerPushZRaw = static_cast<int32_t>(
        (static_cast<int64_t>(normalZRaw) * playerPush) >> 16);
    out.npcPushXRaw = -static_cast<int32_t>(
        (static_cast<int64_t>(normalXRaw) * npcPush) >> 16);
    out.npcPushZRaw = -static_cast<int32_t>(
        (static_cast<int64_t>(normalZRaw) * npcPush) >> 16);

    const int32_t relativeX = playerMoveXRaw - npcMoveXRaw;
    const int32_t relativeZ = playerMoveZRaw - npcMoveZRaw;
    const int32_t closingRaw = static_cast<int32_t>(
        ((static_cast<int64_t>(relativeX) * normalXRaw) +
         (static_cast<int64_t>(relativeZ) * normalZRaw)) >> 16);
    if (allowKick && closingRaw < -(1 << 15))
    {
        const int32_t kick = Min((-closingRaw) >> 2, 3 << 16);
        out.playerKickXRaw = static_cast<int32_t>(
            (static_cast<int64_t>(normalXRaw) * kick) >> 16);
        out.playerKickZRaw = static_cast<int32_t>(
            (static_cast<int64_t>(normalZRaw) * kick) >> 16);
        out.npcKickXRaw = -out.playerKickXRaw;
        out.npcKickZRaw = -out.playerKickZRaw;

        // Classify a same-direction nose-to-tail impact from the centers,
        // not the separating normal (which can point sideways on a corner).
        const int64_t headingDot =
            (static_cast<int64_t>(player.forwardXRaw) * npc.forwardXRaw +
             static_cast<int64_t>(player.forwardZRaw) * npc.forwardZRaw) >> 16;
        const int64_t ahead =
            ((-baseDx * player.forwardXRaw) -
             (baseDz * player.forwardZRaw)) >> 16;
        const int64_t side =
            ((baseDz * player.forwardXRaw) -
             (baseDx * player.forwardZRaw)) >> 16;
        const int64_t moveAlong =
            ((static_cast<int64_t>(relativeX) * player.forwardXRaw) +
             (static_cast<int64_t>(relativeZ) * player.forwardZRaw)) >> 16;
        if (headingDot > 52428 && side > -(36LL << 16) &&
            side < (36LL << 16))
        {
            if (ahead > (20LL << 16) && moveAlong > (1 << 15))
                out.rearStriker = 1u;
            else if (ahead < -(20LL << 16) && moveAlong < -(1 << 15))
                out.rearStriker = 2u;
            if (out.rearStriker != 0u)
            {
                const int32_t closingKmh =
                    static_cast<int32_t>((Abs(static_cast<int32_t>(moveAlong)) * 11LL) >> 17);
                out.strikerLossKmh = static_cast<uint8_t>(
                    Min(25, 6 + (closingKmh >> 2)));
            }
        }
    }
    return out;
}

inline Pose Interpolate(const Pose& from, const Pose& to, int32_t tRaw)
{
    Pose result = to;
    result.xRaw = from.xRaw + static_cast<int32_t>(
        ((static_cast<int64_t>(to.xRaw) - from.xRaw) * tRaw) >> 16);
    result.zRaw = from.zRaw + static_cast<int32_t>(
        ((static_cast<int64_t>(to.zRaw) - from.zRaw) * tRaw) >> 16);
    return result;
}

// Sample the closest approach, then bisect the first contact. The existing
// two-disc footprint remains authoritative at every sample. No square root,
// allocation or per-face query is needed on the SH-2.
inline Response ResolveSwept(const Pose& playerPrev, const Pose& playerNow,
                             const Pose& npcPrev, const Pose& npcNow,
                             bool allowKick)
{
    const int32_t playerMoveX = playerNow.xRaw - playerPrev.xRaw;
    const int32_t playerMoveZ = playerNow.zRaw - playerPrev.zRaw;
    const int32_t npcMoveX = npcNow.xRaw - npcPrev.xRaw;
    const int32_t npcMoveZ = npcNow.zRaw - npcPrev.zRaw;
    const Response current = Resolve(playerNow, npcNow,
                                     playerMoveX, playerMoveZ,
                                     npcMoveX, npcMoveZ, allowKick);
    if (Resolve(playerPrev, npcPrev, 0, 0, 0, 0, false).contact)
        return current;

    // Scale before squaring so a long track coordinate cannot overflow i64.
    const int64_t rx = (static_cast<int64_t>(playerPrev.xRaw) - npcPrev.xRaw) >> 8;
    const int64_t rz = (static_cast<int64_t>(playerPrev.zRaw) - npcPrev.zRaw) >> 8;
    const int64_t vx = (static_cast<int64_t>(playerMoveX) - npcMoveX) >> 8;
    const int64_t vz = (static_cast<int64_t>(playerMoveZ) - npcMoveZ) >> 8;
    const int64_t speedSq = vx * vx + vz * vz;
    if (speedSq <= 0) return current;
    const int64_t approach = -(rx * vx + rz * vz);
    if (approach <= 0) return current;
    const int64_t closestRaw = approach >= speedSq
        ? (1 << 16) : (approach * (1 << 16)) / speedSq;

    const Pose playerClosest = Interpolate(playerPrev, playerNow,
                                           static_cast<int32_t>(closestRaw));
    const Pose npcClosest = Interpolate(npcPrev, npcNow,
                                        static_cast<int32_t>(closestRaw));
    if (!Resolve(playerClosest, npcClosest,
                 playerMoveX, playerMoveZ, npcMoveX, npcMoveZ, false).contact)
        return current;

    int32_t low = 0;
    int32_t high = static_cast<int32_t>(closestRaw);
    for (uint8_t i = 0u; i < 7u; ++i)
    {
        const int32_t middle = low + ((high - low) >> 1);
        const Pose p = Interpolate(playerPrev, playerNow, middle);
        const Pose n = Interpolate(npcPrev, npcNow, middle);
        if (Resolve(p, n, playerMoveX, playerMoveZ,
                    npcMoveX, npcMoveZ, false).contact)
            high = middle;
        else
            low = middle;
    }
    const Pose playerImpact = Interpolate(playerPrev, playerNow, high);
    const Pose npcImpact = Interpolate(npcPrev, npcNow, high);
    Response impact = Resolve(playerImpact, npcImpact,
                              playerMoveX, playerMoveZ,
                              npcMoveX, npcMoveZ, allowKick);
    if (!impact.contact) return current;
    impact.swept = true;
    impact.playerImpactXRaw = playerImpact.xRaw;
    impact.playerImpactZRaw = playerImpact.zRaw;
    impact.npcImpactXRaw = npcImpact.xRaw;
    impact.npcImpactZRaw = npcImpact.zRaw;
    return impact;
}
} // namespace AutoLapCarContactPolicy
