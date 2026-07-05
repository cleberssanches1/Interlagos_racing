#pragma once

#include <cmath>
#include <cstdint>

#include <srl.hpp>

#include "car_system.hpp"
#include "game_loop_debug_ops.hpp"
#include "game_loop_debug_state.hpp"
#include "shadow_debug_state.hpp"

namespace GameLoopRuntime
{

inline void PresentExtendedDrivetrainOverlay(
    const Game::CarSystem::DrivetrainDebugSnapshot& drivetrain)
{
    SRL::Debug::Print(0, 13, "GEAR:%c RPM:%d KM:%d    ",
                      drivetrain.gearChar,
                      static_cast<int>(drivetrain.engineRpm),
                      static_cast<int>(drivetrain.speedKmh));
    SRL::Debug::Print(1, 17, "OVR gear:%c rpm:%d km:%d   ",
                      drivetrain.gearChar,
                      static_cast<int>(drivetrain.engineRpm),
                      static_cast<int>(drivetrain.speedKmh));
    SRL::Debug::Print(1, 30, "OVR drv th:%d br:%u sp:%d km:%d g:%c rp:%d",
                      static_cast<int>(drivetrain.throttle),
                      static_cast<unsigned>(drivetrain.Braking() ? 1u : 0u),
                      static_cast<int>(drivetrain.speedProxy),
                      static_cast<int>(drivetrain.speedKmh),
                      drivetrain.gearChar,
                      static_cast<int>(drivetrain.engineRpm));
    SRL::Debug::Print(0, 14, "DRV th:%d br:%u sp:%d km:%d g:%c rp:%d",
                      static_cast<int>(drivetrain.throttle),
                      static_cast<unsigned>(drivetrain.Braking() ? 1u : 0u),
                      static_cast<int>(drivetrain.speedProxy),
                      static_cast<int>(drivetrain.speedKmh),
                      drivetrain.gearChar,
                      static_cast<int>(drivetrain.engineRpm));
    SRL::Debug::Print(1, 31, "OVR dyn st:%d yr:%d ys:%d pdx:%d ndz:%d",
                      static_cast<int>(drivetrain.steeringCommand),
                      static_cast<int>(drivetrain.yawRateDeg),
                      static_cast<int>(drivetrain.yawStepDeg),
                      static_cast<int>(drivetrain.planarDx),
                      static_cast<int>(drivetrain.netDz));
    SRL::Debug::Print(0, 31, "DYN st:%d yr:%d ys:%d pdx:%d ndz:%d",
                      static_cast<int>(drivetrain.steeringCommand),
                      static_cast<int>(drivetrain.yawRateDeg),
                      static_cast<int>(drivetrain.yawStepDeg),
                      static_cast<int>(drivetrain.planarDx),
                      static_cast<int>(drivetrain.netDz));
}

inline void PresentSegmentSpatialOverlay(const OverlayDiagnosticsSnapshot& overlay,
                                         int32_t carYawDeg)
{
    SRL::Debug::Print(1, 26, "OVR dir y:%d fx:%d fz:%d cx:%d cz:%d",
                      static_cast<int>(carYawDeg),
                      static_cast<int>(overlay.segment.fwdX),
                      static_cast<int>(overlay.segment.fwdZ),
                      static_cast<int>(overlay.segment.camDirX),
                      static_cast<int>(overlay.segment.camDirZ));
    SRL::Debug::Print(1, 27, "OVR y seg:%d dy:%d gr:%d gf:%d gt:%d sf:%u fm:%u fc:%d",
                      static_cast<int>(overlay.segment.segY),
                      static_cast<int>(overlay.segment.deltaY),
                      static_cast<int>(overlay.carDebug.groundRearY),
                      static_cast<int>(overlay.carDebug.groundFrontY),
                      static_cast<int>(overlay.carDebug.groundTargetY),
                      static_cast<unsigned>(overlay.carDebug.groundSurfaceType),
                      static_cast<unsigned>(overlay.carDebug.groundFamilyId),
                      static_cast<int>(overlay.carDebug.groundFaceIndex));
}

inline void PresentShadowSpatialOverlay(const ShadowDebugState& shadowDebug)
{
    const int32_t shX = FxpToIntDebug(shadowDebug.worldPos.X);
    const int32_t shY = FxpToIntDebug(shadowDebug.worldPos.Y);
    const int32_t shZ = FxpToIntDebug(shadowDebug.worldPos.Z);
    const char shXSgn = (shX < 0) ? '-' : '+';
    const char shYSgn = (shY < 0) ? '-' : '+';
    const char shZSgn = (shZ < 0) ? '-' : '+';
    SRL::Debug::Print(1, 25, "OVR shd X:%c%d Y:%c%d Z:%c%d y:%d",
                      shXSgn,
                      static_cast<int>(std::abs(shX)),
                      shYSgn,
                      static_cast<int>(std::abs(shY)),
                      shZSgn,
                      static_cast<int>(std::abs(shZ)),
                      static_cast<int>(shadowDebug.yawDeg));
}

inline void PresentFaceAndShadowOverlay(const OverlayDiagnosticsSnapshot& overlay,
                                        bool sbaLoaded,
                                        bool renderShadowModelLoaded,
                                        uint32_t sbaMeshCount,
                                        uint32_t sbaFaceCount)
{
    SRL::Debug::Print(1, 21, "OVR face tr:%u ca:%u tt:%u",
                      static_cast<unsigned>(overlay.submittedTrackFaces),
                      static_cast<unsigned>(overlay.submittedCarFaces),
                      static_cast<unsigned>(overlay.submittedFacesTotal));
    SRL::Debug::Print(1, 24, "OVR sba ld:%u rd:%u m:%u f:%u",
                      static_cast<unsigned>(sbaLoaded ? 1u : 0u),
                      static_cast<unsigned>(renderShadowModelLoaded ? 1u : 0u),
                      static_cast<unsigned>(sbaMeshCount),
                      static_cast<unsigned>(sbaFaceCount));
}

inline void PresentGroundProbeOverlay(const OverlayDiagnosticsSnapshot& overlay)
{
    SRL::Debug::Print(1, 29, "OVR gp m:%u dx:%d dz:%d wh:%u wx:%d wz:%d",
                      static_cast<unsigned>(overlay.carDebug.groundMask),
                      static_cast<int>(overlay.segment.deltaX),
                      static_cast<int>(overlay.segment.deltaZ),
                      static_cast<unsigned>(overlay.carDebug.WallHit() ? 1u : 0u),
                      static_cast<int>(overlay.carDebug.wallPushX),
                      static_cast<int>(overlay.carDebug.wallPushZ));
}

inline void PresentPhysicsQueryOverlay(const OverlayDiagnosticsSnapshot& overlay)
{
    SRL::Debug::Print(1, 23, "OVR q q:%u g:%u ch:%u cm:%u w:%u/%u",
                      static_cast<unsigned>(overlay.queryCalls),
                      static_cast<unsigned>(overlay.queryGlobalPasses),
                      static_cast<unsigned>(overlay.queryCacheHits),
                      static_cast<unsigned>(overlay.queryCacheMisses),
                      static_cast<unsigned>(overlay.wallQueryHits),
                      static_cast<unsigned>(overlay.wallQueryCalls));
}

inline void PresentSegmentEventOverlay(const OverlayEventState& state,
                                       const SegmentOverlaySnapshot& overlay,
                                       bool physicsSafeTelemetry)
{
    if (physicsSafeTelemetry)
    {
        return;
    }

    const bool carSegChanged = state.CarSegmentChanged(overlay);
    const bool startChanged = state.WindowStartChanged(overlay);
    if (carSegChanged || startChanged)
    {
        SRL::Debug::Print(1, 23, "OVR evt car:%d>%d ws:%d>%d",
                          static_cast<int>(state.prevCarSegmentId),
                          static_cast<int>(overlay.carSegmentId),
                          static_cast<int>(state.prevWindowStartId),
                          static_cast<int>(overlay.windowStartId));
    }
    else
    {
        SRL::Debug::Print(1, 23, "OVR evt -");
    }
}

} // namespace GameLoopRuntime
