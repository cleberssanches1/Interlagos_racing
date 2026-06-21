#pragma once

#include "cd_asset_contracts.hpp"

namespace GameLoopRuntime
{

struct CdAssetSbaBootstrapDecisionPacket
{
    bool valid = false;
    bool shouldResolveSbaPath = false;
    bool hasResolvedSbaPath = false;
    bool shouldAttemptSbaModelLoad = false;
    const char* resolvedSbaPath = nullptr;
    uint32_t candidateCount = 0u;
};

} // namespace GameLoopRuntime
