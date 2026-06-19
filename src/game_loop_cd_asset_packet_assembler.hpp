#pragma once

#include "game_loop_cd_asset_packet.hpp"

namespace GameLoopRuntime
{

inline void SeedCdAssetFramePacket(
    const CdAssetDomain::CdAssetRequestPacket& request,
    const CdAssetDomain::CdAssetReadPacket& read,
    const CdAssetDomain::CdAssetParsePacket& parse,
    const CdAssetDomain::CdAssetTelemetry& telemetry,
    const CdAssetDomain::CarAnchorParseResult& carAnchors,
    CdAssetFramePacket& outPacket)
{
    outPacket.request = request;
    outPacket.read = read;
    outPacket.parse = parse;
    outPacket.telemetry = telemetry;
    outPacket.carAnchors = carAnchors;
}

} // namespace GameLoopRuntime
