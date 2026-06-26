#pragma once

namespace GameLoopRuntime
{

struct TrackRenderProducerStatePacket
{
    bool valid = false;
    bool producerJobInFlight = false;
    bool producerSafeModeActive = false;
};

} // namespace GameLoopRuntime
