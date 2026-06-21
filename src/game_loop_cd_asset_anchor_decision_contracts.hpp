#pragma once

#include "cd_asset_system.hpp"

namespace GameLoopRuntime
{

struct CdAssetAnchorBootstrapDecisionPacket
{
    bool valid = false;
    bool shouldResolveAnchorAsset = false;
    bool shouldReadAnchorAsset = false;
    bool shouldParseAnchorAsset = false;
    bool hasValidAnchors = false;
    bool shouldUseAnchorFallback = false;
    Game::CdAssetSystem::CarAnchorPoints anchors{};
    uint32_t candidateCount = 0u;
};

} // namespace GameLoopRuntime
