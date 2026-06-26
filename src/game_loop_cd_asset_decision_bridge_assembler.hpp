#pragma once

#include "game_loop_cd_asset_anchor_decision_assembler.hpp"
#include "game_loop_cd_asset_bootstrap_decision_assembler.hpp"
#include "game_loop_cd_asset_decision_bridge_contracts.hpp"
#include "game_loop_cd_asset_sba_decision_assembler.hpp"

namespace GameLoopRuntime
{

inline void SeedCdAssetBootstrapDecisionBridgePacket(
    const CdAssetBootstrapDecisionPacket& bootstrapDecision,
    const CdAssetSbaBootstrapDecisionPacket& sbaDecision,
    const CdAssetAnchorBootstrapDecisionPacket& anchorDecision,
    CdAssetBootstrapDecisionBridgePacket& outPacket)
{
    outPacket.valid = bootstrapDecision.valid || sbaDecision.valid || anchorDecision.valid;
    outPacket.bootstrap = bootstrapDecision;
    outPacket.sba = sbaDecision;
    outPacket.anchor = anchorDecision;
}

inline CdAssetBootstrapDecisionBridgePacket BuildCdAssetBootstrapDecisionBridgePacket(
    const CdAssetBootstrapDecisionPacket& bootstrapDecision,
    const CdAssetSbaBootstrapDecisionPacket& sbaDecision,
    const CdAssetAnchorBootstrapDecisionPacket& anchorDecision)
{
    CdAssetBootstrapDecisionBridgePacket packet{};
    SeedCdAssetBootstrapDecisionBridgePacket(
        bootstrapDecision,
        sbaDecision,
        anchorDecision,
        packet);
    return packet;
}

inline CdAssetBootstrapDecisionBridgePacket BuildCdAssetBootstrapDecisionBridgePacket(
    const CdAssetFramePacket& framePacket)
{
    const auto bootstrapDecision = BuildCdAssetBootstrapDecisionPacket(framePacket);
    return BuildCdAssetBootstrapDecisionBridgePacket(
        bootstrapDecision,
        BuildCdAssetSbaBootstrapDecisionPacket(bootstrapDecision, framePacket),
        BuildCdAssetAnchorBootstrapDecisionPacket(bootstrapDecision, framePacket));
}

inline CdAssetBootstrapDecisionBridgePacket BuildCdAssetBootstrapDecisionBridgePacket(
    const CdAssetBootstrapDecisionPacket& bootstrapDecision,
    const CdAssetFramePacket& framePacket)
{
    return BuildCdAssetBootstrapDecisionBridgePacket(
        bootstrapDecision,
        BuildCdAssetSbaBootstrapDecisionPacket(bootstrapDecision, framePacket),
        BuildCdAssetAnchorBootstrapDecisionPacket(bootstrapDecision, framePacket));
}

} // namespace GameLoopRuntime
