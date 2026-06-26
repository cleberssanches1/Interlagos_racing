#pragma once

#include "game_loop_cd_asset_anchor_decision_contracts.hpp"
#include "game_loop_cd_asset_bootstrap_decision_contracts.hpp"
#include "game_loop_cd_asset_sba_decision_contracts.hpp"

namespace GameLoopRuntime
{

struct CdAssetBootstrapDecisionBridgePacket
{
    bool valid = false;
    CdAssetBootstrapDecisionPacket bootstrap{};
    CdAssetSbaBootstrapDecisionPacket sba{};
    CdAssetAnchorBootstrapDecisionPacket anchor{};
};

} // namespace GameLoopRuntime
