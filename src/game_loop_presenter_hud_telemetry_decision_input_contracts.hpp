#pragma once

namespace GameLoopRuntime
{

struct PresenterHudTelemetryDecisionInputPacket
{
    bool runtimeStatsEnabled = false;
    bool avoidOptionalHudTelemetry = false;
    bool avoidDebugTransientOptionalTelemetry = false;
};

} // namespace GameLoopRuntime
