#pragma once

#include <cstdint>

#include "cd_asset_contracts.hpp"

namespace GameLoopRuntime
{

struct CdAssetBootstrapDecisionPacket
{
    bool valid = false;
    CdAssetDomain::AssetKind assetKind = CdAssetDomain::AssetKind::GenericBinary;
    bool shouldResolveCandidates = false;
    bool shouldReadAsset = false;
    bool shouldParseAsset = false;
    bool hasResolvedPath = false;
    bool readSucceeded = false;
    bool parseSucceeded = false;
    bool hasValidCarAnchors = false;
    uint32_t candidateCount = 0u;
    uint32_t chunkBytes = 0u;
};

} // namespace GameLoopRuntime
