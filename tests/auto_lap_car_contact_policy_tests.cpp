#include <cassert>
#include <vector>

#include "auto_lap_car_contact_policy.hpp"
#include "auto_lap_speed_policy.hpp"
#include "auto_lap_traffic_policy.hpp"

namespace
{
constexpr int32_t Unit(int32_t value) { return value << 16; }

AutoLapCarContactPolicy::Pose PoseAt(int32_t x, int32_t z)
{
    AutoLapCarContactPolicy::Pose pose{};
    pose.xRaw = Unit(x);
    pose.zRaw = Unit(z);
    return pose;
}

struct Raw { int32_t value; int32_t RawValue() const { return value; } };
struct RoutePoint { Raw X; Raw Z; };
struct TestRoute
{
    std::vector<RoutePoint> centers;
    std::vector<int16_t> yawDeg;
};
struct NamedRoute
{
    std::vector<RoutePoint> centers;
    std::vector<int16_t> yawDeg;
    std::vector<int16_t> ids;
};
}

int main()
{
    using AutoLapCarContactPolicy::Resolve;
    const auto player = PoseAt(0, 0);

    // Parallel lanes with enough side clearance do not spuriously collide.
    assert(!Resolve(player, PoseAt(72, 0), 0, 0, 0, 0, true).contact);

    // A side hit separates both, with the lighter NPC correction larger.
    const auto side = Resolve(player, PoseAt(40, 0), Unit(10), 0, 0, 0, true);
    assert(side.contact);
    assert(side.playerPushXRaw < 0);
    assert(side.npcPushXRaw > 0);
    assert(side.playerKickXRaw < 0);
    assert(side.npcKickXRaw > 0);
    assert(-side.playerPushXRaw <= Unit(4));
    assert(side.npcPushXRaw <= Unit(12));

    // The two-disc footprint also catches a nose-to-tail impact.
    const auto rear = Resolve(player, PoseAt(0, -70), 0, -Unit(10), 0, 0, true);
    assert(rear.contact);
    assert(rear.playerPushZRaw > 0);
    assert(rear.playerKickZRaw > 0);
    assert(rear.rearStriker == 1u && rear.strikerLossKmh >= 6u);
    const auto rearLimits = AutoLapCarContactPolicy::ContactSpeedLimits(
        rear, 120, 80);
    assert(rearLimits.playerKmh < 120 && rearLimits.npcKmh == -1);
    const auto npcRear = Resolve(PoseAt(0, -70), player,
                                 0, 0, 0, -Unit(10), true);
    assert(npcRear.contact && npcRear.rearStriker == 2u);
    const auto npcRearLimits = AutoLapCarContactPolicy::ContactSpeedLimits(
        npcRear, 80, 120);
    assert(npcRearLimits.playerKmh == -1 && npcRearLimits.npcKmh < 120);
    assert(side.rearStriker == 0u);

    // Separating or cooling down must not inject a second rebound.
    const auto departing = Resolve(player, PoseAt(40, 0), -Unit(5), 0, 0, 0, true);
    assert(departing.contact && departing.playerKickXRaw == 0);
    const auto cooling = Resolve(player, PoseAt(40, 0), Unit(10), 0, 0, 0, false);
    assert(cooling.contact && cooling.playerKickXRaw == 0);

    // Rebound grows with closing speed, but remains capped on a hard hit.
    const auto soft = Resolve(player, PoseAt(40, 0), Unit(2), 0, 0, 0, true);
    const auto hard = Resolve(player, PoseAt(40, 0), Unit(20), 0, 0, 0, true);
    assert(-soft.playerKickXRaw < -hard.playerKickXRaw);
    assert(-hard.playerKickXRaw <= Unit(3));

    // Exact overlap has a deterministic normal and bounded correction.
    const auto same = Resolve(player, player, 0, 0, 0, 0, true);
    assert(same.contact);
    assert(same.playerPushXRaw != 0 || same.playerPushZRaw != 0);

    // Both final poses are separate, but the player crossed the NPC between
    // physics publications: this must still produce an impact and rollback.
    const auto tunneled = AutoLapCarContactPolicy::ResolveSwept(
        PoseAt(0, 0), PoseAt(0, -220),
        PoseAt(0, -90), PoseAt(0, -90), true);
    assert(tunneled.contact && tunneled.swept);
    assert(tunneled.playerImpactZRaw > Unit(-220));
    assert(tunneled.playerKickZRaw > 0);
    const auto noCross = AutoLapCarContactPolicy::ResolveSwept(
        PoseAt(90, 0), PoseAt(90, -220),
        PoseAt(0, -90), PoseAt(0, -90), true);
    assert(!noCross.contact);

    AutoLapTrafficPolicy::State traffic{};
    AutoLapTrafficPolicy::Vehicle ego{Unit(0), Unit(0), 0, -Unit(1), 82};
    AutoLapTrafficPolicy::Vehicle slower{Unit(0), -Unit(120), 0, -Unit(1), 48};
    const int16_t following = AutoLapTrafficPolicy::LimitSpeed(ego, slower, 82, traffic);
    assert(following < 48);
    AutoLapDrivePolicy::Output command{};
    command.targetSpeedKmh = 82;
    command.accelerate = true;
    AutoLapTrafficPolicy::ApplySpeedLimit(command, ego.speedKmh, following);
    assert(!command.accelerate && command.brake);
    // Parallel lanes and a car behind cannot force a new slowdown.
    traffic = {};
    slower.xRaw = Unit(90);
    assert(AutoLapTrafficPolicy::LimitSpeed(ego, slower, 82, traffic) == 82);

    // Rear traffic alone does not brake the lead car; at top speed a stopped
    // car is visible before the old 400-unit collision horizon.
    AutoLapTrafficPolicy::Vehicle fast{0, 0, 0, -Unit(1), 335};
    AutoLapTrafficPolicy::Vehicle stopped{0, -Unit(3000), 0, -Unit(1), 0};
    traffic = {};
    const auto distantLimit = AutoLapTrafficPolicy::LimitSpeed(
        fast, stopped, 335, traffic);
    assert(distantLimit > 0 && distantLimit < 335);
    traffic = {};
    assert(AutoLapTrafficPolicy::LimitSpeed(stopped, fast, 335, traffic) == 335);

    TestRoute route{};
    for (int i = 0; i < 20; ++i)
    {
        route.centers.push_back({{0}, {Unit(-i * 500)}});
        route.yawDeg.push_back(0);
    }
    assert(AutoLapSpeedPolicy::SectionTargetKmh(0) == 0);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(26) == 335);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(27) == 128);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(35) == 128);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(36) == 56);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(40) == 56);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(41) == 262);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(62) == 308);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(63) == 335);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(90) == 332);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(91) == 80);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(95) == 80);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(97) == 80);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(98) == 80);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(99) == 230);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(142) == 150);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(166) == 90);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(175) == 104);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(190) == 50);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(224) == 90);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(203) == 260);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(254) == 335);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(269) == 335);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(281) == 335);
    assert(AutoLapSpeedPolicy::SectionTargetKmh(291) == 0);
    assert(AutoLapSpeedPolicy::RouteSpeed(route, 0, 0, 0) == 335);
    NamedRoute named{};
    for (int i = 0; i < 20; ++i)
    {
        named.centers.push_back({{0}, {Unit(-i * 500)}});
        named.yawDeg.push_back(0);
        named.ids.push_back(281);
    }
    named.yawDeg[8] = 90;
    assert(AutoLapSpeedPolicy::RouteSpeed(named, 0, 0, 0) == 335);
    named.ids[4] = 190;
    const int16_t towardBico = AutoLapSpeedPolicy::RouteSpeed(named, 0, 0, 0);
    assert(towardBico < 335 && towardBico > 50);
    // Index 7 sits inside the k=28 brake horizon. Index 8 is already past it.
    route.yawDeg[7] = 90;
    const int16_t early = AutoLapSpeedPolicy::RouteSpeed(route, 0, 0, 0);
    const int16_t near = AutoLapSpeedPolicy::RouteSpeed(
        route, 6, 0, Unit(-3000));
    assert(early < 335 && early > near && near > 40);
    AutoLapDrivePolicy::Input speedInput{};
    speedInput.current = {0, 0};
    speedInput.next = {0, -Unit(500)};
    speedInput.afterNext = {0, -Unit(1000)};
    speedInput.groundSupported = true;
    speedInput.plannedSpeedKmh = 335;
    speedInput.speedKmh = 200;
    AutoLapDrivePolicy::State speedState{};
    const auto straightCommand = AutoLapDrivePolicy::Step(speedInput, speedState);
    assert(straightCommand.accelerate && !straightCommand.brake);
    speedInput.plannedSpeedKmh = 85;
    const auto bendCommand = AutoLapDrivePolicy::Step(speedInput, speedState);
    assert(!bendCommand.accelerate && bendCommand.brake);
    int32_t lookX = 0;
    int32_t lookZ = 0;
    AutoLapSpeedPolicy::RouteLookahead(
        route, 0, 0, 0, 200, lookX, lookZ);
    assert(lookX == 0 && lookZ < 0);
    // A brief lane-border fluctuation does not immediately release the brake.
    traffic = {};
    slower.xRaw = 0;
    slower.zRaw = -Unit(120);
    assert(AutoLapTrafficPolicy::LimitSpeed(ego, slower, 82, traffic) < 82);
    slower.xRaw = Unit(90);
    assert(AutoLapTrafficPolicy::LimitSpeed(ego, slower, 82, traffic) < 82);
    for (int i = 0; i < 4; ++i)
        AutoLapTrafficPolicy::LimitSpeed(ego, slower, 82, traffic);
    assert(AutoLapTrafficPolicy::LimitSpeed(ego, slower, 82, traffic) == 82);
    slower.zRaw = Unit(120);
    traffic = {};
    assert(AutoLapTrafficPolicy::LimitSpeed(ego, slower, 82, traffic) == 82);

    // A stopped car ahead invites a pass away from its side; moving traffic
    // continues to use the existing following/braking policy.
    AutoLapTrafficPolicy::Vehicle obstacle{Unit(-20), -Unit(150), 0, -Unit(1), 0};
    assert(AutoLapTrafficPolicy::StoppedObstacleAhead(ego, obstacle));
    assert(AutoLapTrafficPolicy::PreferredPassSide(ego, obstacle) == 1);
    obstacle.xRaw = Unit(20);
    assert(AutoLapTrafficPolicy::PreferredPassSide(ego, obstacle) == -1);
    obstacle.speedKmh = 25;
    assert(!AutoLapTrafficPolicy::StoppedObstacleAhead(ego, obstacle));
    obstacle.speedKmh = 0;
    obstacle.zRaw = Unit(150);
    assert(AutoLapTrafficPolicy::PassedObstacle(ego, obstacle));
    const auto offset = AutoLapTrafficPolicy::OffsetTarget(
        {0, -Unit(100)}, 0, -Unit(1), 1);
    assert(offset.xRaw == Unit(90) && offset.zRaw == -Unit(100));

    AutoLapDrivePolicy::Input guided{};
    guided.current = {0, 0};
    guided.next = {0, -Unit(200)};
    guided.afterNext = {0, -Unit(400)};
    guided.groundSupported = true;
    guided.hasOverrideTarget = true;
    guided.overrideTarget = {Unit(90), -Unit(150)};
    AutoLapDrivePolicy::State guideState{};
    assert(AutoLapDrivePolicy::Step(guided, guideState).target.xRaw == Unit(90));

    AutoLapTrafficPolicy::StallState stall{};
    assert(!AutoLapTrafficPolicy::TickStall(stall, 0, 0, 0, true));
    for (int frame = 0; frame < 89; ++frame)
        assert(!AutoLapTrafficPolicy::TickStall(stall, 0, 0, 0, true));
    assert(AutoLapTrafficPolicy::TickStall(stall, 0, 0, 0, true));
    assert(!AutoLapTrafficPolicy::TickStall(stall, Unit(2), 0, 0, true));
    assert(stall.stoppedFrames == 0);
    for (int frame = 0; frame < 89; ++frame)
        assert(!AutoLapTrafficPolicy::TickStall(stall, Unit(2), 0, 0, true));
    assert(AutoLapTrafficPolicy::TickStall(stall, Unit(2), 0, 0, true));

    AutoLapTrafficPolicy::NpcProgressState npcProgress{};
    for (int frame = 0; frame < 89; ++frame)
        assert(!AutoLapTrafficPolicy::TickNpcRecovery(
            npcProgress, 0, 0, true, false));
    assert(AutoLapTrafficPolicy::TickNpcRecovery(
        npcProgress, 0, 0, true, false)); // Engine speed is irrelevant.
    npcProgress = {};
    for (int frame = 0; frame < 180; ++frame)
        assert(!AutoLapTrafficPolicy::TickNpcRecovery(
            npcProgress, Unit(frame / 10), 0, true, false)); // Slow progress.
    npcProgress = {};
    for (int frame = 0; frame < 120; ++frame)
        assert(!AutoLapTrafficPolicy::TickNpcRecovery(
            npcProgress, 0, 0, true, true)); // Waiting for traffic.
    npcProgress = {};
    for (int frame = 0; frame < 23; ++frame)
        assert(!AutoLapTrafficPolicy::TickNpcRecovery(
            npcProgress, Unit(frame * 3), 0, false, false));
    assert(AutoLapTrafficPolicy::TickNpcRecovery(
        npcProgress, Unit(69), 0, false, false)); // Moving without ground.
    return 0;
}
