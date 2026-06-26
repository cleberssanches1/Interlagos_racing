#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstdint>

#include <srl.hpp>

#include "game_loop_debug_state.hpp"
#include "game_loop_runtime_state.hpp"
#include "game_loop_track_render_telemetry_view_contracts.hpp"
#include "track_render_contracts.hpp"

namespace GameLoopRuntime
{

inline int32_t FxpToIntDebug(const SRL::Math::Types::Fxp& value)
{
    return value.RawValue() >> 16;
}

inline uint16_t ClampToU16(uint32_t value)
{
    return static_cast<uint16_t>(std::min<uint32_t>(value, 65535u));
}

inline uint8_t ClampToU8(uint32_t value)
{
    return static_cast<uint8_t>(std::min<uint32_t>(value, 255u));
}

inline void PopulateOverlayFaceCounters(uint32_t submittedTrackFaces,
                                        uint32_t submittedCarFaces,
                                        OverlayDiagnosticsSnapshot& out)
{
    out.submittedTrackFaces = ClampToU16(submittedTrackFaces);
    out.submittedCarFaces = ClampToU16(submittedCarFaces);
    out.submittedFacesTotal = ClampToU16(
        static_cast<uint32_t>(submittedTrackFaces) +
        static_cast<uint32_t>(submittedCarFaces));
}

inline void PopulateOverlayQueryMetrics(const TrackRenderDomain::TrackRenderTelemetry& telemetry,
                                        OverlayDiagnosticsSnapshot& out)
{
    out.queryCalls = ClampToU16(telemetry.queryCalls);
    out.queryGlobalPasses = ClampToU16(telemetry.queryGlobal);
    out.queryCacheHits = ClampToU16(telemetry.queryCacheHits);
    out.queryCacheMisses = ClampToU16(telemetry.queryCacheMisses);
    out.wallQueryCalls = ClampToU16(telemetry.wallQueryCalls);
    out.wallQueryHits = ClampToU16(telemetry.wallQueryHits);
}

inline void PopulateOverlayQueryMetrics(const TrackRenderTelemetryViewPacket& telemetry,
                                        OverlayDiagnosticsSnapshot& out)
{
    out.queryCalls = ClampToU16(telemetry.queryCalls);
    out.queryGlobalPasses = ClampToU16(telemetry.queryGlobal);
    out.queryCacheHits = ClampToU16(telemetry.queryCacheHits);
    out.queryCacheMisses = ClampToU16(telemetry.queryCacheMisses);
    out.wallQueryCalls = ClampToU16(telemetry.wallQueryCalls);
    out.wallQueryHits = ClampToU16(telemetry.wallQueryHits);
}

inline void PopulateOverlaySignFlags(SegmentOverlaySnapshot& out)
{
    out.signFlags = 0u;
    if (out.carX < 0) out.signFlags |= SegmentOverlaySnapshot::kCarXNegBit;
    if (out.carY < 0) out.signFlags |= SegmentOverlaySnapshot::kCarYNegBit;
    if (out.carZ < 0) out.signFlags |= SegmentOverlaySnapshot::kCarZNegBit;
    if (out.camX < 0) out.signFlags |= SegmentOverlaySnapshot::kCamXNegBit;
    if (out.camY < 0) out.signFlags |= SegmentOverlaySnapshot::kCamYNegBit;
    if (out.camZ < 0) out.signFlags |= SegmentOverlaySnapshot::kCamZNegBit;
}

inline void PopulateSh2BusyMetrics(Sh2SplitTelemetrySnapshot& out)
{
    out.masterBusyTicks = out.trackMasterTicks;
    out.masterWaitTicks = out.simMasterWaitTicks;
    out.slaveWorkTicks = ClampToU16(
        static_cast<uint32_t>(out.trackSlaveProducerTicks) +
        static_cast<uint32_t>(out.trackSlaveSortTicks) +
        static_cast<uint32_t>(out.trackSlavePlanTicks) +
        static_cast<uint32_t>(out.simSlaveTicks));
    out.busyTotal = out.masterBusyTicks + out.slaveWorkTicks;
    out.masterBusyPct = (out.busyTotal > 0u)
        ? ClampToU8((static_cast<uint32_t>(out.masterBusyTicks) * 100u) / out.busyTotal)
        : 0u;
    out.slaveWorkPct = (out.busyTotal > 0u)
        ? ClampToU8((static_cast<uint32_t>(out.slaveWorkTicks) * 100u) / out.busyTotal)
        : 0u;
    out.masterWaitPctOfSim = (out.simSlaveTicks > 0u)
        ? ClampToU8((static_cast<uint32_t>(out.masterWaitTicks) * 100u) /
                    static_cast<uint32_t>(out.simSlaveTicks))
        : 0u;
}

inline void PopulateSh2QueryTelemetry(const TrackRenderDomain::TrackRenderTelemetry& telemetry,
                                      Sh2SplitTelemetrySnapshot& out)
{
    out.queryCalls = ClampToU16(telemetry.queryCalls);
    out.queryGlobal = ClampToU16(telemetry.queryGlobal);
    out.queryScmap = ClampToU16(telemetry.queryScmap);
}

inline void PopulateSh2QueryTelemetry(const TrackRenderTelemetryViewPacket& telemetry,
                                      Sh2SplitTelemetrySnapshot& out)
{
    out.queryCalls = ClampToU16(telemetry.queryCalls);
    out.queryGlobal = ClampToU16(telemetry.queryGlobal);
    out.queryScmap = ClampToU16(telemetry.queryScmap);
}

inline void PrintSegmentWindowOverlay(const SegmentOverlaySnapshot& overlay)
{
    SRL::Debug::Print(1, 18, "OVR car:%d near:%d ws:%d d:%d n:%u",
                      static_cast<int>(overlay.carSegmentId),
                      static_cast<int>(overlay.nearestSegmentId),
                      static_cast<int>(overlay.windowStartId),
                      static_cast<int>(overlay.windowDir),
                      static_cast<unsigned>(overlay.windowCount));
    SRL::Debug::Print(1, 19, "OVR seq:%d,%d,%d,%d,%d",
                      static_cast<int>(overlay.seq[0]),
                      static_cast<int>(overlay.seq[1]),
                      static_cast<int>(overlay.seq[2]),
                      static_cast<int>(overlay.seq[3]),
                      static_cast<int>(overlay.seq[4]));
    SRL::Debug::Print(1, 20, "OVR car X:%c%d Y:%c%d Z:%c%d",
                      overlay.CarXSign(),
                      static_cast<int>(std::abs(overlay.carX)),
                      overlay.CarYSign(),
                      static_cast<int>(std::abs(overlay.carY)),
                      overlay.CarZSign(),
                      static_cast<int>(std::abs(overlay.carZ)));
    SRL::Debug::Print(1, 22, "OVR cam X:%c%d Y:%c%d Z:%c%d",
                      overlay.CamXSign(),
                      static_cast<int>(std::abs(overlay.camX)),
                      overlay.CamYSign(),
                      static_cast<int>(std::abs(overlay.camY)),
                      overlay.CamZSign(),
                      static_cast<int>(std::abs(overlay.camZ)));
}

inline void PrintInputOverlay(const OverlayDiagnosticsSnapshot& overlay)
{
    SRL::Debug::Print(1, 28, "OVR inp dL:%u dR:%u L:%u R:%u C:%u B:%u",
                      static_cast<unsigned>(overlay.input.SteerLeftHeld() ? 1u : 0u),
                      static_cast<unsigned>(overlay.input.SteerRightHeld() ? 1u : 0u),
                      static_cast<unsigned>(overlay.input.ShiftDownHeld() ? 1u : 0u),
                      static_cast<unsigned>(overlay.input.ShiftUpHeld() ? 1u : 0u),
                      static_cast<unsigned>(overlay.input.BrakeHeld() ? 1u : 0u),
                      static_cast<unsigned>(overlay.input.AccelerateHeld() ? 1u : 0u));
}

} // namespace GameLoopRuntime
