#pragma once

#include "cd_asset_transition_ops.hpp"
#include "game_loop_cd_asset_anchor_decision_contracts.hpp"
#include "game_loop_cd_asset_sba_decision_contracts.hpp"

namespace Game
{

class CdAssetBootstrapRuntimeBridge final
{
public:
    static GameLoopRuntime::CdAssetSbaBootstrapDecisionPacket BuildSbaShadowModelDecision()
    {
        const auto request = CdAssetDomain::BuildSbaShadowModelRequest();
        const char* const resolvedPath = request.valid
                                             ? CdAssetDomain::ResolveSbaShadowModelPath()
                                             : nullptr;

        GameLoopRuntime::CdAssetSbaBootstrapDecisionPacket packet{};
        packet.valid = request.valid;
        packet.shouldResolveSbaPath = request.valid;
        packet.hasResolvedSbaPath = (resolvedPath != nullptr);
        packet.shouldAttemptSbaModelLoad = packet.hasResolvedSbaPath;
        packet.resolvedSbaPath = resolvedPath;
        packet.candidateCount = static_cast<uint32_t>(request.candidates.count);
        return packet;
    }

    static GameLoopRuntime::CdAssetAnchorBootstrapDecisionPacket BuildCarAnchorDecision()
    {
        const auto request = CdAssetDomain::BuildCarAnchorRequest();
        const auto anchors = request.valid
                                 ? CdAssetDomain::LoadCarAnchorPointsAsset()
                                 : CdAssetDomain::CarAnchorParseResult{}.anchors;

        GameLoopRuntime::CdAssetAnchorBootstrapDecisionPacket packet{};
        packet.valid = request.valid;
        packet.shouldResolveAnchorAsset = request.valid;
        packet.shouldReadAnchorAsset = request.valid;
        packet.shouldParseAnchorAsset = request.valid && request.requiresLightParse;
        packet.hasValidAnchors = anchors.valid;
        packet.shouldUseAnchorFallback = packet.hasValidAnchors;
        packet.anchors = anchors;
        packet.candidateCount = static_cast<uint32_t>(request.candidates.count);
        return packet;
    }
};

} // namespace Game
