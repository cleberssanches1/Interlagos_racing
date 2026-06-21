#pragma once

namespace GameLoopRuntime
{

struct PresenterHudTelemetryDecisionPacket
{
    bool valid = false;
    bool shouldPresentPeriodicHud = false;
    bool shouldPresentSegmentOverlapDiagnostics = false;
    bool shouldPresentSh2Telemetry = false;
};

} // namespace GameLoopRuntime
