#pragma once

#include "game_loop_cd_asset_anchor_decision_contracts.hpp"
#include "game_loop_cd_asset_bootstrap_decision_contracts.hpp"
#include "game_loop_cd_asset_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedCdAssetAnchorBootstrapDecisionPacket(
    const CdAssetFramePacket& framePacket,
    CdAssetAnchorBootstrapDecisionPacket& outPacket)
{
    outPacket.valid = framePacket.request.valid &&
                      framePacket.request.assetKind == CdAssetDomain::AssetKind::CarAnchorPoints;
    outPacket.shouldResolveAnchorAsset = outPacket.valid;
    outPacket.shouldReadAnchorAsset = framePacket.read.valid;
    outPacket.shouldParseAnchorAsset = framePacket.parse.valid;
    outPacket.hasValidAnchors = framePacket.carAnchors.parseSucceeded;
    outPacket.shouldUseAnchorFallback = outPacket.hasValidAnchors;
    outPacket.anchors = framePacket.carAnchors.anchors;
    outPacket.candidateCount = framePacket.telemetry.candidateCount;
}

inline CdAssetAnchorBootstrapDecisionPacket BuildCdAssetAnchorBootstrapDecisionPacket(
    const CdAssetFramePacket& framePacket)
{
    CdAssetAnchorBootstrapDecisionPacket packet{};
    SeedCdAssetAnchorBootstrapDecisionPacket(framePacket, packet);
    return packet;
}

inline CdAssetAnchorBootstrapDecisionPacket BuildCdAssetAnchorBootstrapDecisionPacket(
    const CdAssetBootstrapDecisionPacket& bootstrapDecision,
    const CdAssetFramePacket& framePacket)
{
    CdAssetAnchorBootstrapDecisionPacket packet{};
    packet.valid = bootstrapDecision.valid &&
                   bootstrapDecision.assetKind == CdAssetDomain::AssetKind::CarAnchorPoints;
    packet.shouldResolveAnchorAsset = bootstrapDecision.shouldResolveCandidates && packet.valid;
    packet.shouldReadAnchorAsset = bootstrapDecision.shouldReadAsset;
    packet.shouldParseAnchorAsset = bootstrapDecision.shouldParseAsset;
    packet.hasValidAnchors = bootstrapDecision.hasValidCarAnchors;
    packet.shouldUseAnchorFallback = packet.hasValidAnchors;
    packet.anchors = framePacket.carAnchors.anchors;
    packet.candidateCount = bootstrapDecision.candidateCount;
    return packet;
}

} // namespace GameLoopRuntime
