#pragma once

#include "game_loop_cd_asset_bootstrap_decision_contracts.hpp"
#include "game_loop_cd_asset_packet.hpp"
#include "game_loop_cd_asset_sba_decision_contracts.hpp"

namespace GameLoopRuntime
{

inline void SeedCdAssetSbaBootstrapDecisionPacket(
    const CdAssetFramePacket& framePacket,
    CdAssetSbaBootstrapDecisionPacket& outPacket)
{
    outPacket.valid = framePacket.request.valid &&
                      framePacket.request.assetKind == CdAssetDomain::AssetKind::SbaShadowModel;
    outPacket.shouldResolveSbaPath = outPacket.valid;
    outPacket.hasResolvedSbaPath = framePacket.read.resolvedPath != nullptr;
    outPacket.shouldAttemptSbaModelLoad = outPacket.hasResolvedSbaPath;
    outPacket.resolvedSbaPath = framePacket.read.resolvedPath;
    outPacket.candidateCount = framePacket.telemetry.candidateCount;
}

inline CdAssetSbaBootstrapDecisionPacket BuildCdAssetSbaBootstrapDecisionPacket(
    const CdAssetFramePacket& framePacket)
{
    CdAssetSbaBootstrapDecisionPacket packet{};
    SeedCdAssetSbaBootstrapDecisionPacket(framePacket, packet);
    return packet;
}

inline CdAssetSbaBootstrapDecisionPacket BuildCdAssetSbaBootstrapDecisionPacket(
    const CdAssetBootstrapDecisionPacket& bootstrapDecision,
    const CdAssetFramePacket& framePacket)
{
    CdAssetSbaBootstrapDecisionPacket packet{};
    packet.valid = bootstrapDecision.valid &&
                   bootstrapDecision.assetKind == CdAssetDomain::AssetKind::SbaShadowModel;
    packet.shouldResolveSbaPath = bootstrapDecision.shouldResolveCandidates && packet.valid;
    packet.hasResolvedSbaPath = bootstrapDecision.hasResolvedPath;
    packet.shouldAttemptSbaModelLoad = packet.hasResolvedSbaPath;
    packet.resolvedSbaPath = framePacket.read.resolvedPath;
    packet.candidateCount = bootstrapDecision.candidateCount;
    return packet;
}

} // namespace GameLoopRuntime
