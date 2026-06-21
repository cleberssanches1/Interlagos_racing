#pragma once

#include "auto_lap_route_transition_ops.hpp"
#include "game_loop_auto_lap_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedAutoLapFramePacket(
    const AutoLapRouteDomain::AutoLapFrameContext& frameContext,
    const AutoLapRouteDomain::AutoLapRouteStorageSnapshot& storage,
    const AutoLapRouteDomain::AutoLapGuideLoadPacket& guideLoad,
    const AutoLapRouteDomain::AutoLapRouteBuildPacket& build,
    const AutoLapRouteDomain::AutoLapGuideRouteTrace& routeTrace,
    const AutoLapRouteDomain::AutoLapRouteStepPacket& step,
    AutoLapFramePacket& outPacket)
{
    outPacket.frameContext = frameContext;
    outPacket.storage = storage;
    outPacket.guideLoad = guideLoad;
    outPacket.build = build;
    outPacket.routeTrace = routeTrace;
    outPacket.step = step;
}

inline AutoLapFramePacket BuildAutoLapFramePacket(
    const AutoLapRouteDomain::AutoLapFrameContext& frameContext,
    const AutoLapRouteDomain::AutoLapRouteStorageSnapshot& storage,
    const AutoLapRouteDomain::AutoLapGuideLoadPacket& guideLoad,
    const AutoLapRouteDomain::AutoLapRouteBuildPacket& build,
    const AutoLapRouteDomain::AutoLapGuideRouteTrace& routeTrace,
    const AutoLapRouteDomain::AutoLapRouteStepPacket& step)
{
    AutoLapFramePacket packet{};
    SeedAutoLapFramePacket(frameContext, storage, guideLoad, build, routeTrace, step, packet);
    return packet;
}

inline AutoLapFramePacket BuildAutoLapFramePacket(
    const AutoLapRouteDomain::AutoLapFrameContext& frameContext,
    const AutoLapRouteState& state,
    const AutoLapRouteDomain::AutoLapGuideLoadPacket& guideLoad,
    bool buildValid,
    bool usedGuidePath,
    bool normalizedDirection,
    int32_t selectedLineIndex,
    size_t outputPointCount,
    int16_t observedSegmentId,
    int32_t carYawDeg,
    const SRL::Math::Types::Vector3D& carWorldPosition,
    bool stepValid = true)
{
    return BuildAutoLapFramePacket(
        frameContext,
        AutoLapRouteDomain::BuildAutoLapRouteStorageSnapshot(state),
        guideLoad,
        AutoLapRouteDomain::BuildAutoLapRouteBuildPacket(
            state,
            buildValid,
            usedGuidePath,
            normalizedDirection),
        AutoLapRouteDomain::BuildAutoLapGuideRouteTrace(
            state,
            selectedLineIndex,
            outputPointCount,
            normalizedDirection),
        AutoLapRouteDomain::BuildAutoLapRouteStepPacket(
            state,
            observedSegmentId,
            carYawDeg,
            carWorldPosition,
            stepValid));
}

inline AutoLapFramePacket BuildFallbackAutoLapFramePacket(
    const AutoLapRouteDomain::AutoLapFrameContext& frameContext,
    const AutoLapRouteState& state,
    const AutoLapRouteDomain::AutoLapGuideLoadPacket& guideLoad,
    bool buildValid,
    int16_t observedSegmentId,
    int32_t carYawDeg,
    const SRL::Math::Types::Vector3D& carWorldPosition,
    bool stepValid = true)
{
    return BuildAutoLapFramePacket(
        frameContext,
        AutoLapRouteDomain::BuildAutoLapRouteStorageSnapshot(state),
        guideLoad,
        AutoLapRouteDomain::BuildAutoLapRouteBuildPacket(
            state,
            buildValid,
            false,
            false),
        AutoLapRouteDomain::BuildFallbackAutoLapGuideRouteTrace(state),
        AutoLapRouteDomain::BuildAutoLapRouteStepPacket(
            state,
            observedSegmentId,
            carYawDeg,
            carWorldPosition,
            stepValid));
}

} // namespace GameLoopRuntime
