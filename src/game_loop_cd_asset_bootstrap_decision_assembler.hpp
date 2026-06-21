#pragma once

#include "game_loop_cd_asset_bootstrap_decision_contracts.hpp"
#include "game_loop_cd_asset_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedCdAssetBootstrapDecisionPacket(const CdAssetFramePacket& framePacket,
                                               CdAssetBootstrapDecisionPacket& outPacket)
{
    outPacket.valid = framePacket.request.valid;
    outPacket.assetKind = framePacket.request.assetKind;
    outPacket.shouldResolveCandidates = framePacket.request.valid;
    outPacket.shouldReadAsset = framePacket.read.valid;
    outPacket.shouldParseAsset = framePacket.parse.valid;
    outPacket.hasResolvedPath = framePacket.read.resolvedPath != nullptr;
    outPacket.readSucceeded = framePacket.telemetry.readSucceeded;
    outPacket.parseSucceeded = framePacket.telemetry.parseSucceeded;
    outPacket.hasValidCarAnchors = framePacket.carAnchors.parseSucceeded;
    outPacket.candidateCount = framePacket.telemetry.candidateCount;
    outPacket.chunkBytes = framePacket.telemetry.chunkBytes;
}

inline CdAssetBootstrapDecisionPacket BuildCdAssetBootstrapDecisionPacket(
    const CdAssetFramePacket& framePacket)
{
    CdAssetBootstrapDecisionPacket packet{};
    SeedCdAssetBootstrapDecisionPacket(framePacket, packet);
    return packet;
}

} // namespace GameLoopRuntime
