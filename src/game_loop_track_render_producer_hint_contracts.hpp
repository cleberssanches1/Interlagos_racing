#pragma once

namespace GameLoopRuntime
{

struct TrackRenderProducerHintPacket
{
    bool valid = false;
    bool producerJobInFlight = false;
};

} // namespace GameLoopRuntime
