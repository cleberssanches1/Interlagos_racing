#pragma once

#include "game_loop_reuse_observability_debug_assembler.hpp"
#include "game_loop_reuse_observability_debug_bundle_contracts.hpp"

namespace GameLoopObservabilityDomain
{

inline void SeedReuseObservabilityDebugBundle(const ReuseObservabilityPacket& reuse,
                                              const ReuseObservabilityDebugPacket& debug,
                                              ReuseObservabilityDebugBundle& outBundle)
{
    outBundle.valid = reuse.valid || debug.valid;
    outBundle.reuse = reuse;
    outBundle.debug = debug;
}

inline void SeedReuseObservabilityDebugBundle(const ReuseObservabilityPacket& reuse,
                                              ReuseObservabilityDebugBundle& outBundle)
{
    SeedReuseObservabilityDebugBundle(reuse,
                                      BuildReuseObservabilityDebugPacket(reuse),
                                      outBundle);
}

inline ReuseObservabilityDebugBundle BuildReuseObservabilityDebugBundle(
    const ReuseObservabilityPacket& reuse,
    const ReuseObservabilityDebugPacket& debug)
{
    ReuseObservabilityDebugBundle outBundle{};
    SeedReuseObservabilityDebugBundle(reuse, debug, outBundle);
    return outBundle;
}

inline ReuseObservabilityDebugBundle BuildReuseObservabilityDebugBundle(
    const ReuseObservabilityPacket& reuse)
{
    ReuseObservabilityDebugBundle outBundle{};
    SeedReuseObservabilityDebugBundle(reuse, outBundle);
    return outBundle;
}

} // namespace GameLoopObservabilityDomain
