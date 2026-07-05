#pragma once

#include <algorithm>

#include "game_loop_debug_ops.hpp"
#include "game_loop_overlay_contracts.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"
#include "track_system.hpp"

namespace GameLoopOverlayDomain
{

inline Game::CarSystem::DrivetrainDebugSnapshot BuildExtendedDrivetrainOverlaySnapshot(
    Game::CarSystem* car)
{
    return car
        ? car->BuildDrivetrainDebugSnapshot()
        : Game::CarSystem::DrivetrainDebugSnapshot{};
}

struct SegmentSnapshotAssemblyInputs
{
    Ports ports{};
    SegmentFrameContext frame{};
};

inline bool TryBuildOverlayWindowState(const SegmentSnapshotAssemblyInputs& inputs,
                                       GameLoopRuntime::SegmentOverlaySnapshot& out,
                                       bool& outWindowValid)
{
    int32_t windowStartId = -1;
    uint16_t windowCount = 0;
    outWindowValid = inputs.ports.track->GetRenderWindowDebugSnapshot(windowStartId,
                                                                      out.windowDir,
                                                                      windowCount);
    out.windowStartId = static_cast<int16_t>(windowStartId);
    out.windowCount = static_cast<uint8_t>(std::min<uint16_t>(windowCount, 255u));
    return true;
}

inline void PopulateOverlayNearestSegment(const SegmentSnapshotAssemblyInputs& inputs,
                                          GameLoopRuntime::SegmentOverlaySnapshot& out)
{
    out.carSegmentId = inputs.frame.latestActiveSegmentId;
    int32_t nearestSegmentId = -1;
    SRL::Math::Types::Vector3D nearestCenter{};
    (void)inputs.ports.track->FindNearestSegment(inputs.frame.carWorldPosition,
                                                 inputs.frame.trackSegOffset,
                                                 nearestSegmentId,
                                                 nearestCenter);
    out.nearestSegmentId = static_cast<int16_t>(nearestSegmentId);
}

inline bool TryResolveCarSegmentCenter(const SegmentSnapshotAssemblyInputs& inputs,
                                       const GameLoopRuntime::SegmentOverlaySnapshot& overlay,
                                       SRL::Math::Types::Vector3D& outCarSegmentCenter)
{
    return (overlay.carSegmentId > 0) &&
           inputs.ports.track->FindSegmentCenterById(overlay.carSegmentId,
                                                     inputs.frame.trackSegOffset,
                                                     outCarSegmentCenter);
}

inline void PopulateOverlayWindowSequence(const SegmentSnapshotAssemblyInputs& inputs,
                                          GameLoopRuntime::SegmentOverlaySnapshot& out,
                                          bool windowValid)
{
    if (!windowValid)
    {
        return;
    }

    for (size_t i = 0; i < out.seq.size(); ++i)
    {
        int32_t id = -1;
        if (inputs.ports.track->GetRenderWindowSegmentIdAt(i, id))
        {
            out.seq[i] = static_cast<int16_t>(id);
        }
    }
}

inline bool BuildSegmentOverlaySnapshot(const SegmentSnapshotAssemblyInputs& inputs,
                                        GameLoopRuntime::SegmentOverlaySnapshot& out)
{
    if (!inputs.ports.track)
    {
        return false;
    }

    bool windowValid = false;
    TryBuildOverlayWindowState(inputs, out, windowValid);
    PopulateOverlayNearestSegment(inputs, out);

    SRL::Math::Types::Vector3D carSegmentCenter{};
    const bool carCenterValid = TryResolveCarSegmentCenter(inputs, out, carSegmentCenter);
    out.carY = GameLoopRuntime::FxpToIntDebug(inputs.frame.carWorldPosition.Y);
    out.carX = GameLoopRuntime::FxpToIntDebug(inputs.frame.carWorldPosition.X);
    out.carZ = GameLoopRuntime::FxpToIntDebug(inputs.frame.carWorldPosition.Z);
    out.camX = GameLoopRuntime::FxpToIntDebug(inputs.frame.cameraLocation.X);
    out.camY = GameLoopRuntime::FxpToIntDebug(inputs.frame.cameraLocation.Y);
    out.camZ = GameLoopRuntime::FxpToIntDebug(inputs.frame.cameraLocation.Z);
    out.segY = carCenterValid ? GameLoopRuntime::FxpToIntDebug(carSegmentCenter.Y) : 0;
    out.deltaY = carCenterValid
        ? GameLoopRuntime::FxpToIntDebug(inputs.frame.carWorldPosition.Y - carSegmentCenter.Y)
        : 0;
    out.deltaX = carCenterValid
        ? GameLoopRuntime::FxpToIntDebug(inputs.frame.carWorldPosition.X - carSegmentCenter.X)
        : 0;
    out.deltaZ = carCenterValid
        ? GameLoopRuntime::FxpToIntDebug(inputs.frame.carWorldPosition.Z - carSegmentCenter.Z)
        : 0;
    out.camDirX = GameLoopRuntime::FxpToIntDebug(
        inputs.frame.cameraLookTarget.X - inputs.frame.cameraLocation.X);
    out.camDirZ = GameLoopRuntime::FxpToIntDebug(
        inputs.frame.cameraLookTarget.Z - inputs.frame.cameraLocation.Z);
    GameLoopRuntime::PopulateOverlaySignFlags(out);
    const auto yawAngle =
        SRL::Math::Types::Angle::FromDegrees(
            SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(inputs.frame.carYawDeg) << 16));
    out.fwdX = GameLoopRuntime::FxpToIntDebug(SRL::Math::Trigonometry::Sin(yawAngle));
    out.fwdZ = GameLoopRuntime::FxpToIntDebug(
        SRL::Math::Types::Fxp::BuildRaw(-SRL::Math::Trigonometry::Cos(yawAngle).RawValue()));
    PopulateOverlayWindowSequence(inputs, out, windowValid);

    return true;
}

inline void PopulateOverlayQueryMetrics(
    const GameLoopRuntime::TrackRenderTelemetryViewPacket* trackTelemetryView,
    GameLoopRuntime::OverlayDiagnosticsSnapshot& out)
{
    if (!trackTelemetryView || !trackTelemetryView->valid)
    {
        return;
    }

    GameLoopRuntime::PopulateOverlayQueryMetrics(*trackTelemetryView, out);
}

inline bool BuildOverlayDiagnosticsSnapshot(
    const SegmentSnapshotAssemblyInputs& inputs,
    const Game::CarSystem::RuntimeDebugSnapshot& carDebug,
    const Game::CarSystem::GameplayInputSnapshot& input,
    uint32_t submittedTrackFaces,
    uint32_t submittedCarFaces,
    const GameLoopRuntime::TrackRenderTelemetryViewPacket* trackTelemetryView,
    GameLoopRuntime::OverlayDiagnosticsSnapshot& out)
{
    if (!BuildSegmentOverlaySnapshot(inputs, out.segment))
    {
        return false;
    }

    out.carDebug = carDebug;
    out.input = input;
    GameLoopRuntime::PopulateOverlayFaceCounters(submittedTrackFaces, submittedCarFaces, out);
    PopulateOverlayQueryMetrics(trackTelemetryView, out);
    return true;
}

} // namespace GameLoopOverlayDomain
